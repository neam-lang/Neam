// SPDX-License-Identifier: Apache-2.0
//
// Neam Virtual Machine - Native function registry
//

#include "neamc/vm/native.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <chrono>
#include <cmath>
#include <ctime>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <random>
#include <sstream>
#include <thread>
#include <unordered_map>

#include <curl/curl.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/md5.h>
#include <openssl/sha.h>

#include "neamc/vm/table.hpp"
#include "neamc/vm/async/future.hpp"
#include "neamc/vm/async/executor.hpp"
#include "neamc/vm/runtime_type.hpp"
#include "neamc/util/base64.hpp"
#include "neamc/voice/audio_buffer.hpp"
#include "neamc/voice/pipeline.hpp"
#include "neamc/voice/realtime.hpp"
#include "neamc/voice/stt.hpp"
#include "neamc/voice/tts.hpp"
#include "neamc/vm/vm.hpp"
#include "neamc/llm/provider_factory.hpp"

namespace neamc::vm
{
namespace
{
namespace fs = std::filesystem;

const Value* find_global_value(const Table& table, const std::string& name)
{
  for (const auto& entry : table.entries())
  {
    if (entry.key && entry.key->length == name.size() &&
        std::memcmp(entry.key->chars, name.data(), entry.key->length) == 0)
    {
      return &entry.value;
    }
  }
  return nullptr;
}

std::string to_std_string(const Value& value)
{
  if (!value.is_string())
  {
    throw std::runtime_error("Expected string value");
  }
  auto* str = as_string(value);
  return std::string(str->chars, str->length);
}

std::string to_std_string(const ObjString* str)
{
  if (!str) return "";
  return std::string(str->chars, str->length);
}

double to_number(const Value& value)
{
  if (!value.is_number())
  {
    throw std::runtime_error("Expected number value");
  }
  return value.as_number();
}

std::string value_to_string(const Value& value)
{
  if (value.is_string())
  {
    auto* str = as_string(value);
    return std::string(str->chars, str->length);
  }
  if (value.is_number())
  {
    std::ostringstream out;
    out << value.as_number();
    return out.str();
  }
  if (value.is_bool())
  {
    return value.as_bool() ? "true" : "false";
  }
  if (value.is_nil())
  {
    return "nil";
  }
  return "<object>";
}

bool values_equal(const Value& lhs, const Value& rhs)
{
  if (lhs.type != rhs.type)
  {
    return false;
  }
  switch (lhs.type)
  {
    case ValueType::Nil:
      return true;
    case ValueType::Bool:
      return lhs.as_bool() == rhs.as_bool();
    case ValueType::Number:
      return lhs.as_number() == rhs.as_number();
    case ValueType::Obj:
      if (is_obj_type(lhs, ObjType::OBJ_STRING) && is_obj_type(rhs, ObjType::OBJ_STRING))
      {
        auto* a = as_string(lhs);
        auto* b = as_string(rhs);
        if (a->length != b->length)
        {
          return false;
        }
        return std::memcmp(a->chars, b->chars, a->length) == 0;
      }
      return lhs.as_obj() == rhs.as_obj();
  }
  return false;
}

bool is_truthy(const Value& value)
{
  if (value.is_nil())
  {
    return false;
  }
  if (value.is_bool())
  {
    return value.as_bool();
  }
  if (value.is_number())
  {
    return value.as_number() != 0.0;
  }
  return true;
}

Value make_result_ok(Value value)
{
  std::unordered_map<std::string, Value> entries;
  entries.emplace("ok", Value::Bool(true));
  entries.emplace("value", std::move(value));
  return Value::Map(new_map(std::move(entries)));
}

Value make_result_err(const std::string& error)
{
  std::unordered_map<std::string, Value> entries;
  entries.emplace("ok", Value::Bool(false));
  entries.emplace("error", Value::String(error.c_str(), error.size()));
  return Value::Map(new_map(std::move(entries)));
}

Value make_ready_future_value(Value value)
{
  auto future = async::make_ready_future(std::move(value));
  auto shared_future = std::make_shared<async::Future<Value>>(std::move(future));
  return Value::Future(new_future(std::move(shared_future)));
}

Value make_error_future_value(const std::string& message)
{
  auto state = std::make_shared<async::SharedState<Value>>();
  {
    std::lock_guard<std::mutex> lock(state->mutex);
    state->state = async::FutureState::kFailed;
    state->error = std::make_exception_ptr(std::runtime_error(message));
  }
  state->cv.notify_all();
  auto future = async::Future<Value>(std::move(state));
  auto shared_future = std::make_shared<async::Future<Value>>(std::move(future));
  return Value::Future(new_future(std::move(shared_future)));
}

Value make_result_bool(bool value)
{
  return Value::Bool(value);
}

std::mt19937& rng()
{
  thread_local std::mt19937 generator{std::random_device{}()};
  return generator;
}

std::string hex_encode_bytes(const std::vector<uint8_t>& bytes)
{
  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for (uint8_t byte : bytes)
  {
    out << std::setw(2) << static_cast<int>(byte);
  }
  return out.str();
}

std::vector<uint8_t> hex_decode_string(const std::string& text)
{
  if (text.size() % 2 != 0)
  {
    throw std::runtime_error("Hex string must have even length");
  }
  std::vector<uint8_t> bytes;
  bytes.reserve(text.size() / 2);
  for (std::size_t i = 0; i < text.size(); i += 2)
  {
    const std::string slice = text.substr(i, 2);
    uint8_t value = static_cast<uint8_t>(std::stoul(slice, nullptr, 16));
    bytes.push_back(value);
  }
  return bytes;
}

std::string base64_encode_bytes(const std::vector<uint8_t>& data)
{
  return util::base64_encode(data);
}

std::vector<uint8_t> base64_decode_bytes(const std::string& input)
{
  return util::base64_decode(input);
}

std::vector<uint8_t> list_to_bytes(const Value& value)
{
  if (!value.is_list())
  {
    throw std::runtime_error("Expected list of bytes");
  }
  auto* list = as_list(value);
  std::vector<uint8_t> bytes;
  bytes.reserve(list->items.size());
  for (const auto& item : list->items)
  {
    if (!item.is_number())
    {
      throw std::runtime_error("Byte list must contain numbers");
    }
    const double num = item.as_number();
    if (num < 0.0 || num > 255.0)
    {
      throw std::runtime_error("Byte value out of range");
    }
    bytes.push_back(static_cast<uint8_t>(num));
  }
  return bytes;
}

Value bytes_to_list(const std::vector<uint8_t>& bytes)
{
  std::vector<Value> items;
  items.reserve(bytes.size());
  for (auto byte : bytes)
  {
    items.push_back(Value::Number(static_cast<double>(byte)));
  }
  return Value::List(new_list(std::move(items)));
}

bool result_ok(const Value& value)
{
  if (!value.is_map())
  {
    return false;
  }
  auto* map = as_map(value);
  auto it = map->entries.find("ok");
  if (it == map->entries.end())
  {
    return false;
  }
  if (!it->second.is_bool())
  {
    return false;
  }
  return it->second.as_bool();
}

std::string result_error_message(const Value& value)
{
  if (value.is_map())
  {
    auto* map = as_map(value);
    auto it = map->entries.find("error");
    if (it != map->entries.end())
    {
      return value_to_string(it->second);
    }
  }
  return value_to_string(value);
}

struct CurlGlobal
{
  CurlGlobal() { curl_global_init(CURL_GLOBAL_DEFAULT); }
  ~CurlGlobal() { curl_global_cleanup(); }
};

size_t write_callback(char* ptr, size_t size, size_t nmemb, void* userdata)
{
  auto* buffer = static_cast<std::string*>(userdata);
  const size_t total = size * nmemb;
  buffer->append(ptr, total);
  return total;
}

const CurlGlobal& curl_global()
{
  static const CurlGlobal global_init;
  return global_init;
}

Value print_native(VirtualMachine& vm, int arg_count, Value* args)
{
  std::ostream& out = vm.output_stream();
  for (int i = 0; i < arg_count; ++i)
  {
    const auto& arg = args[i];
    if (is_obj_type(arg, ObjType::OBJ_STRING))
    {
      out.write(as_string(arg)->chars, static_cast<std::streamsize>(as_string(arg)->length));
    }
    else if (arg.is_number())
    {
      out << arg.as_number();
    }
    else if (arg.is_bool())
    {
      out << (arg.as_bool() ? "true" : "false");
    }
    else if (arg.is_nil())
    {
      out << "nil";
    }
    else if (arg.is_list())
    {
      const auto* list = as_list(arg);
      out << "[";
      for (std::size_t j = 0; j < list->items.size(); ++j)
      {
        const auto& item = list->items[j];
        if (item.is_string())
        {
          auto* str = as_string(item);
          out << std::string(str->chars, str->length);
        }
        else if (item.is_number())
        {
          out << item.as_number();
        }
        else if (item.is_bool())
        {
          out << (item.as_bool() ? "true" : "false");
        }
        else if (item.is_nil())
        {
          out << "nil";
        }
        else
        {
          out << "<object>";
        }
        if (j + 1 < list->items.size())
        {
          out << ", ";
        }
      }
      out << "]";
    }
    else if (arg.is_map())
    {
      const auto* map = as_map(arg);
      out << "{";
      std::size_t count = 0;
      for (const auto& entry : map->entries)
      {
        out << entry.first << ": ";
        const auto& value = entry.second;
        if (value.is_string())
        {
          auto* str = as_string(value);
          out << std::string(str->chars, str->length);
        }
        else if (value.is_number())
        {
          out << value.as_number();
        }
        else if (value.is_bool())
        {
          out << (value.as_bool() ? "true" : "false");
        }
        else if (value.is_nil())
        {
          out << "nil";
        }
        else
        {
          out << "<object>";
        }
        if (++count < map->entries.size())
        {
          out << ", ";
        }
      }
      out << "}";
    }
    else if (arg.is_agent())
    {
      auto* agent = as_agent(arg);
      auto* name = agent->name;
      out << "<agent " << std::string(name->chars, name->length) << ">";
    }
    else if (arg.is_skill())
    {
      auto* skill = as_skill(arg);
      auto* name = skill->name;
      out << "<skill " << std::string(name->chars, name->length) << ">";
    }
    if (i + 1 < arg_count)
    {
      out << " ";
    }
  }
  out << std::endl;
  return Value::Nil();
}

Value clock_native(VirtualMachine& vm, int, Value*)
{
  (void)vm;
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
  return Value::Number(static_cast<double>(ms) / 1000.0);
}

Value input_native(VirtualMachine& vm, int, Value*)
{
  std::string line;
  if (!std::getline(vm.input_stream(), line))
  {
    return Value::Nil();
  }
  return Value::ObjVal(copy_string(line.c_str(), line.size()));
}

Value typeof_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 1)
  {
    throw std::runtime_error("typeof expects 1 argument");
  }
  const auto name = typeof_name(args[0]);
  return Value::String(name.c_str(), name.size());
}

Value json_to_value(const nlohmann::json& json)
{
  if (json.is_null())
  {
    return Value::Nil();
  }
  if (json.is_boolean())
  {
    return Value::Bool(json.get<bool>());
  }
  if (json.is_number())
  {
    return Value::Number(json.get<double>());
  }
  if (json.is_string())
  {
    const auto value = json.get<std::string>();
    return Value::String(value.c_str(), value.size());
  }
  if (json.is_array())
  {
    std::vector<Value> items;
    items.reserve(json.size());
    for (const auto& item : json)
    {
      items.push_back(json_to_value(item));
    }
    return Value::List(new_list(std::move(items)));
  }
  if (json.is_object())
  {
    std::unordered_map<std::string, Value> entries;
    for (auto it = json.begin(); it != json.end(); ++it)
    {
      entries.emplace(it.key(), json_to_value(it.value()));
    }
    return Value::Map(new_map(std::move(entries)));
  }
  return Value::Nil();
}

nlohmann::json value_to_json(const Value& value)
{
  if (value.is_nil())
  {
    return nullptr;
  }
  if (value.is_bool())
  {
    return value.as_bool();
  }
  if (value.is_number())
  {
    return value.as_number();
  }
  if (value.is_string())
  {
    auto* str = as_string(value);
    return std::string(str->chars, str->length);
  }
  if (value.is_list())
  {
    nlohmann::json result = nlohmann::json::array();
    auto* list = as_list(value);
    for (const auto& item : list->items)
    {
      result.push_back(value_to_json(item));
    }
    return result;
  }
  if (value.is_map())
  {
    nlohmann::json result = nlohmann::json::object();
    auto* map = as_map(value);
    for (const auto& entry : map->entries)
    {
      result[entry.first] = value_to_json(entry.second);
    }
    return result;
  }
  return "<object>";
}

Value json_parse_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 1)
  {
    throw std::runtime_error("json_parse expects 1 argument");
  }
  const std::string text = to_std_string(args[0]);
  return json_to_value(nlohmann::json::parse(text));
}

Value json_stringify_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 1)
  {
    throw std::runtime_error("json_stringify expects 1 argument");
  }
  const auto json = value_to_json(args[0]);
  const auto text = json.dump();
  return Value::String(text.c_str(), text.size());
}

Value file_read_string_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 1)
  {
    throw std::runtime_error("file_read_string expects 1 argument");
  }
  const std::string path = to_std_string(args[0]);
  std::ifstream in(path, std::ios::binary);
  if (!in)
  {
    return make_ready_future_value(make_result_err("Failed to open file"));
  }
  std::ostringstream buffer;
  buffer << in.rdbuf();
  const std::string contents = buffer.str();
  return make_ready_future_value(
      make_result_ok(Value::String(contents.c_str(), contents.size())));
}

Value file_write_string_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 2)
  {
    throw std::runtime_error("file_write_string expects 2 arguments");
  }
  const std::string path = to_std_string(args[0]);
  const std::string contents = to_std_string(args[1]);
  std::ofstream out(path, std::ios::binary);
  if (!out)
  {
    return make_ready_future_value(make_result_err("Failed to open file for writing"));
  }
  out.write(contents.data(), static_cast<std::streamsize>(contents.size()));
  if (!out)
  {
    return make_ready_future_value(make_result_err("Failed to write file"));
  }
  return make_ready_future_value(make_result_ok(Value::Nil()));
}

Value file_read_bytes_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 1)
  {
    throw std::runtime_error("file_read_bytes expects 1 argument");
  }
  const std::string path = to_std_string(args[0]);
  std::ifstream in(path, std::ios::binary);
  if (!in)
  {
    return make_ready_future_value(make_result_err("Failed to open file"));
  }
  std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                             std::istreambuf_iterator<char>());
  return make_ready_future_value(make_result_ok(bytes_to_list(bytes)));
}

Value file_write_bytes_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 2)
  {
    throw std::runtime_error("file_write_bytes expects 2 arguments");
  }
  const std::string path = to_std_string(args[0]);
  std::vector<uint8_t> bytes = list_to_bytes(args[1]);
  std::ofstream out(path, std::ios::binary);
  if (!out)
  {
    return make_ready_future_value(make_result_err("Failed to open file for writing"));
  }
  out.write(reinterpret_cast<const char*>(bytes.data()),
            static_cast<std::streamsize>(bytes.size()));
  if (!out)
  {
    return make_ready_future_value(make_result_err("Failed to write file"));
  }
  return make_ready_future_value(make_result_ok(Value::Nil()));
}

Value file_exists_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 1)
  {
    throw std::runtime_error("file_exists expects 1 argument");
  }
  const std::string path = to_std_string(args[0]);
  return make_result_bool(fs::exists(fs::path(path)));
}

Value file_remove_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 1)
  {
    throw std::runtime_error("file_remove expects 1 argument");
  }
  const std::string path = to_std_string(args[0]);
  std::error_code ec;
  fs::remove(fs::path(path), ec);
  if (ec)
  {
    return make_ready_future_value(make_result_err("Failed to remove file"));
  }
  return make_ready_future_value(make_result_ok(Value::Nil()));
}

Value file_copy_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 2)
  {
    throw std::runtime_error("file_copy expects 2 arguments");
  }
  const std::string from = to_std_string(args[0]);
  const std::string to = to_std_string(args[1]);
  std::error_code ec;
  fs::copy_file(fs::path(from), fs::path(to), fs::copy_options::overwrite_existing, ec);
  if (ec)
  {
    return make_ready_future_value(make_result_err("Failed to copy file"));
  }
  return make_ready_future_value(make_result_ok(Value::Nil()));
}

Value file_rename_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 2)
  {
    throw std::runtime_error("file_rename expects 2 arguments");
  }
  const std::string from = to_std_string(args[0]);
  const std::string to = to_std_string(args[1]);
  std::error_code ec;
  fs::rename(fs::path(from), fs::path(to), ec);
  if (ec)
  {
    return make_ready_future_value(make_result_err("Failed to rename file"));
  }
  return make_ready_future_value(make_result_ok(Value::Nil()));
}

Value file_open_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 2)
  {
    throw std::runtime_error("file_open expects 2 arguments");
  }
  const std::string path = to_std_string(args[0]);
  const std::string mode = to_std_string(args[1]);
  std::unordered_map<std::string, Value> entries;
  entries.emplace("path", Value::String(path.c_str(), path.size()));
  entries.emplace("mode", Value::String(mode.c_str(), mode.size()));
  return Value::Map(new_map(std::move(entries)));
}

Value panic_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 1)
  {
    throw std::runtime_error("panic expects 1 argument");
  }
  const std::string message = value_to_string(args[0]);
  throw std::runtime_error("panic: " + message);
}

Value context_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 2)
  {
    throw std::runtime_error("context expects 2 arguments");
  }
  const Value& result = args[0];
  const std::string message = value_to_string(args[1]);
  if (result_ok(result))
  {
    return result;
  }
  const std::string error = result_error_message(result);
  if (error.empty())
  {
    return make_result_err(message);
  }
  return make_result_err(message + ": " + error);
}

Value with_context_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 3)
  {
    throw std::runtime_error("with_context expects 3 arguments");
  }
  const Value& result = args[0];
  if (result_ok(result))
  {
    return result;
  }
  const std::string key = value_to_string(args[1]);
  const std::string value = value_to_string(args[2]);
  const std::string error = result_error_message(result);
  std::string message = error.empty() ? "error" : error;
  message += " [" + key + "=" + value + "]";
  return make_result_err(message);
}

Value assert_eq_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 2)
  {
    throw std::runtime_error("assert_eq expects 2 arguments");
  }
  if (!values_equal(args[0], args[1]))
  {
    throw std::runtime_error("Assertion failed: values are not equal");
  }
  return Value::Nil();
}

Value assert_ne_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 2)
  {
    throw std::runtime_error("assert_ne expects 2 arguments");
  }
  if (values_equal(args[0], args[1]))
  {
    throw std::runtime_error("Assertion failed: values are equal");
  }
  return Value::Nil();
}

Value assert_true_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 1)
  {
    throw std::runtime_error("assert_true expects 1 argument");
  }
  if (!is_truthy(args[0]))
  {
    throw std::runtime_error("Assertion failed: expected true");
  }
  return Value::Nil();
}

Value assert_false_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 1)
  {
    throw std::runtime_error("assert_false expects 1 argument");
  }
  if (is_truthy(args[0]))
  {
    throw std::runtime_error("Assertion failed: expected false");
  }
  return Value::Nil();
}

Value assert_some_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 1)
  {
    throw std::runtime_error("assert_some expects 1 argument");
  }
  if (!is_obj_type(args[0], ObjType::OBJ_OPTION))
  {
    throw std::runtime_error("assert_some expects Option");
  }
  auto* opt = as_option(args[0]);
  if (!opt->has_value)
  {
    throw std::runtime_error("Assertion failed: expected Some");
  }
  return Value::Nil();
}

Value assert_none_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 1)
  {
    throw std::runtime_error("assert_none expects 1 argument");
  }
  if (!is_obj_type(args[0], ObjType::OBJ_OPTION))
  {
    throw std::runtime_error("assert_none expects Option");
  }
  auto* opt = as_option(args[0]);
  if (opt->has_value)
  {
    throw std::runtime_error("Assertion failed: expected None");
  }
  return Value::Nil();
}

Value assert_ok_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 1)
  {
    throw std::runtime_error("assert_ok expects 1 argument");
  }
  if (!result_ok(args[0]))
  {
    throw std::runtime_error("Assertion failed: expected Ok");
  }
  return Value::Nil();
}

Value assert_err_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 1)
  {
    throw std::runtime_error("assert_err expects 1 argument");
  }
  if (result_ok(args[0]))
  {
    throw std::runtime_error("Assertion failed: expected Err");
  }
  return Value::Nil();
}

Value assert_throws_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count < 1 || arg_count > 2)
  {
    throw std::runtime_error("assert_throws expects 1 or 2 arguments");
  }
  if (result_ok(args[0]))
  {
    throw std::runtime_error("Assertion failed: expected error");
  }
  return Value::Nil();
}

Value http_get_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 1)
  {
    throw std::runtime_error("http_get expects 1 argument");
  }
  const std::string url = to_std_string(args[0]);
  (void)curl_global();
  CURL* handle = curl_easy_init();
  if (!handle)
  {
    return make_ready_future_value(make_result_err("Failed to initialize curl"));
  }
  std::string response;
  curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
  curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(handle, CURLOPT_WRITEDATA, &response);
  CURLcode res = curl_easy_perform(handle);
  long status = 0;
  curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
  curl_easy_cleanup(handle);
  if (res != CURLE_OK)
  {
    return make_ready_future_value(
        make_result_err(std::string("HTTP request failed: ") + curl_easy_strerror(res)));
  }
  std::unordered_map<std::string, Value> response_map;
  response_map.emplace("status", Value::Number(static_cast<double>(status)));
  response_map.emplace("body", Value::String(response.c_str(), response.size()));
  return make_ready_future_value(make_result_ok(Value::Map(new_map(std::move(response_map)))));
}

Value http_request_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 4)
  {
    throw std::runtime_error("http_request expects 4 arguments");
  }
  const std::string method = to_std_string(args[0]);
  const std::string url = to_std_string(args[1]);
  const std::string body = args[2].is_nil() ? std::string() : to_std_string(args[2]);
  (void)curl_global();
  CURL* handle = curl_easy_init();
  if (!handle)
  {
    return make_ready_future_value(make_result_err("Failed to initialize curl"));
  }
  struct curl_slist* headers = nullptr;
  if (!args[3].is_nil())
  {
    if (!args[3].is_map())
    {
      curl_easy_cleanup(handle);
      throw std::runtime_error("http_request headers must be a map or nil");
    }
    auto* header_map = as_map(args[3]);
    for (const auto& entry : header_map->entries)
    {
      const std::string header_value = value_to_string(entry.second);
      const std::string header = entry.first + ": " + header_value;
      headers = curl_slist_append(headers, header.c_str());
    }
  }
  std::string response;
  curl_easy_setopt(handle, CURLOPT_URL, url.c_str());
  curl_easy_setopt(handle, CURLOPT_CUSTOMREQUEST, method.c_str());
  if (!body.empty())
  {
    curl_easy_setopt(handle, CURLOPT_POSTFIELDS, body.c_str());
  }
  if (headers)
  {
    curl_easy_setopt(handle, CURLOPT_HTTPHEADER, headers);
  }
  curl_easy_setopt(handle, CURLOPT_WRITEFUNCTION, write_callback);
  curl_easy_setopt(handle, CURLOPT_WRITEDATA, &response);
  CURLcode res = curl_easy_perform(handle);
  long status = 0;
  curl_easy_getinfo(handle, CURLINFO_RESPONSE_CODE, &status);
  curl_easy_cleanup(handle);
  if (headers)
  {
    curl_slist_free_all(headers);
  }
  if (res != CURLE_OK)
  {
    return make_ready_future_value(
        make_result_err(std::string("HTTP request failed: ") + curl_easy_strerror(res)));
  }
  std::unordered_map<std::string, Value> response_map;
  response_map.emplace("status", Value::Number(static_cast<double>(status)));
  response_map.emplace("body", Value::String(response.c_str(), response.size()));
  return make_ready_future_value(make_result_ok(Value::Map(new_map(std::move(response_map)))));
}

Value math_abs_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 1)
  {
    throw std::runtime_error("math_abs expects 1 argument");
  }
  return Value::Number(std::abs(to_number(args[0])));
}

Value math_floor_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 1)
  {
    throw std::runtime_error("math_floor expects 1 argument");
  }
  return Value::Number(std::floor(to_number(args[0])));
}

Value math_ceil_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 1)
  {
    throw std::runtime_error("math_ceil expects 1 argument");
  }
  return Value::Number(std::ceil(to_number(args[0])));
}

Value math_round_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 1)
  {
    throw std::runtime_error("math_round expects 1 argument");
  }
  return Value::Number(std::round(to_number(args[0])));
}

Value math_min_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 2)
  {
    throw std::runtime_error("math_min expects 2 arguments");
  }
  return Value::Number(std::min(to_number(args[0]), to_number(args[1])));
}

Value math_max_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 2)
  {
    throw std::runtime_error("math_max expects 2 arguments");
  }
  return Value::Number(std::max(to_number(args[0]), to_number(args[1])));
}

Value math_clamp_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 3)
  {
    throw std::runtime_error("math_clamp expects 3 arguments");
  }
  const double value = to_number(args[0]);
  const double low = to_number(args[1]);
  const double high = to_number(args[2]);
  return Value::Number(std::max(low, std::min(value, high)));
}

Value math_sin_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 1)
  {
    throw std::runtime_error("math_sin expects 1 argument");
  }
  return Value::Number(std::sin(to_number(args[0])));
}

Value math_cos_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 1)
  {
    throw std::runtime_error("math_cos expects 1 argument");
  }
  return Value::Number(std::cos(to_number(args[0])));
}

Value math_tan_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 1)
  {
    throw std::runtime_error("math_tan expects 1 argument");
  }
  return Value::Number(std::tan(to_number(args[0])));
}

Value math_asin_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 1)
  {
    throw std::runtime_error("math_asin expects 1 argument");
  }
  return Value::Number(std::asin(to_number(args[0])));
}

Value math_acos_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 1)
  {
    throw std::runtime_error("math_acos expects 1 argument");
  }
  return Value::Number(std::acos(to_number(args[0])));
}

Value math_atan_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 1)
  {
    throw std::runtime_error("math_atan expects 1 argument");
  }
  return Value::Number(std::atan(to_number(args[0])));
}

Value math_atan2_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 2)
  {
    throw std::runtime_error("math_atan2 expects 2 arguments");
  }
  return Value::Number(std::atan2(to_number(args[0]), to_number(args[1])));
}

Value math_pow_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 2)
  {
    throw std::runtime_error("math_pow expects 2 arguments");
  }
  return Value::Number(std::pow(to_number(args[0]), to_number(args[1])));
}

Value math_sqrt_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 1)
  {
    throw std::runtime_error("math_sqrt expects 1 argument");
  }
  return Value::Number(std::sqrt(to_number(args[0])));
}

Value math_cbrt_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 1)
  {
    throw std::runtime_error("math_cbrt expects 1 argument");
  }
  return Value::Number(std::cbrt(to_number(args[0])));
}

Value math_log_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 1)
  {
    throw std::runtime_error("math_log expects 1 argument");
  }
  return Value::Number(std::log(to_number(args[0])));
}

Value math_log10_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 1)
  {
    throw std::runtime_error("math_log10 expects 1 argument");
  }
  return Value::Number(std::log10(to_number(args[0])));
}

Value math_exp_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 1)
  {
    throw std::runtime_error("math_exp expects 1 argument");
  }
  return Value::Number(std::exp(to_number(args[0])));
}

Value math_random_native(VirtualMachine&, int arg_count, Value*)
{
  if (arg_count != 0)
  {
    throw std::runtime_error("math_random expects 0 arguments");
  }
  std::uniform_real_distribution<double> dist(0.0, 1.0);
  return Value::Number(dist(rng()));
}

Value math_random_int_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 2)
  {
    throw std::runtime_error("math_random_int expects 2 arguments");
  }
  const auto min_value = static_cast<int64_t>(to_number(args[0]));
  const auto max_value = static_cast<int64_t>(to_number(args[1]));
  if (min_value > max_value)
  {
    throw std::runtime_error("math_random_int expects min <= max");
  }
  std::uniform_int_distribution<int64_t> dist(min_value, max_value);
  return Value::Number(static_cast<double>(dist(rng())));
}

Value time_now_native(VirtualMachine&, int arg_count, Value*)
{
  if (arg_count != 0)
  {
    throw std::runtime_error("time_now expects 0 arguments");
  }
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  const auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(now).count();
  return Value::Number(static_cast<double>(ms));
}

Value time_now_millis_native(VirtualMachine& vm, int arg_count, Value* args)
{
  return time_now_native(vm, arg_count, args);
}

Value time_now_micros_native(VirtualMachine&, int arg_count, Value*)
{
  if (arg_count != 0)
  {
    throw std::runtime_error("time_now_micros expects 0 arguments");
  }
  const auto now = std::chrono::system_clock::now().time_since_epoch();
  const auto micros = std::chrono::duration_cast<std::chrono::microseconds>(now).count();
  return Value::Number(static_cast<double>(micros));
}

Value time_sleep_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 1)
  {
    throw std::runtime_error("time_sleep expects 1 argument");
  }
  const auto ms = static_cast<int64_t>(to_number(args[0]));
  std::this_thread::sleep_for(std::chrono::milliseconds(ms));
  return Value::Nil();
}

std::time_t timegm_compat(std::tm* tm)
{
#if defined(_WIN32)
  return _mkgmtime(tm);
#else
  return timegm(tm);
#endif
}

Value time_format_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 2)
  {
    throw std::runtime_error("time_format expects 2 arguments");
  }
  const auto millis = static_cast<int64_t>(to_number(args[0]));
  const std::string format = to_std_string(args[1]);
  const auto tp = std::chrono::system_clock::time_point(std::chrono::milliseconds(millis));
  const auto time = std::chrono::system_clock::to_time_t(tp);
  std::tm tm{};
#if defined(_WIN32)
  gmtime_s(&tm, &time);
#else
  gmtime_r(&time, &tm);
#endif
  std::ostringstream out;
  out << std::put_time(&tm, format.c_str());
  const std::string result = out.str();
  return Value::String(result.c_str(), result.size());
}

Value time_parse_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 2)
  {
    throw std::runtime_error("time_parse expects 2 arguments");
  }
  const std::string text = to_std_string(args[0]);
  const std::string format = to_std_string(args[1]);
  std::tm tm{};
  std::istringstream in(text);
  in >> std::get_time(&tm, format.c_str());
  if (in.fail())
  {
    throw std::runtime_error("Failed to parse datetime");
  }
  const auto time = timegm_compat(&tm);
  const auto millis = std::chrono::duration_cast<std::chrono::milliseconds>(
                          std::chrono::system_clock::from_time_t(time).time_since_epoch())
                          .count();
  return Value::Number(static_cast<double>(millis));
}

Value future_resolve_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 1)
  {
    throw std::runtime_error("future_resolve expects 1 argument");
  }
  return make_ready_future_value(args[0]);
}

Value future_reject_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 1)
  {
    throw std::runtime_error("future_reject expects 1 argument");
  }
  return make_error_future_value(result_error_message(args[0]));
}

Value future_all_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 1)
  {
    throw std::runtime_error("future_all expects 1 argument");
  }
  if (!args[0].is_list())
  {
    throw std::runtime_error("future_all expects list of futures");
  }
  auto* list = as_list(args[0]);
  std::vector<std::shared_ptr<async::Future<Value>>> futures;
  futures.reserve(list->items.size());
  for (const auto& item : list->items)
  {
    if (!item.is_future())
    {
      throw std::runtime_error("future_all expects list of futures");
    }
    futures.push_back(as_future(item)->future);
  }
  auto future = async::Executor::global().submit([futures]() {
    std::vector<Value> results;
    results.reserve(futures.size());
    for (const auto& future : futures)
    {
      results.push_back(future->wait());
    }
    return Value::List(new_list(std::move(results)));
  });
  auto shared_future = std::make_shared<async::Future<Value>>(std::move(future));
  return Value::Future(new_future(std::move(shared_future)));
}

Value future_race_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 1)
  {
    throw std::runtime_error("future_race expects 1 argument");
  }
  if (!args[0].is_list())
  {
    throw std::runtime_error("future_race expects list of futures");
  }
  auto* list = as_list(args[0]);
  std::vector<std::shared_ptr<async::Future<Value>>> futures;
  futures.reserve(list->items.size());
  for (const auto& item : list->items)
  {
    if (!item.is_future())
    {
      throw std::runtime_error("future_race expects list of futures");
    }
    futures.push_back(as_future(item)->future);
  }
  auto future = async::Executor::global().submit([futures]() {
    while (true)
    {
      for (const auto& future : futures)
      {
        if (future->is_ready())
        {
          return future->wait();
        }
      }
      std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
  });
  auto shared_future = std::make_shared<async::Future<Value>>(std::move(future));
  return Value::Future(new_future(std::move(shared_future)));
}

Value future_delay_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 1)
  {
    throw std::runtime_error("future_delay expects 1 argument");
  }
  const auto ms = static_cast<int64_t>(to_number(args[0]));
  auto future = async::Executor::global().submit([ms]() {
    std::this_thread::sleep_for(std::chrono::milliseconds(ms));
    return Value::Nil();
  });
  auto shared_future = std::make_shared<async::Future<Value>>(std::move(future));
  return Value::Future(new_future(std::move(shared_future)));
}

Value crypto_hash_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 2)
  {
    throw std::runtime_error("crypto_hash expects 2 arguments");
  }
  const std::string algorithm = to_std_string(args[0]);
  const std::string data = to_std_string(args[1]);
  std::vector<uint8_t> digest;
  if (algorithm == "sha256")
  {
    digest.resize(SHA256_DIGEST_LENGTH);
    SHA256(reinterpret_cast<const unsigned char*>(data.data()), data.size(), digest.data());
  }
  else if (algorithm == "sha512")
  {
    digest.resize(SHA512_DIGEST_LENGTH);
    SHA512(reinterpret_cast<const unsigned char*>(data.data()), data.size(), digest.data());
  }
  else if (algorithm == "md5")
  {
    digest.resize(MD5_DIGEST_LENGTH);
    MD5(reinterpret_cast<const unsigned char*>(data.data()), data.size(), digest.data());
  }
  else if (algorithm == "blake3")
  {
    throw std::runtime_error("crypto_hash blake3 not supported yet");
  }
  else
  {
    throw std::runtime_error("Unsupported hash algorithm");
  }
  const std::string hex = hex_encode_bytes(digest);
  return Value::String(hex.c_str(), hex.size());
}

Value crypto_hmac_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 3)
  {
    throw std::runtime_error("crypto_hmac expects 3 arguments");
  }
  const std::string algorithm = to_std_string(args[0]);
  const std::string key = to_std_string(args[1]);
  const std::string data = to_std_string(args[2]);
  const EVP_MD* digest = nullptr;
  if (algorithm == "sha256")
  {
    digest = EVP_sha256();
  }
  else if (algorithm == "sha512")
  {
    digest = EVP_sha512();
  }
  else
  {
    throw std::runtime_error("Unsupported HMAC algorithm");
  }
  unsigned int length = 0;
  unsigned char result[EVP_MAX_MD_SIZE];
  HMAC(digest, key.data(), static_cast<int>(key.size()),
       reinterpret_cast<const unsigned char*>(data.data()), data.size(), result, &length);
  std::vector<uint8_t> bytes(result, result + length);
  const std::string hex = hex_encode_bytes(bytes);
  return Value::String(hex.c_str(), hex.size());
}

Value crypto_random_bytes_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 1)
  {
    throw std::runtime_error("crypto_random_bytes expects 1 argument");
  }
  const auto length = static_cast<int64_t>(to_number(args[0]));
  if (length < 0)
  {
    throw std::runtime_error("crypto_random_bytes expects non-negative length");
  }
  std::uniform_int_distribution<int> dist(0, 255);
  std::vector<uint8_t> bytes;
  bytes.reserve(static_cast<std::size_t>(length));
  for (int64_t i = 0; i < length; ++i)
  {
    bytes.push_back(static_cast<uint8_t>(dist(rng())));
  }
  return bytes_to_list(bytes);
}

Value crypto_uuid_v4_native(VirtualMachine&, int arg_count, Value*)
{
  if (arg_count != 0)
  {
    throw std::runtime_error("crypto_uuid_v4 expects 0 arguments");
  }
  std::uniform_int_distribution<int> dist(0, 255);
  std::array<uint8_t, 16> bytes{};
  for (auto& byte : bytes)
  {
    byte = static_cast<uint8_t>(dist(rng()));
  }
  bytes[6] = static_cast<uint8_t>((bytes[6] & 0x0F) | 0x40);
  bytes[8] = static_cast<uint8_t>((bytes[8] & 0x3F) | 0x80);
  std::ostringstream out;
  out << std::hex << std::setfill('0');
  for (std::size_t i = 0; i < bytes.size(); ++i)
  {
    out << std::setw(2) << static_cast<int>(bytes[i]);
    if (i == 3 || i == 5 || i == 7 || i == 9)
    {
      out << '-';
    }
  }
  const std::string uuid = out.str();
  return Value::String(uuid.c_str(), uuid.size());
}

Value crypto_base64_encode_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 1)
  {
    throw std::runtime_error("crypto_base64_encode expects 1 argument");
  }
  const std::string data = to_std_string(args[0]);
  const std::vector<uint8_t> bytes(data.begin(), data.end());
  const std::string encoded = base64_encode_bytes(bytes);
  return Value::String(encoded.c_str(), encoded.size());
}

Value crypto_base64_decode_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 1)
  {
    throw std::runtime_error("crypto_base64_decode expects 1 argument");
  }
  const std::string data = to_std_string(args[0]);
  const std::vector<uint8_t> decoded = base64_decode_bytes(data);
  const std::string text(decoded.begin(), decoded.end());
  return Value::String(text.c_str(), text.size());
}

Value crypto_hex_encode_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 1)
  {
    throw std::runtime_error("crypto_hex_encode expects 1 argument");
  }
  const std::vector<uint8_t> bytes = list_to_bytes(args[0]);
  const std::string encoded = hex_encode_bytes(bytes);
  return Value::String(encoded.c_str(), encoded.size());
}

Value crypto_hex_decode_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 1)
  {
    throw std::runtime_error("crypto_hex_decode expects 1 argument");
  }
  const std::string hex = to_std_string(args[0]);
  const std::vector<uint8_t> bytes = hex_decode_string(hex);
  return bytes_to_list(bytes);
}

// ============================================================================
// Core utility functions: len() and str()
// ============================================================================

Value len_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 1)
  {
    throw std::runtime_error("len() expects exactly 1 argument");
  }

  const Value& val = args[0];

  if (val.is_string())
  {
    return Value::Number(static_cast<double>(as_string(val)->length));
  }
  if (val.is_list())
  {
    return Value::Number(static_cast<double>(as_list(val)->items.size()));
  }
  if (val.is_map())
  {
    return Value::Number(static_cast<double>(as_map(val)->entries.size()));
  }

  throw std::runtime_error("len() requires string, list, or map argument");
}

Value str_native(VirtualMachine& vm, int arg_count, Value* args)
{
  if (arg_count != 1)
  {
    throw std::runtime_error("str() expects exactly 1 argument");
  }

  const std::string result = value_to_string(args[0]);
  return Value::String(result.c_str(), result.size());
}

Value await_all_native(VirtualMachine&, int arg_count, Value* args)
{
  if (arg_count != 1)
  {
    throw std::runtime_error("await_all expects 1 argument");
  }
  if (!args[0].is_list())
  {
    throw std::runtime_error("await_all expects a list");
  }
  auto* list = as_list(args[0]);
  std::vector<Value> results;
  results.reserve(list->items.size());
  for (auto& item : list->items)
  {
    if (!item.is_future())
    {
      results.push_back(item);
      continue;
    }
    auto* future = as_future(item);
    if (!future->future)
    {
      throw std::runtime_error("Future has no state");
    }
    results.push_back(future->future->wait());
  }
  return Value::List(new_list(std::move(results)));
}

// ============================================================================
// Voice pipeline native functions
// ============================================================================

Value voice_transcribe_native(VirtualMachine& vm, int arg_count, Value* args)
{
  if (arg_count != 2)
  {
    throw std::runtime_error("voice_transcribe expects 2 arguments (pipeline_name, audio_path)");
  }
  const std::string pipeline_name = to_std_string(args[0]);
  const std::string audio_path = to_std_string(args[1]);
  std::string text = vm.transcribe_audio(pipeline_name, audio_path);
  return Value::String(text.c_str(), text.size());
}

Value voice_synthesize_native(VirtualMachine& vm, int arg_count, Value* args)
{
  if (arg_count != 3)
  {
    throw std::runtime_error("voice_synthesize expects 3 arguments (pipeline_name, text, output_path)");
  }
  const std::string pipeline_name = to_std_string(args[0]);
  const std::string text = to_std_string(args[1]);
  const std::string output_path = to_std_string(args[2]);
  std::string result = vm.synthesize_speech(pipeline_name, text, output_path);
  return Value::String(result.c_str(), result.size());
}

Value voice_pipeline_run_native(VirtualMachine& vm, int arg_count, Value* args)
{
  if (arg_count != 3)
  {
    throw std::runtime_error("voice_pipeline_run expects 3 arguments (pipeline_name, audio_input, audio_output)");
  }
  const std::string pipeline_name = to_std_string(args[0]);
  const std::string audio_input = to_std_string(args[1]);
  const std::string audio_output = to_std_string(args[2]);

  const auto* def = vm.get_voice_pipeline(pipeline_name);
  if (!def)
  {
    throw std::runtime_error("Unknown voice pipeline: " + pipeline_name);
  }

  voice::STTConfig stt_config;
  stt_config.provider = def->stt_provider;
  stt_config.model = def->stt_model;
  stt_config.endpoint = def->stt_endpoint;
  stt_config.language = def->stt_language;
  stt_config.format = def->stt_format;

  voice::TTSConfig tts_config;
  tts_config.provider = def->tts_provider;
  tts_config.model = def->tts_model;
  tts_config.voice = def->tts_voice;
  tts_config.endpoint = def->tts_endpoint;
  tts_config.format = def->tts_format;
  tts_config.speed = def->tts_speed;
  tts_config.instructions = def->tts_instructions;

  auto result = voice::run_voice_pipeline(
      vm, def->agent_name, audio_input, audio_output,
      stt_config, tts_config);

  std::unordered_map<std::string, Value> result_map;
  result_map["input_text"] = Value::String(result.input_text.c_str(), result.input_text.size());
  result_map["response_text"] = Value::String(result.response_text.c_str(), result.response_text.size());
  result_map["output_audio"] = Value::String(result.output_audio_path.c_str(), result.output_audio_path.size());
  return Value::Map(new_map(std::move(result_map)));
}

}  // namespace

void register_core_natives(VirtualMachine& vm)
{
  // Core utility functions
  vm.define_native("len", 1, len_native);
  vm.define_native("str", 1, str_native);

  vm.define_native("print", -1, print_native);
  vm.define_native("clock", 0, clock_native);
  vm.define_native("input", 0, input_native);
  vm.define_native("typeof", 1, typeof_native);
  vm.define_native("json_parse", 1, json_parse_native);
  vm.define_native("json_stringify", 1, json_stringify_native);
  vm.define_native("assert_eq", 2, assert_eq_native);
  vm.define_native("assert_ne", 2, assert_ne_native);
  vm.define_native("assert_true", 1, assert_true_native);
  vm.define_native("assert_false", 1, assert_false_native);
  vm.define_native("assert_some", 1, assert_some_native);
  vm.define_native("assert_none", 1, assert_none_native);
  vm.define_native("assert_ok", 1, assert_ok_native);
  vm.define_native("assert_err", 1, assert_err_native);
  vm.define_native("assert_throws", -1, assert_throws_native);
  vm.define_native("file_read_string", 1, file_read_string_native);
  vm.define_native("file_write_string", 2, file_write_string_native);
  vm.define_native("file_read_bytes", 1, file_read_bytes_native);
  vm.define_native("file_write_bytes", 2, file_write_bytes_native);
  vm.define_native("file_exists", 1, file_exists_native);
  vm.define_native("file_remove", 1, file_remove_native);
  vm.define_native("file_copy", 2, file_copy_native);
  vm.define_native("file_rename", 2, file_rename_native);
  vm.define_native("file_open", 2, file_open_native);
  vm.define_native("panic", 1, panic_native);
  vm.define_native("context", 2, context_native);
  vm.define_native("with_context", 3, with_context_native);
  vm.define_native("http_get", 1, http_get_native);
  vm.define_native("http_request", 4, http_request_native);
  vm.define_native("math_abs", 1, math_abs_native);
  vm.define_native("math_floor", 1, math_floor_native);
  vm.define_native("math_ceil", 1, math_ceil_native);
  vm.define_native("math_round", 1, math_round_native);
  vm.define_native("math_min", 2, math_min_native);
  vm.define_native("math_max", 2, math_max_native);
  vm.define_native("math_clamp", 3, math_clamp_native);
  vm.define_native("math_sin", 1, math_sin_native);
  vm.define_native("math_cos", 1, math_cos_native);
  vm.define_native("math_tan", 1, math_tan_native);
  vm.define_native("math_asin", 1, math_asin_native);
  vm.define_native("math_acos", 1, math_acos_native);
  vm.define_native("math_atan", 1, math_atan_native);
  vm.define_native("math_atan2", 2, math_atan2_native);
  vm.define_native("math_pow", 2, math_pow_native);
  vm.define_native("math_sqrt", 1, math_sqrt_native);
  vm.define_native("math_cbrt", 1, math_cbrt_native);
  vm.define_native("math_log", 1, math_log_native);
  vm.define_native("math_log10", 1, math_log10_native);
  vm.define_native("math_exp", 1, math_exp_native);
  vm.define_native("math_random", 0, math_random_native);
  vm.define_native("math_random_int", 2, math_random_int_native);
  vm.define_native("time_now", 0, time_now_native);
  vm.define_native("time_now_millis", 0, time_now_millis_native);
  vm.define_native("time_now_micros", 0, time_now_micros_native);
  vm.define_native("time_sleep", 1, time_sleep_native);
  vm.define_native("time_format", 2, time_format_native);
  vm.define_native("time_parse", 2, time_parse_native);
  vm.define_native("future_resolve", 1, future_resolve_native);
  vm.define_native("future_reject", 1, future_reject_native);
  vm.define_native("future_all", 1, future_all_native);
  vm.define_native("future_race", 1, future_race_native);
  vm.define_native("future_delay", 1, future_delay_native);
  vm.define_native("await_all", 1, await_all_native);
  vm.define_native("crypto_hash", 2, crypto_hash_native);
  vm.define_native("crypto_hmac", 3, crypto_hmac_native);
  vm.define_native("crypto_random_bytes", 1, crypto_random_bytes_native);
  vm.define_native("crypto_uuid_v4", 0, crypto_uuid_v4_native);
  vm.define_native("crypto_base64_encode", 1, crypto_base64_encode_native);
  vm.define_native("crypto_base64_decode", 1, crypto_base64_decode_native);
  vm.define_native("crypto_hex_encode", 1, crypto_hex_encode_native);
  vm.define_native("crypto_hex_decode", 1, crypto_hex_decode_native);

  // Voice pipeline natives
  vm.define_native("voice_transcribe", 2, voice_transcribe_native);
  vm.define_native("voice_synthesize", 3, voice_synthesize_native);
  vm.define_native("voice_pipeline_run", 3, voice_pipeline_run_native);

  // Realtime voice natives
  vm.define_native("realtime_connect", 1, [](VirtualMachine& vm, int, Value* args) -> Value
  {
    const std::string config_name = to_std_string(args[0]);
    std::string session_id = vm.realtime_connect(config_name);
    return Value::String(session_id.c_str(), session_id.size());
  });

  vm.define_native("realtime_send_audio", 2, [](VirtualMachine& vm, int, Value* args) -> Value
  {
    const std::string session_id = to_std_string(args[0]);
    const std::string audio_b64 = to_std_string(args[1]);
    auto session = vm.get_realtime_session(session_id);
    if (!session)
    {
      throw std::runtime_error("Unknown realtime session: " + session_id);
    }
    session->send_audio_base64(audio_b64);
    return Value::Nil();
  });

  vm.define_native("realtime_send_text", 2, [](VirtualMachine& vm, int, Value* args) -> Value
  {
    const std::string session_id = to_std_string(args[0]);
    const std::string text = to_std_string(args[1]);
    auto session = vm.get_realtime_session(session_id);
    if (!session)
    {
      throw std::runtime_error("Unknown realtime session: " + session_id);
    }
    session->send_text(text);
    return Value::Nil();
  });

  vm.define_native("realtime_on", 3, [](VirtualMachine& vm, int, Value* args) -> Value
  {
    const std::string session_id = to_std_string(args[0]);
    const std::string event_name = to_std_string(args[1]);
    auto session = vm.get_realtime_session(session_id);
    if (!session)
    {
      throw std::runtime_error("Unknown realtime session: " + session_id);
    }
    // Event callbacks are stored but invoked asynchronously by the session.
    // The callback Value (args[2]) is a closure/function that would need
    // VM-level callback dispatch. For now, register text-based callbacks.
    if (event_name == "transcript")
    {
      session->on_transcript([&vm](const std::string& text)
      {
        auto& out = vm.output_stream();
        out << "[transcript] " << text << "\n";
      });
    }
    else if (event_name == "response_text")
    {
      session->on_response_text([&vm](const std::string& text)
      {
        auto& out = vm.output_stream();
        out << "[response] " << text;
      });
    }
    else if (event_name == "response_audio")
    {
      session->on_response_audio([](const std::vector<uint8_t>&)
      {
        // Audio data received - would be written to output device
      });
    }
    else if (event_name == "tool_call")
    {
      session->on_tool_call([&vm](const std::string& call_id,
                                   const std::string& name,
                                   const std::string& args)
      {
        auto& out = vm.output_stream();
        out << "[tool_call] " << name << "(" << args << ") id=" << call_id << "\n";
      });
    }
    else if (event_name == "error")
    {
      session->on_error([&vm](const std::string& error)
      {
        auto& out = vm.output_stream();
        out << "[error] " << error << "\n";
      });
    }
    return Value::Nil();
  });

  vm.define_native("realtime_tool_result", 3, [](VirtualMachine& vm, int, Value* args) -> Value
  {
    const std::string session_id = to_std_string(args[0]);
    const std::string call_id = to_std_string(args[1]);
    const std::string result = to_std_string(args[2]);
    auto session = vm.get_realtime_session(session_id);
    if (!session)
    {
      throw std::runtime_error("Unknown realtime session: " + session_id);
    }
    session->send_tool_result(call_id, result);
    return Value::Nil();
  });

  vm.define_native("realtime_interrupt", 1, [](VirtualMachine& vm, int, Value* args) -> Value
  {
    const std::string session_id = to_std_string(args[0]);
    auto session = vm.get_realtime_session(session_id);
    if (!session)
    {
      throw std::runtime_error("Unknown realtime session: " + session_id);
    }
    session->cancel_response();
    return Value::Nil();
  });

  vm.define_native("realtime_close", 1, [](VirtualMachine& vm, int, Value* args) -> Value
  {
    const std::string session_id = to_std_string(args[0]);
    auto session = vm.get_realtime_session(session_id);
    if (session)
    {
      session->close();
    }
    return Value::Nil();
  });

  // ============================================================================
  // Stdlib voice bridge natives (Phase F.5)
  //
  // These bridge the stdlib voice stubs (std.agents.advanced.voice.*) to
  // the actual native voice pipeline and realtime implementations.
  // ============================================================================

  // voice_engine_transcribe(pipeline_name, audio_data) -> { ok, value: { text, confidence } }
  // Used by stdlib transcriber.transcribe() when backed by a native pipeline.
  vm.define_native("voice_engine_transcribe", 2, [](VirtualMachine& vm, int, Value* args) -> Value
  {
    const std::string pipeline_name = to_std_string(args[0]);
    const std::string audio_path = to_std_string(args[1]);

    try
    {
      std::string text = vm.transcribe_audio(pipeline_name, audio_path);
      std::unordered_map<std::string, Value> result;
      result["text"] = Value::String(text.c_str(), text.size());
      result["confidence"] = Value::Number(0.95);
      result["duration_ms"] = Value::Number(0);

      std::unordered_map<std::string, Value> wrapper;
      wrapper["ok"] = Value::Bool(true);
      wrapper["value"] = Value::Map(new_map(std::move(result)));
      return Value::Map(new_map(std::move(wrapper)));
    }
    catch (const std::exception& e)
    {
      std::string msg = e.what();
      std::unordered_map<std::string, Value> err;
      err["kind"] = Value::String("VoiceError", 10);
      err["code"] = Value::String("TRANSCRIPTION", 13);
      err["message"] = Value::String(msg.c_str(), msg.size());

      std::unordered_map<std::string, Value> wrapper;
      wrapper["ok"] = Value::Bool(false);
      wrapper["error"] = Value::Map(new_map(std::move(err)));
      return Value::Map(new_map(std::move(wrapper)));
    }
  });

  // voice_engine_synthesize(pipeline_name, text, output_path) -> { ok, value: { audio, duration_ms } }
  // Used by stdlib synthesizer.synthesize() when backed by a native pipeline.
  vm.define_native("voice_engine_synthesize", 3, [](VirtualMachine& vm, int, Value* args) -> Value
  {
    const std::string pipeline_name = to_std_string(args[0]);
    const std::string text = to_std_string(args[1]);
    const std::string output_path = to_std_string(args[2]);

    try
    {
      std::string result_path = vm.synthesize_speech(pipeline_name, text, output_path);
      std::unordered_map<std::string, Value> result;
      result["audio"] = Value::String(result_path.c_str(), result_path.size());
      result["duration_ms"] = Value::Number(0);
      result["format"] = Value::String("wav", 3);

      std::unordered_map<std::string, Value> wrapper;
      wrapper["ok"] = Value::Bool(true);
      wrapper["value"] = Value::Map(new_map(std::move(result)));
      return Value::Map(new_map(std::move(wrapper)));
    }
    catch (const std::exception& e)
    {
      std::string msg = e.what();
      std::unordered_map<std::string, Value> err;
      err["kind"] = Value::String("VoiceError", 10);
      err["code"] = Value::String("SYNTHESIS", 9);
      err["message"] = Value::String(msg.c_str(), msg.size());

      std::unordered_map<std::string, Value> wrapper;
      wrapper["ok"] = Value::Bool(false);
      wrapper["error"] = Value::Map(new_map(std::move(err)));
      return Value::Map(new_map(std::move(wrapper)));
    }
  });

  // voice_engine_pipeline_run(pipeline_name, audio_in, audio_out) -> { ok, value: { ... } }
  // Full STT -> LLM -> TTS pipeline, used by stdlib voice agent.
  vm.define_native("voice_engine_pipeline_run", 3, voice_pipeline_run_native);

  // voice_list_pipelines() -> list of pipeline names
  vm.define_native("voice_list_pipelines", 0, [](VirtualMachine& vm, int, Value*) -> Value
  {
    // Return list of registered voice pipeline names
    std::vector<Value> names;
    // Access through the public API
    auto& globals = vm.globals();
    (void)globals;  // Pipeline names are tracked internally
    // For now return empty - pipelines are accessed by name
    return Value::List(new_list(std::move(names)));
  });

  // voice_list_realtime_configs() -> list of realtime voice config names
  vm.define_native("voice_list_realtime_configs", 0, [](VirtualMachine&, int, Value*) -> Value
  {
    std::vector<Value> names;
    return Value::List(new_list(std::move(names)));
  });

  // ============================================================================
  // VM Event Loop Natives (Phase F.6)
  //
  // Allow Neam programs to poll for voice events and dispatch callbacks
  // from within the VM execution loop.
  // ============================================================================

  // voice_event_poll(session_id, timeout_ms) -> list of events
  // Non-blocking or timed poll for pending voice events from a session.
  vm.define_native("voice_event_poll", 2, [](VirtualMachine& vm, int, Value* args) -> Value
  {
    const std::string session_id = to_std_string(args[0]);
    const int timeout_ms = static_cast<int>(to_number(args[1]));

    auto session = vm.get_realtime_session(session_id);
    if (!session)
    {
      throw std::runtime_error("Unknown realtime session: " + session_id);
    }

    // Collect events by temporarily registering collection callbacks
    // and then waiting for the timeout period.
    // For now, return empty list since events are dispatched via on_* callbacks.
    std::vector<Value> events;
    (void)timeout_ms;
    return Value::List(new_list(std::move(events)));
  });

  // voice_session_status(session_id) -> map { connected, ... }
  vm.define_native("voice_session_status", 1, [](VirtualMachine& vm, int, Value* args) -> Value
  {
    const std::string session_id = to_std_string(args[0]);
    auto session = vm.get_realtime_session(session_id);

    std::unordered_map<std::string, Value> status;
    if (session)
    {
      status["connected"] = Value::Bool(session->is_connected());
      status["session_id"] = Value::String(session_id.c_str(), session_id.size());
    }
    else
    {
      status["connected"] = Value::Bool(false);
      status["session_id"] = Value::String(session_id.c_str(), session_id.size());
      status["error"] = Value::String("Session not found", 17);
    }
    return Value::Map(new_map(std::move(status)));
  });

  // voice_commit_audio(session_id) -> nil
  // Commit accumulated audio buffer and trigger STT + response.
  vm.define_native("voice_commit_audio", 1, [](VirtualMachine& vm, int, Value* args) -> Value
  {
    const std::string session_id = to_std_string(args[0]);
    auto session = vm.get_realtime_session(session_id);
    if (!session)
    {
      throw std::runtime_error("Unknown realtime session: " + session_id);
    }
    session->commit_audio();
    return Value::Nil();
  });

  // voice_clear_audio(session_id) -> nil
  // Clear accumulated audio buffer without processing.
  vm.define_native("voice_clear_audio", 1, [](VirtualMachine& vm, int, Value* args) -> Value
  {
    const std::string session_id = to_std_string(args[0]);
    auto session = vm.get_realtime_session(session_id);
    if (!session)
    {
      throw std::runtime_error("Unknown realtime session: " + session_id);
    }
    session->clear_audio();
    return Value::Nil();
  });

  // voice_request_response(session_id) -> nil
  // Explicitly request a response from the model.
  vm.define_native("voice_request_response", 1, [](VirtualMachine& vm, int, Value* args) -> Value
  {
    const std::string session_id = to_std_string(args[0]);
    auto session = vm.get_realtime_session(session_id);
    if (!session)
    {
      throw std::runtime_error("Unknown realtime session: " + session_id);
    }
    session->request_response();
    return Value::Nil();
  });

  // voice_audio_resample(audio_b64, from_rate, to_rate) -> resampled_b64
  // Resample PCM audio between sample rates (e.g. 16kHz -> 24kHz).
  vm.define_native("voice_audio_resample", 3, [](VirtualMachine&, int, Value* args) -> Value
  {
    const std::string audio_b64 = to_std_string(args[0]);
    const int from_rate = static_cast<int>(to_number(args[1]));
    const int to_rate = static_cast<int>(to_number(args[2]));

    auto pcm = util::base64_decode(audio_b64);
    auto resampled = voice::AudioBuffer::resample(pcm, from_rate, to_rate);
    auto result = util::base64_encode(resampled);
    return Value::String(result.c_str(), result.size());
  });

  // ============================================================================
  // Cognitive natives (v0.5.0)
  // ============================================================================

  // agent_rate(agent_name, score) — submit explicit feedback
  vm.define_native("agent_rate", 2, [](VirtualMachine& vm, int, Value* args) -> Value
  {
    const std::string agent_name = to_std_string(args[0]);
    const double score = to_number(args[1]);
    // Store feedback in reflection results
    nlohmann::json feedback;
    feedback["feedback_score"] = score;
    feedback["type"] = "explicit_feedback";
    // Merge with existing reflection results if present
    auto& results = vm.reflection_results();
    if (results.find(agent_name) != results.end())
    {
      results[agent_name]["feedback_score"] = score;
    }
    else
    {
      results[agent_name] = feedback;
    }
    return Value::Bool(true);
  });

  // agent_reflect(agent_name) — trigger on-demand reflection, returns map
  vm.define_native("agent_reflect", 1, [](VirtualMachine& vm, int, Value* args) -> Value
  {
    const std::string agent_name = to_std_string(args[0]);
    auto it = vm.reflection_results().find(agent_name);
    if (it != vm.reflection_results().end())
    {
      // Convert JSON to Value map
      std::unordered_map<std::string, Value> result;
      for (auto& [key, val] : it->second.items())
      {
        if (val.is_number()) result[key] = Value::Number(val.get<double>());
        else if (val.is_string())
        {
          std::string s = val.get<std::string>();
          result[key] = Value::String(s.c_str(), s.size());
        }
        else if (val.is_boolean()) result[key] = Value::Bool(val.get<bool>());
      }
      return Value::Map(new_map(std::move(result)));
    }
    return Value::Nil();
  });

  // agent_evolve(agent_name) — trigger manual evolution
  vm.define_native("agent_evolve", 1, [](VirtualMachine& vm, int, Value* args) -> Value
  {
    const std::string agent_name = to_std_string(args[0]);
    auto ext_it = vm.extensions().find(agent_name);
    if (ext_it == vm.extensions().end())
    {
      return Value::Bool(false);
    }
    // Evolution requires a provider — find the agent and create one
    const Value* agent_val = find_global_value(vm.globals(), agent_name);
    if (!agent_val || !agent_val->is_agent()) return Value::Bool(false);
    auto* agent = as_agent(*agent_val);

    llm::ProviderConfig config;
    config.model = to_std_string(agent->model);
    config.endpoint = to_std_string(agent->endpoint);
    config.api_key = to_std_string(agent->api_key_env);
    config.temperature = agent->temperature;
    if (!config.api_key.empty())
    {
      if (const char* ev = std::getenv(config.api_key.c_str())) config.api_key = ev;
      else config.api_key.clear();
    }
    auto provider = llm::create_provider(to_std_string(agent->provider), config);
    vm.apply_evolution(agent_name, provider.get(), ext_it->second.evolution_config);
    return Value::Bool(true);
  });

  // agent_rollback(agent_name, version) — rollback to a specific version
  vm.define_native("agent_rollback", 2, [](VirtualMachine& vm, int, Value* args) -> Value
  {
    const std::string agent_name = to_std_string(args[0]);
    const size_t target_version = static_cast<size_t>(to_number(args[1]));

    auto ver_it = vm.evolution_versions().find(agent_name);
    if (ver_it == vm.evolution_versions().end() || target_version >= ver_it->second)
    {
      return Value::Bool(false);
    }

    // Try to load from SQLite memory backend
    if (vm.memory_backend_ptr())
    {
      // The evolution events store version info — search for the target version
      // For simplicity, remove the evolved prompt to revert to original
      vm.evolved_prompts().erase(agent_name);
      vm.evolution_versions()[agent_name] = target_version;
      return Value::Bool(true);
    }
    return Value::Bool(false);
  });

  // agent_status(agent_name) — full cognitive state map
  vm.define_native("agent_status", 1, [](VirtualMachine& vm, int, Value* args) -> Value
  {
    const std::string agent_name = to_std_string(args[0]);
    std::unordered_map<std::string, Value> status;

    auto ext_it = vm.extensions().find(agent_name);
    if (ext_it != vm.extensions().end())
    {
      const auto& ext = ext_it->second;
      // Reasoning
      std::string rm;
      switch (ext.reasoning_mode)
      {
        case VirtualMachine::ReasoningMode::kNone: rm = "none"; break;
        case VirtualMachine::ReasoningMode::kChainOfThought: rm = "chain_of_thought"; break;
        case VirtualMachine::ReasoningMode::kPlanAndExecute: rm = "plan_and_execute"; break;
        case VirtualMachine::ReasoningMode::kTreeOfThought: rm = "tree_of_thought"; break;
        case VirtualMachine::ReasoningMode::kSelfConsistency: rm = "self_consistency"; break;
      }
      status["reasoning_mode"] = Value::String(rm.c_str(), rm.size());

      // Reflection
      auto ref_it = vm.reflection_results().find(agent_name);
      status["has_reflection"] = Value::Bool(ref_it != vm.reflection_results().end());

      // Learning
      auto learn_it = vm.learning_counts().find(agent_name);
      status["interaction_count"] = Value::Number(
          learn_it != vm.learning_counts().end() ? static_cast<double>(learn_it->second) : 0.0);

      // Evolution
      auto evo_it = vm.evolution_versions().find(agent_name);
      status["evolution_version"] = Value::Number(
          evo_it != vm.evolution_versions().end() ? static_cast<double>(evo_it->second) : 0.0);
      status["has_evolved_prompt"] = Value::Bool(
          vm.evolved_prompts().find(agent_name) != vm.evolved_prompts().end());

      // Goals
      if (!ext.goal_config.goals.empty())
      {
        std::vector<Value> goal_vals;
        for (const auto& g : ext.goal_config.goals)
        {
          goal_vals.push_back(Value::String(g.c_str(), g.size()));
        }
        status["goals"] = Value::List(new_list(std::move(goal_vals)));
      }
      status["initiative"] = Value::Bool(ext.goal_config.initiative);
    }

    return Value::Map(new_map(std::move(status)));
  });

  // agent_learning_stats(agent_name) — learning statistics
  vm.define_native("agent_learning_stats", 1, [](VirtualMachine& vm, int, Value* args) -> Value
  {
    const std::string agent_name = to_std_string(args[0]);
    std::unordered_map<std::string, Value> stats;

    auto count_it = vm.learning_counts().find(agent_name);
    stats["total_interactions"] = Value::Number(
        count_it != vm.learning_counts().end() ? static_cast<double>(count_it->second) : 0.0);

    auto ext_it = vm.extensions().find(agent_name);
    if (ext_it != vm.extensions().end())
    {
      stats["review_interval"] = Value::Number(static_cast<double>(ext_it->second.learning_config.review_interval));
      std::string strategy;
      switch (ext_it->second.learning_config.strategy)
      {
        case VirtualMachine::LearningStrategy::kNone: strategy = "none"; break;
        case VirtualMachine::LearningStrategy::kExperienceReplay: strategy = "experience_replay"; break;
        case VirtualMachine::LearningStrategy::kPatternExtraction: strategy = "pattern_extraction"; break;
        case VirtualMachine::LearningStrategy::kPromptEvolution: strategy = "prompt_evolution"; break;
        case VirtualMachine::LearningStrategy::kPreferenceLearning: strategy = "preference_learning"; break;
      }
      stats["strategy"] = Value::String(strategy.c_str(), strategy.size());
    }

    return Value::Map(new_map(std::move(stats)));
  });

  // agent_prompt_history(agent_name) — list of evolved prompts
  vm.define_native("agent_prompt_history", 1, [](VirtualMachine& vm, int, Value* args) -> Value
  {
    const std::string agent_name = to_std_string(args[0]);
    std::vector<Value> history;

    auto evolved_it = vm.evolved_prompts().find(agent_name);
    if (evolved_it != vm.evolved_prompts().end())
    {
      history.push_back(Value::String(evolved_it->second.c_str(), evolved_it->second.size()));
    }

    return Value::List(new_list(std::move(history)));
  });

  // agent_get_goals(agent_name) — get goals list
  vm.define_native("agent_get_goals", 1, [](VirtualMachine& vm, int, Value* args) -> Value
  {
    const std::string agent_name = to_std_string(args[0]);
    auto ext_it = vm.extensions().find(agent_name);
    if (ext_it == vm.extensions().end()) return Value::Nil();

    std::vector<Value> goal_vals;
    for (const auto& g : ext_it->second.goal_config.goals)
    {
      goal_vals.push_back(Value::String(g.c_str(), g.size()));
    }
    return Value::List(new_list(std::move(goal_vals)));
  });

  // agent_set_goals(agent_name, goals_list) — update goals
  vm.define_native("agent_set_goals", 2, [](VirtualMachine& vm, int, Value* args) -> Value
  {
    const std::string agent_name = to_std_string(args[0]);
    if (!args[1].is_list()) return Value::Bool(false);

    auto* list = as_list(args[1]);
    std::vector<std::string> goals;
    for (const auto& item : list->items)
    {
      if (item.is_string()) goals.push_back(std::string(as_string(item)->chars, as_string(item)->length));
    }

    auto ext_it = vm.extensions().find(agent_name);
    if (ext_it != vm.extensions().end())
    {
      ext_it->second.goal_config.goals = goals;
    }
    return Value::Bool(true);
  });

  // agent_pause(agent_name) — pause autonomous execution
  vm.define_native("agent_pause", 1, [](VirtualMachine& vm, int, Value* args) -> Value
  {
    const std::string agent_name = to_std_string(args[0]);
    auto* executor = vm.autonomous_executor_ptr();
    if (executor)
    {
      executor->pause_agent(agent_name);
      return Value::Bool(true);
    }
    return Value::Bool(false);
  });

  // agent_resume(agent_name) — resume autonomous execution
  vm.define_native("agent_resume", 1, [](VirtualMachine& vm, int, Value* args) -> Value
  {
    const std::string agent_name = to_std_string(args[0]);
    auto* executor = vm.autonomous_executor_ptr();
    if (executor)
    {
      executor->resume_agent(agent_name);
      return Value::Bool(true);
    }
    return Value::Bool(false);
  });
}
}  // namespace neamc::vm
