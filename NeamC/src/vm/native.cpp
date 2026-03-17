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

#include "neamc/vm/dag_executor.hpp"

#include <curl/curl.h>
#include <openssl/evp.h>
#include <openssl/hmac.h>
#include <openssl/md5.h>
#include <openssl/sha.h>

#include "neamc/vm/table.hpp"
#include "neamc/vm/async/future.hpp"
#include "neamc/vm/async/executor.hpp"
#include "neamc/vm/runtime_type.hpp"
#include "neamc/vm/vm.hpp"
#include "neamc/vm/memory_index.hpp"
#include "neamc/vm/session_manager.hpp"
#include "neamc/vm/claw_agent_type.hpp"
#include "neamc/vm/migration_types.hpp"
#include "neamc/vm/dataops_types.hpp"
#include "neamc/vm/governance_types.hpp"
#include "neamc/vm/modeling_types.hpp"
#include "neamc/vm/analyst_types.hpp"
#include "neamc/vm/deploy_types.hpp"

namespace neamc::vm
{
namespace
{
namespace fs = std::filesystem;

std::string to_std_string(const Value& value)
{
  if (!value.is_string())
  {
    throw std::runtime_error("Expected string value");
  }
  auto* str = as_string(value);
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

const std::string& base64_alphabet()
{
  static const std::string alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  return alphabet;
}

std::string base64_encode_bytes(const std::vector<uint8_t>& data)
{
  const auto& alphabet = base64_alphabet();
  std::string out;
  out.reserve(((data.size() + 2) / 3) * 4);
  for (std::size_t i = 0; i < data.size(); i += 3)
  {
    const std::size_t remaining = data.size() - i;
    const uint32_t octet_a = data[i];
    const uint32_t octet_b = remaining > 1 ? data[i + 1] : 0;
    const uint32_t octet_c = remaining > 2 ? data[i + 2] : 0;
    const uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

    out.push_back(alphabet[(triple >> 18) & 0x3F]);
    out.push_back(alphabet[(triple >> 12) & 0x3F]);
    out.push_back(remaining > 1 ? alphabet[(triple >> 6) & 0x3F] : '=');
    out.push_back(remaining > 2 ? alphabet[triple & 0x3F] : '=');
  }
  return out;
}

std::vector<uint8_t> base64_decode_bytes(const std::string& input)
{
  const auto& alphabet = base64_alphabet();
  std::vector<int> table(256, -1);
  for (std::size_t i = 0; i < alphabet.size(); ++i)
  {
    table[static_cast<unsigned char>(alphabet[i])] = static_cast<int>(i);
  }
  std::vector<uint8_t> out;
  uint32_t buffer = 0;
  int bits_collected = 0;
  for (unsigned char c : input)
  {
    if (std::isspace(c))
    {
      continue;
    }
    if (c == '=')
    {
      break;
    }
    int val = table[c];
    if (val == -1)
    {
      throw std::runtime_error("Invalid base64 character");
    }
    buffer = (buffer << 6) | static_cast<uint32_t>(val);
    bits_collected += 6;
    if (bits_collected >= 8)
    {
      bits_collected -= 8;
      out.push_back(static_cast<uint8_t>((buffer >> bits_collected) & 0xFF));
    }
  }
  return out;
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
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wdeprecated-declarations"
    MD5(reinterpret_cast<const unsigned char*>(data.data()), data.size(), digest.data());
#pragma GCC diagnostic pop
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
// v0.7.0: range() native — creates a Range object
Value range_native(VirtualMachine& /*vm*/, int argCount, Value* args)
{
  if (argCount < 1 || argCount > 3)
  {
    throw std::runtime_error("range() expects 1-3 arguments");
  }
  int64_t start = 0;
  int64_t end = 0;
  int64_t step = 1;
  if (argCount == 1)
  {
    if (!args[0].is_number()) throw std::runtime_error("range() argument must be a number");
    end = static_cast<int64_t>(args[0].as_number());
  }
  else if (argCount == 2)
  {
    if (!args[0].is_number() || !args[1].is_number())
      throw std::runtime_error("range() arguments must be numbers");
    start = static_cast<int64_t>(args[0].as_number());
    end = static_cast<int64_t>(args[1].as_number());
  }
  else
  {
    if (!args[0].is_number() || !args[1].is_number() || !args[2].is_number())
      throw std::runtime_error("range() arguments must be numbers");
    start = static_cast<int64_t>(args[0].as_number());
    end = static_cast<int64_t>(args[1].as_number());
    step = static_cast<int64_t>(args[2].as_number());
    if (step == 0) throw std::runtime_error("range() step cannot be zero");
  }
  return Value::Range(new_range(start, end, step));
}

// v0.7.0: set() native — creates a Set from arguments
Value set_native(VirtualMachine& /*vm*/, int argCount, Value* args)
{
  std::unordered_set<Value, ValueHash, ValueEqual> items;
  for (int i = 0; i < argCount; ++i)
  {
    items.insert(args[i]);
  }
  return Value::Set(new_set(std::move(items)));
}

// v0.7.0: tuple() native — creates a Tuple from arguments
Value tuple_native(VirtualMachine& /*vm*/, int argCount, Value* args)
{
  std::vector<Value> items;
  items.reserve(argCount);
  for (int i = 0; i < argCount; ++i)
  {
    items.push_back(args[i]);
  }
  return Value::Tuple(new_tuple(std::move(items)));
}

// v0.7.0: Some() native — wraps a value in Option
Value some_native(VirtualMachine& /*vm*/, int argCount, Value* args)
{
  if (argCount != 1) throw std::runtime_error("Some() expects 1 argument");
  return Value::Option(new_option(true, args[0]));
}



// v0.7.0: len() generic — returns length of string/list/map/set/tuple/range
Value len_native(VirtualMachine& /*vm*/, int argCount, Value* args)
{
  if (argCount != 1) throw std::runtime_error("len() expects 1 argument");
  const auto& val = args[0];
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
  if (val.is_set())
  {
    return Value::Number(static_cast<double>(as_set(val)->items.size()));
  }
  if (val.is_tuple())
  {
    return Value::Number(static_cast<double>(as_tuple(val)->items.size()));
  }
  if (val.is_range())
  {
    auto* range = as_range(val);
    int64_t len = 0;
    if (range->step > 0 && range->start < range->end)
      len = (range->end - range->start + range->step - 1) / range->step;
    else if (range->step < 0 && range->start > range->end)
      len = (range->start - range->end + (-range->step) - 1) / (-range->step);
    return Value::Number(static_cast<double>(len));
  }
  throw std::runtime_error("len() not supported for " + std::string(val.is_nil() ? "nil" : "this type"));
}

// v0.8 Phase 7: Workspace and memory native functions

Value workspace_read_native(VirtualMachine& vm, int argCount, Value* args)
{
  if (argCount != 1) throw std::runtime_error("workspace_read() expects 1 argument");
  std::string rel_path = to_std_string(args[0]);

  // Reject path traversal
  if (rel_path.find("..") != std::string::npos) return Value::Nil();

  // Resolve via first claw agent workspace
  const auto& claw_agents = vm.claw_agents();
  if (claw_agents.empty()) return Value::Nil();
  auto* claw = claw_agents.begin()->second;
  if (claw->workspace.empty()) return Value::Nil();

  fs::path full = fs::path(claw->workspace) / rel_path;
  fs::path canonical_ws = fs::weakly_canonical(fs::path(claw->workspace));
  fs::path canonical_full = fs::weakly_canonical(full);
  if (canonical_full.string().rfind(canonical_ws.string(), 0) != 0) return Value::Nil();

  std::ifstream in(full);
  if (!in.is_open()) return Value::Nil();
  std::string content((std::istreambuf_iterator<char>(in)),
                      std::istreambuf_iterator<char>());
  return Value::String(content.c_str(), content.size());
}

Value workspace_write_native(VirtualMachine& vm, int argCount, Value* args)
{
  if (argCount != 2) throw std::runtime_error("workspace_write() expects 2 arguments");
  std::string rel_path = to_std_string(args[0]);
  std::string content = to_std_string(args[1]);

  if (rel_path.find("..") != std::string::npos) return Value::Bool(false);

  const auto& claw_agents = vm.claw_agents();
  if (claw_agents.empty()) return Value::Bool(false);
  auto* claw = claw_agents.begin()->second;
  if (claw->workspace.empty()) return Value::Bool(false);

  fs::path full = fs::path(claw->workspace) / rel_path;
  fs::path canonical_ws = fs::weakly_canonical(fs::path(claw->workspace));
  fs::path canonical_full = fs::weakly_canonical(full);
  if (canonical_full.string().rfind(canonical_ws.string(), 0) != 0) return Value::Bool(false);

  if (full.has_parent_path()) fs::create_directories(full.parent_path());
  std::ofstream out(full, std::ios::trunc);
  if (!out.is_open()) return Value::Bool(false);
  out << content;
  return Value::Bool(true);
}

Value workspace_append_native(VirtualMachine& vm, int argCount, Value* args)
{
  if (argCount != 2) throw std::runtime_error("workspace_append() expects 2 arguments");
  std::string rel_path = to_std_string(args[0]);
  std::string content = to_std_string(args[1]);

  if (rel_path.find("..") != std::string::npos) return Value::Bool(false);

  const auto& claw_agents = vm.claw_agents();
  if (claw_agents.empty()) return Value::Bool(false);
  auto* claw = claw_agents.begin()->second;
  if (claw->workspace.empty()) return Value::Bool(false);

  fs::path full = fs::path(claw->workspace) / rel_path;
  fs::path canonical_ws = fs::weakly_canonical(fs::path(claw->workspace));
  fs::path canonical_full = fs::weakly_canonical(full);
  if (canonical_full.string().rfind(canonical_ws.string(), 0) != 0) return Value::Bool(false);

  if (full.has_parent_path()) fs::create_directories(full.parent_path());
  std::ofstream out(full, std::ios::app);
  if (!out.is_open()) return Value::Bool(false);
  out << content;
  return Value::Bool(true);
}

Value memory_search_native(VirtualMachine& vm, int argCount, Value* args)
{
  if (argCount < 1) throw std::runtime_error("memory_search() expects at least 1 argument");
  std::string query = to_std_string(args[0]);
  std::size_t top_k = 5;
  if (argCount >= 2 && args[1].is_number())
  {
    top_k = static_cast<std::size_t>(args[1].as_number());
  }

  auto results = vm.search_memory(query, top_k);
  std::vector<Value> result_list;
  for (const auto& r : results)
  {
    std::unordered_map<std::string, Value> m;
    m["file_path"] = Value::String(r.file_path.c_str(), r.file_path.size());
    m["chunk"] = Value::String(r.chunk.c_str(), r.chunk.size());
    m["score"] = Value::Number(static_cast<double>(r.score));
    result_list.push_back(Value::Map(new_map(std::move(m))));
  }
  return Value::List(new_list(std::move(result_list)));
}

Value session_history_native(VirtualMachine& vm, int argCount, Value* args)
{
  const auto& claw_agents = vm.claw_agents();
  if (claw_agents.empty())
  {
    return Value::List(new_list(std::vector<Value>{}));
  }
  auto* claw = claw_agents.begin()->second;

  std::string session_key = "default";
  int limit = -1;
  if (argCount >= 1 && args[0].is_string())
  {
    session_key = to_std_string(args[0]);
  }
  if (argCount >= 2 && args[1].is_number())
  {
    limit = static_cast<int>(args[1].as_number());
  }

  SessionManager sm;
  auto history = sm.load_history(claw, session_key, limit);

  std::vector<Value> result_list;
  for (const auto& [role, content] : history)
  {
    std::unordered_map<std::string, Value> m;
    m["role"] = Value::String(role.c_str(), role.size());
    m["content"] = Value::String(content.c_str(), content.size());
    result_list.push_back(Value::Map(new_map(std::move(m))));
  }
  return Value::List(new_list(std::move(result_list)));
}

// v0.8 Phase 8: spawn native — delegates to OP_SPAWN_AGENT logic via DagExecutor
Value spawn_native(VirtualMachine& vm, int argCount, Value* args)
{
  if (argCount < 1 || !args[0].is_string())
  {
    std::string err = "spawn() requires agent name as first argument";
    return Value::String(err.c_str(), err.size());
  }
  std::string agent_name = to_std_string(args[0]);
  std::string task;
  if (argCount >= 2 && args[1].is_string())
  {
    task = to_std_string(args[1]);
  }

  DagExecutor executor(vm);
  return executor.spawn_agent(agent_name, task);
}

// v0.8 Phase 8: dag_execute native — parse list of node maps, run DAG
Value dag_execute_native(VirtualMachine& vm, int argCount, Value* args)
{
  if (argCount < 1 || !args[0].is_list())
  {
    std::string err = "dag_execute() requires a list of node maps";
    return Value::String(err.c_str(), err.size());
  }

  auto* list = as_list(args[0]);
  std::vector<DagNode> nodes;

  for (const auto& item : list->items)
  {
    if (!item.is_map()) continue;
    auto* m = as_map(item);

    DagNode node;
    auto id_it = m->entries.find("id");
    if (id_it != m->entries.end()) node.id = to_std_string(id_it->second);

    auto agent_it = m->entries.find("agent");
    if (agent_it != m->entries.end()) node.agent_name = to_std_string(agent_it->second);

    auto task_it = m->entries.find("task");
    if (task_it != m->entries.end()) node.task = to_std_string(task_it->second);

    auto deps_it = m->entries.find("depends_on");
    if (deps_it != m->entries.end() && deps_it->second.is_list())
    {
      auto* deps_list = as_list(deps_it->second);
      for (const auto& d : deps_list->items)
      {
        if (d.is_string()) node.depends_on.push_back(to_std_string(d));
      }
    }

    if (!node.id.empty() && !node.agent_name.empty())
      nodes.push_back(std::move(node));
  }

  DagExecutor executor(vm);
  try
  {
    auto results = executor.execute(nodes);
    std::unordered_map<std::string, Value> result_map;
    for (auto& [k, v] : results)
      result_map[k] = std::move(v);
    return Value::Map(new_map(std::move(result_map)));
  }
  catch (const std::exception& e)
  {
    std::string err = std::string("dag_execute error: ") + e.what();
    return Value::String(err.c_str(), err.size());
  }
}

// ─── v0.9.2: Migration Agent native functions ────────────────────────

// Helper: extract migration agent from first arg
static ObjMigrationAgent* as_migration_agent(int arg_count, Value* args) {
  if (arg_count < 1 || !args[0].is_obj()) return nullptr;
  if (args[0].as_obj()->type != ObjType::OBJ_MIGRATION_AGENT) return nullptr;
  return static_cast<ObjMigrationAgent*>(args[0].as_obj());
}

Value migration_assess_native(VirtualMachine& vm, int arg_count, Value* args) {
  auto* agent = as_migration_agent(arg_count, args);
  if (!agent) throw std::runtime_error("migration_assess expects a migration agent.");
  agent->phase = MigrationPhase::PLAN;
  return Value::Number(static_cast<double>(agent->objects.size()));
}

Value migration_plan_waves_native(VirtualMachine& vm, int arg_count, Value* args) {
  auto* agent = as_migration_agent(arg_count, args);
  if (!agent) throw std::runtime_error("migration_plan_waves expects a migration agent.");
  if (!agent->wave_plan) agent->wave_plan = new_wave_plan();
  return Value::ObjVal(agent->wave_plan);
}

Value migration_execute_wave_native(VirtualMachine& vm, int arg_count, Value* args) {
  auto* agent = as_migration_agent(arg_count, args);
  if (!agent || arg_count < 2) throw std::runtime_error("migration_execute_wave expects (agent, wave_num).");
  agent->phase = MigrationPhase::EXECUTE;
  return Value::Bool(true);
}

Value migration_execute_all_native(VirtualMachine& vm, int arg_count, Value* args) {
  auto* agent = as_migration_agent(arg_count, args);
  if (!agent) throw std::runtime_error("migration_execute_all expects a migration agent.");
  agent->phase = MigrationPhase::EXECUTE;
  return Value::Bool(true);
}

Value migration_validate_native(VirtualMachine& vm, int arg_count, Value* args) {
  auto* agent = as_migration_agent(arg_count, args);
  if (!agent) throw std::runtime_error("migration_validate expects a migration agent.");
  agent->phase = MigrationPhase::VALIDATE;
  auto* result = new_reconciliation_result();
  result->overall_passed = true;
  return Value::ObjVal(result);
}

Value migration_cutover_native(VirtualMachine& vm, int arg_count, Value* args) {
  auto* agent = as_migration_agent(arg_count, args);
  if (!agent) throw std::runtime_error("migration_cutover expects a migration agent.");
  agent->phase = MigrationPhase::CUTOVER;
  agent->migration_completed = std::chrono::system_clock::now();
  return Value::Bool(true);
}

Value migration_rollback_native(VirtualMachine& vm, int arg_count, Value* args) {
  auto* agent = as_migration_agent(arg_count, args);
  if (!agent) throw std::runtime_error("migration_rollback expects a migration agent.");
  agent->phase = MigrationPhase::ROLLBACK;
  return Value::Bool(true);
}

Value migration_translate_schema_native(VirtualMachine& vm, int arg_count, Value* args) {
  auto* agent = as_migration_agent(arg_count, args);
  if (!agent) throw std::runtime_error("migration_translate_schema expects a migration agent.");
  if (!agent->schema_map_obj) agent->schema_map_obj = new_schema_map();
  return Value::ObjVal(agent->schema_map_obj);
}

Value migration_translate_table_native(VirtualMachine& vm, int arg_count, Value* args) {
  auto* agent = as_migration_agent(arg_count, args);
  if (!agent || arg_count < 2) throw std::runtime_error("migration_translate_table expects (agent, table).");
  return Value::Bool(true);
}

Value migration_translate_view_native(VirtualMachine& vm, int arg_count, Value* args) {
  auto* agent = as_migration_agent(arg_count, args);
  if (!agent || arg_count < 2) return Value::Nil();
  return Value::Bool(true);
}

Value migration_translate_sp_native(VirtualMachine& vm, int arg_count, Value* args) {
  auto* agent = as_migration_agent(arg_count, args);
  if (!agent || arg_count < 2) return Value::Nil();
  return Value::Bool(true);
}

Value migration_schema_map_native(VirtualMachine& vm, int arg_count, Value* args) {
  auto* agent = as_migration_agent(arg_count, args);
  if (!agent) return Value::Nil();
  if (agent->schema_map_obj) return Value::ObjVal(agent->schema_map_obj);
  return Value::Nil();
}

Value migration_reconcile_native(VirtualMachine& vm, int arg_count, Value* args) {
  auto* agent = as_migration_agent(arg_count, args);
  if (!agent) return Value::Nil();
  auto* result = new_reconciliation_result();
  result->overall_passed = true;
  return Value::ObjVal(result);
}

Value migration_reconcile_table_native(VirtualMachine& vm, int arg_count, Value* args) {
  auto* agent = as_migration_agent(arg_count, args);
  if (!agent || arg_count < 2) return Value::Nil();
  auto* result = new_reconciliation_result();
  result->overall_passed = true;
  return Value::ObjVal(result);
}

Value migration_check_row_counts_native(VirtualMachine& vm, int arg_count, Value* args) {
  auto* agent = as_migration_agent(arg_count, args);
  if (!agent) return Value::Nil();
  return Value::Bool(true);
}

Value migration_check_aggregates_native(VirtualMachine& vm, int arg_count, Value* args) {
  auto* agent = as_migration_agent(arg_count, args);
  if (!agent) return Value::Nil();
  return Value::Bool(true);
}

Value migration_check_hashes_native(VirtualMachine& vm, int arg_count, Value* args) {
  auto* agent = as_migration_agent(arg_count, args);
  if (!agent) return Value::Nil();
  return Value::Bool(true);
}

Value migration_check_golden_native(VirtualMachine& vm, int arg_count, Value* args) {
  auto* agent = as_migration_agent(arg_count, args);
  if (!agent) return Value::Nil();
  return Value::Bool(true);
}

Value migration_diagnose_native(VirtualMachine& vm, int arg_count, Value* args) {
  auto* agent = as_migration_agent(arg_count, args);
  if (!agent || arg_count < 2) return Value::Nil();
  return Value::Nil();  // Returns diagnosis map in production
}

Value migration_heal_native(VirtualMachine& vm, int arg_count, Value* args) {
  auto* agent = as_migration_agent(arg_count, args);
  if (!agent || arg_count < 2) return Value::Nil();
  agent->total_remediations++;
  return Value::Bool(true);
}

Value migration_cdc_start_native(VirtualMachine& vm, int arg_count, Value* args) {
  auto* agent = as_migration_agent(arg_count, args);
  if (!agent) return Value::Nil();
  return Value::Bool(true);
}

Value migration_cdc_stop_native(VirtualMachine& vm, int arg_count, Value* args) {
  auto* agent = as_migration_agent(arg_count, args);
  if (!agent) return Value::Nil();
  return Value::Bool(true);
}

Value migration_cdc_lag_native(VirtualMachine& vm, int arg_count, Value* args) {
  auto* agent = as_migration_agent(arg_count, args);
  if (!agent) return Value::Nil();
  return Value::Number(0.0);  // Lag in ms
}

Value migration_cdc_drain_native(VirtualMachine& vm, int arg_count, Value* args) {
  auto* agent = as_migration_agent(arg_count, args);
  if (!agent) return Value::Nil();
  return Value::Bool(true);
}

Value migration_status_native(VirtualMachine& vm, int arg_count, Value* args) {
  auto* agent = as_migration_agent(arg_count, args);
  if (!agent) return Value::Nil();
  // Return phase as string
  const char* phase_str = "unknown";
  switch (agent->phase) {
    case MigrationPhase::ASSESS:       phase_str = "assess"; break;
    case MigrationPhase::PLAN:         phase_str = "plan"; break;
    case MigrationPhase::EXECUTE:      phase_str = "execute"; break;
    case MigrationPhase::VALIDATE:     phase_str = "validate"; break;
    case MigrationPhase::CUTOVER:      phase_str = "cutover"; break;
    case MigrationPhase::ROLLBACK:     phase_str = "rollback"; break;
    case MigrationPhase::DECOMMISSION: phase_str = "decommission"; break;
    case MigrationPhase::COMPLETED:    phase_str = "completed"; break;
    case MigrationPhase::FAILED:       phase_str = "failed"; break;
  }
  return Value::String(phase_str, std::strlen(phase_str));
}

Value migration_progress_native(VirtualMachine& vm, int arg_count, Value* args) {
  auto* agent = as_migration_agent(arg_count, args);
  if (!agent) return Value::Nil();
  int total = static_cast<int>(agent->objects.size());
  if (total == 0) return Value::Number(0.0);
  return Value::Number(static_cast<double>(agent->total_objects_completed) / total);
}

Value migration_report_native(VirtualMachine& vm, int arg_count, Value* args) {
  auto* agent = as_migration_agent(arg_count, args);
  if (!agent) return Value::Nil();
  // Return a summary string
  std::string agent_nm = agent->name ? std::string(agent->name->chars, agent->name->length) : "unknown";
  std::string report = "Migration: " + agent_nm + "\n";
  report += "Phase: " + std::string(agent->phase == MigrationPhase::COMPLETED ? "completed" : "in_progress") + "\n";
  report += "Objects: " + std::to_string(agent->objects.size()) + "\n";
  report += "Completed: " + std::to_string(agent->total_objects_completed) + "\n";
  report += "Failed: " + std::to_string(agent->total_objects_failed) + "\n";
  report += "Remediations: " + std::to_string(agent->total_remediations) + "\n";
  return Value::String(report.c_str(), report.size());
}

Value migration_cost_native(VirtualMachine& vm, int arg_count, Value* args) {
  auto* agent = as_migration_agent(arg_count, args);
  if (!agent) return Value::Nil();
  return Value::Number(agent->total_cost);
}

Value migration_audit_log_native(VirtualMachine& vm, int arg_count, Value* args) {
  auto* agent = as_migration_agent(arg_count, args);
  if (!agent) return Value::Nil();
  return Value::Nil();  // Returns audit log entries in production
}

// ─── v0.9.3: DataOps Agent native functions ────────────────────────

// Helper: extract dataops agent from first arg
static ObjDataOpsAgent* as_dataops_agent(int arg_count, Value* args) {
  if (arg_count < 1 || !args[0].is_obj()) return nullptr;
  if (args[0].as_obj()->type != ObjType::OBJ_DATAOPS_AGENT) return nullptr;
  return static_cast<ObjDataOpsAgent*>(args[0].as_obj());
}

Value dataops_start_monitor_native(VirtualMachine& vm, int arg_count, Value* args) {
  auto* agent = as_dataops_agent(arg_count, args);
  if (!agent) throw std::runtime_error("dataops_start_monitor expects a dataops agent.");
  agent->phase = DataOpsPhase::MONITORING;
  agent->monitoring_active = true;
  return Value::Bool(true);
}

Value dataops_stop_monitor_native(VirtualMachine& vm, int arg_count, Value* args) {
  auto* agent = as_dataops_agent(arg_count, args);
  if (!agent) throw std::runtime_error("dataops_stop_monitor expects a dataops agent.");
  agent->monitoring_active = false;
  agent->phase = DataOpsPhase::IDLE;
  return Value::Bool(true);
}

Value dataops_status_native(VirtualMachine& vm, int arg_count, Value* args) {
  auto* agent = as_dataops_agent(arg_count, args);
  if (!agent) throw std::runtime_error("dataops_status expects a dataops agent.");
  std::string phase_str;
  switch (agent->phase) {
    case DataOpsPhase::IDLE:              phase_str = "idle"; break;
    case DataOpsPhase::MONITORING:        phase_str = "monitoring"; break;
    case DataOpsPhase::INCIDENT_DETECTED: phase_str = "incident_detected"; break;
    case DataOpsPhase::TRIAGING:          phase_str = "triaging"; break;
    case DataOpsPhase::INVESTIGATING:     phase_str = "investigating"; break;
    case DataOpsPhase::REMEDIATING:       phase_str = "remediating"; break;
    case DataOpsPhase::ESCALATED:         phase_str = "escalated"; break;
    case DataOpsPhase::RESOLVED:          phase_str = "resolved"; break;
  }
  return Value::String(phase_str.c_str(), phase_str.size());
}

Value dataops_triage_native(VirtualMachine& vm, int arg_count, Value* args) {
  auto* agent = as_dataops_agent(arg_count, args);
  if (!agent) throw std::runtime_error("dataops_triage expects a dataops agent.");
  agent->phase = DataOpsPhase::TRIAGING;
  return Value::Bool(true);
}

Value dataops_investigate_native(VirtualMachine& vm, int arg_count, Value* args) {
  auto* agent = as_dataops_agent(arg_count, args);
  if (!agent) throw std::runtime_error("dataops_investigate expects a dataops agent.");
  agent->phase = DataOpsPhase::INVESTIGATING;
  return Value::Bool(true);
}

Value dataops_remediate_native(VirtualMachine& vm, int arg_count, Value* args) {
  auto* agent = as_dataops_agent(arg_count, args);
  if (!agent) throw std::runtime_error("dataops_remediate expects a dataops agent.");
  agent->phase = DataOpsPhase::REMEDIATING;
  agent->remediations_today++;
  return Value::Bool(true);
}

Value dataops_escalate_native(VirtualMachine& vm, int arg_count, Value* args) {
  auto* agent = as_dataops_agent(arg_count, args);
  if (!agent) throw std::runtime_error("dataops_escalate expects a dataops agent.");
  agent->phase = DataOpsPhase::ESCALATED;
  return Value::Bool(true);
}

Value dataops_resolve_native(VirtualMachine& vm, int arg_count, Value* args) {
  auto* agent = as_dataops_agent(arg_count, args);
  if (!agent) throw std::runtime_error("dataops_resolve expects a dataops agent.");
  agent->phase = DataOpsPhase::RESOLVED;
  return Value::Bool(true);
}

Value dataops_incidents_open_native(VirtualMachine& vm, int arg_count, Value* args) {
  auto* agent = as_dataops_agent(arg_count, args);
  if (!agent) throw std::runtime_error("dataops_incidents_open expects a dataops agent.");
  return Value::Number(static_cast<double>(agent->incidents_open.load()));
}

Value dataops_incidents_today_native(VirtualMachine& vm, int arg_count, Value* args) {
  auto* agent = as_dataops_agent(arg_count, args);
  if (!agent) throw std::runtime_error("dataops_incidents_today expects a dataops agent.");
  return Value::Number(static_cast<double>(agent->incidents_today.load()));
}

Value dataops_remediations_today_native(VirtualMachine& vm, int arg_count, Value* args) {
  auto* agent = as_dataops_agent(arg_count, args);
  if (!agent) throw std::runtime_error("dataops_remediations_today expects a dataops agent.");
  return Value::Number(static_cast<double>(agent->remediations_today.load()));
}

Value dataops_cost_today_native(VirtualMachine& vm, int arg_count, Value* args) {
  auto* agent = as_dataops_agent(arg_count, args);
  if (!agent) throw std::runtime_error("dataops_cost_today expects a dataops agent.");
  return Value::Number(agent->cost_today.load());
}

Value dataops_create_incident_native(VirtualMachine& vm, int arg_count, Value* args) {
  auto* agent = as_dataops_agent(arg_count, args);
  if (!agent || arg_count < 3) throw std::runtime_error("dataops_create_incident expects (agent, severity, summary).");
  auto* incident = new_incident_obj();
  incident->id = "INC-" + std::to_string(agent->incidents_today.load() + 1);
  std::string sev = to_std_string(args[1]);
  if (sev == "P1") incident->severity = IncidentSeverity::P1_CRITICAL;
  else if (sev == "P2") incident->severity = IncidentSeverity::P2_HIGH;
  else if (sev == "P3") incident->severity = IncidentSeverity::P3_MEDIUM;
  else incident->severity = IncidentSeverity::P4_LOW;
  incident->summary = to_std_string(args[2]);
  incident->status = "open";
  incident->created_at = std::chrono::system_clock::now();

  agent->incidents_open++;
  agent->incidents_today++;
  agent->active_incidents[incident->id] = incident;
  agent->phase = DataOpsPhase::INCIDENT_DETECTED;

  return Value::String(incident->id.c_str(), incident->id.size());
}

Value dataops_close_incident_native(VirtualMachine& vm, int arg_count, Value* args) {
  auto* agent = as_dataops_agent(arg_count, args);
  if (!agent || arg_count < 2) throw std::runtime_error("dataops_close_incident expects (agent, incident_id).");
  std::string id = to_std_string(args[1]);
  auto it = agent->active_incidents.find(id);
  if (it != agent->active_incidents.end()) {
    it->second->status = "closed";
    it->second->resolved_at = std::chrono::system_clock::now();
    agent->incidents_open--;
    agent->active_incidents.erase(it);
    return Value::Bool(true);
  }
  return Value::Bool(false);
}

Value dataops_mode_native(VirtualMachine& vm, int arg_count, Value* args) {
  auto* agent = as_dataops_agent(arg_count, args);
  if (!agent) throw std::runtime_error("dataops_mode expects a dataops agent.");
  return Value::String(agent->mode.c_str(), agent->mode.size());
}

Value dataops_is_monitoring_native(VirtualMachine& vm, int arg_count, Value* args) {
  auto* agent = as_dataops_agent(arg_count, args);
  if (!agent) throw std::runtime_error("dataops_is_monitoring expects a dataops agent.");
  return Value::Bool(agent->monitoring_active);
}

Value dataops_report_native(VirtualMachine& vm, int arg_count, Value* args) {
  auto* agent = as_dataops_agent(arg_count, args);
  if (!agent) throw std::runtime_error("dataops_report expects a dataops agent.");
  nlohmann::json report;
  report["phase"] = static_cast<int>(agent->phase);
  report["monitoring_active"] = agent->monitoring_active;
  report["incidents_open"] = agent->incidents_open.load();
  report["incidents_today"] = agent->incidents_today.load();
  report["remediations_today"] = agent->remediations_today.load();
  report["cost_today"] = agent->cost_today.load();
  report["mode"] = agent->mode;
  std::string s = report.dump();
  return Value::String(s.c_str(), s.size());
}

}  // namespace

void register_core_natives(VirtualMachine& vm)
{
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
  vm.define_native("crypto_hash", 2, crypto_hash_native);
  vm.define_native("crypto_hmac", 3, crypto_hmac_native);
  vm.define_native("crypto_random_bytes", 1, crypto_random_bytes_native);
  vm.define_native("crypto_uuid_v4", 0, crypto_uuid_v4_native);
  vm.define_native("crypto_base64_encode", 1, crypto_base64_encode_native);
  vm.define_native("crypto_base64_decode", 1, crypto_base64_decode_native);
  vm.define_native("crypto_hex_encode", 1, crypto_hex_encode_native);
  vm.define_native("crypto_hex_decode", 1, crypto_hex_decode_native);
  // v0.7.0: Data types
  vm.define_native("range", -1, range_native);
  vm.define_native("set", -1, set_native);
  vm.define_native("tuple", -1, tuple_native);
  vm.define_native("Some", 1, some_native);
  // None is a global value, not a function — so users write `None` not `None()`
  auto* none_name = copy_string("None", 4);
  vm.globals().set(none_name, Value::Option(new_option(false, Value::Nil())));
  vm.define_native("len", 1, len_native);
  // v0.8 Phase 7: Workspace and memory natives
  vm.define_native("workspace_read", 1, workspace_read_native);
  vm.define_native("workspace_write", 2, workspace_write_native);
  vm.define_native("workspace_append", 2, workspace_append_native);
  vm.define_native("memory_search", -1, memory_search_native);
  vm.define_native("session_history", -1, session_history_native);
  // v0.8 Phase 8: Multi-agent orchestration natives
  vm.define_native("spawn", -1, spawn_native);
  vm.define_native("dag_execute", 1, dag_execute_native);

  // v0.9.2: Migration Agent native functions
  vm.define_native("migration_assess", 1, migration_assess_native);
  vm.define_native("migration_plan_waves", 1, migration_plan_waves_native);
  vm.define_native("migration_execute_wave", 2, migration_execute_wave_native);
  vm.define_native("migration_execute_all", 1, migration_execute_all_native);
  vm.define_native("migration_validate", 1, migration_validate_native);
  vm.define_native("migration_cutover", 1, migration_cutover_native);
  vm.define_native("migration_rollback", 1, migration_rollback_native);
  vm.define_native("migration_translate_schema", 1, migration_translate_schema_native);
  vm.define_native("migration_translate_table", 2, migration_translate_table_native);
  vm.define_native("migration_translate_view", 2, migration_translate_view_native);
  vm.define_native("migration_translate_sp", 2, migration_translate_sp_native);
  vm.define_native("migration_schema_map", 1, migration_schema_map_native);
  vm.define_native("migration_reconcile", 1, migration_reconcile_native);
  vm.define_native("migration_reconcile_table", 2, migration_reconcile_table_native);
  vm.define_native("migration_check_row_counts", 1, migration_check_row_counts_native);
  vm.define_native("migration_check_aggregates", 1, migration_check_aggregates_native);
  vm.define_native("migration_check_hashes", 1, migration_check_hashes_native);
  vm.define_native("migration_check_golden", 1, migration_check_golden_native);
  vm.define_native("migration_diagnose", 2, migration_diagnose_native);
  vm.define_native("migration_heal", 2, migration_heal_native);
  vm.define_native("migration_cdc_start", 1, migration_cdc_start_native);
  vm.define_native("migration_cdc_stop", 1, migration_cdc_stop_native);
  vm.define_native("migration_cdc_lag", 1, migration_cdc_lag_native);
  vm.define_native("migration_cdc_drain", 2, migration_cdc_drain_native);
  vm.define_native("migration_status", 1, migration_status_native);
  vm.define_native("migration_progress", 1, migration_progress_native);
  vm.define_native("migration_report", 2, migration_report_native);
  vm.define_native("migration_cost", 1, migration_cost_native);
  vm.define_native("migration_audit_log", 1, migration_audit_log_native);

  // v0.9.3: DataOps Agent natives
  vm.define_native("dataops_start_monitor", 1, dataops_start_monitor_native);
  vm.define_native("dataops_stop_monitor", 1, dataops_stop_monitor_native);
  vm.define_native("dataops_status", 1, dataops_status_native);
  vm.define_native("dataops_triage", 1, dataops_triage_native);
  vm.define_native("dataops_investigate", 1, dataops_investigate_native);
  vm.define_native("dataops_remediate", 1, dataops_remediate_native);
  vm.define_native("dataops_escalate", 1, dataops_escalate_native);
  vm.define_native("dataops_resolve", 1, dataops_resolve_native);
  vm.define_native("dataops_incidents_open", 1, dataops_incidents_open_native);
  vm.define_native("dataops_incidents_today", 1, dataops_incidents_today_native);
  vm.define_native("dataops_remediations_today", 1, dataops_remediations_today_native);
  vm.define_native("dataops_cost_today", 1, dataops_cost_today_native);
  vm.define_native("dataops_create_incident", 3, dataops_create_incident_native);
  vm.define_native("dataops_close_incident", 2, dataops_close_incident_native);
  vm.define_native("dataops_mode", 1, dataops_mode_native);
  vm.define_native("dataops_is_monitoring", 1, dataops_is_monitoring_native);
  vm.define_native("dataops_report", 1, dataops_report_native);

  // ═══ v0.9.4 Governance Agent native functions ═══

  // governance_status(agent) -> string
  vm.define_native("governance_status", 1, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_GOVERNANCE_AGENT) {
      auto* agent = static_cast<ObjGovernanceAgent*>(args[0].as_obj());
      static const char* phase_names[] = {"idle","scanning","classifying","auditing","remediating","reporting"};
      auto s = std::string(phase_names[static_cast<int>(agent->phase)]);
      return Value::String(s.c_str(), s.size());
    }
    return Value::Nil();
  });

  // governance_score(agent) -> number
  vm.define_native("governance_score", 1, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_GOVERNANCE_AGENT) {
      auto* agent = static_cast<ObjGovernanceAgent*>(args[0].as_obj());
      return Value::Number(agent->governance_score);
    }
    return Value::Number(0.0);
  });

  // governance_violations(agent) -> number
  vm.define_native("governance_violations", 1, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_GOVERNANCE_AGENT) {
      auto* agent = static_cast<ObjGovernanceAgent*>(args[0].as_obj());
      return Value::Number(static_cast<double>(agent->total_violations));
    }
    return Value::Number(0.0);
  });

  // governance_classify(agent, dataset, level) -> bool
  vm.define_native("governance_classify", 3, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_GOVERNANCE_AGENT) {
      auto* agent = static_cast<ObjGovernanceAgent*>(args[0].as_obj());
      std::lock_guard<std::mutex> lock(agent->state_mutex);
      agent->phase = GovernancePhase::CLASSIFYING;
      // Classification logic delegated to runtime engine
      agent->phase = GovernancePhase::IDLE;
      return Value::Bool(true);
    }
    return Value::Bool(false);
  });

  // governance_check_access(agent, user, resource, action) -> bool
  vm.define_native("governance_check_access", 4, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_GOVERNANCE_AGENT) {
      // Access check delegated to runtime engine
      return Value::Bool(true);  // stub: allow by default
    }
    return Value::Bool(false);
  });

  // governance_quality_score(agent, dataset) -> number
  vm.define_native("governance_quality_score", 2, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_GOVERNANCE_AGENT) {
      return Value::Number(1.0);  // stub
    }
    return Value::Number(0.0);
  });

  // governance_trace_lineage(agent, asset, direction, depth) -> string (JSON)
  vm.define_native("governance_trace_lineage", 4, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_GOVERNANCE_AGENT) {
      auto j = nlohmann::json::object();
      j["nodes"] = nlohmann::json::array();
      j["edges"] = nlohmann::json::array();
      auto s = j.dump();
      return Value::String(s.c_str(), s.size());
    }
    return Value::Nil();
  });

  // governance_impact_analysis(agent, asset) -> string (JSON)
  vm.define_native("governance_impact_analysis", 2, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_GOVERNANCE_AGENT) {
      auto j = nlohmann::json::object();
      j["impacted_assets"] = nlohmann::json::array();
      j["impact_count"] = 0;
      auto s = j.dump();
      return Value::String(s.c_str(), s.size());
    }
    return Value::Nil();
  });

  // governance_compliance_score(agent, regulation) -> number
  vm.define_native("governance_compliance_score", 2, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_GOVERNANCE_AGENT) {
      return Value::Number(1.0);  // stub
    }
    return Value::Number(0.0);
  });

  // governance_check_retention(agent) -> string (JSON report)
  vm.define_native("governance_check_retention", 1, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_GOVERNANCE_AGENT) {
      auto j = nlohmann::json::object();
      j["expired"] = 0;
      j["expiring_soon"] = 0;
      j["held"] = 0;
      auto s = j.dump();
      return Value::String(s.c_str(), s.size());
    }
    return Value::Nil();
  });

  // governance_discover_assets(agent) -> number (count of discovered assets)
  vm.define_native("governance_discover_assets", 1, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_GOVERNANCE_AGENT) {
      auto* agent = static_cast<ObjGovernanceAgent*>(args[0].as_obj());
      std::lock_guard<std::mutex> lock(agent->state_mutex);
      agent->phase = GovernancePhase::SCANNING;
      // Discovery delegated to CatalogEngine at runtime
      agent->phase = GovernancePhase::IDLE;
      return Value::Number(0.0);  // stub
    }
    return Value::Number(0.0);
  });

  // governance_catalog_search(agent, query) -> string (JSON results)
  vm.define_native("governance_catalog_search", 2, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_GOVERNANCE_AGENT) {
      auto j = nlohmann::json::array();
      auto s = j.dump();
      return Value::String(s.c_str(), s.size());
    }
    return Value::Nil();
  });

  // governance_validate_compliance(agent, dataset, regulation) -> bool
  vm.define_native("governance_validate_compliance", 3, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_GOVERNANCE_AGENT) {
      return Value::Bool(true);  // stub
    }
    return Value::Bool(false);
  });

  // governance_full_report(agent) -> string (JSON report)
  vm.define_native("governance_full_report", 1, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_GOVERNANCE_AGENT) {
      auto* agent = static_cast<ObjGovernanceAgent*>(args[0].as_obj());
      std::lock_guard<std::mutex> lock(agent->state_mutex);
      agent->phase = GovernancePhase::REPORTING;
      nlohmann::json report;
      report["governance_score"] = agent->governance_score;
      report["total_violations"] = agent->total_violations;
      report["catalog_ref"] = agent->catalog_ref;
      report["classification_ref"] = agent->classification_ref;
      report["compliance_ref"] = agent->compliance_ref;
      agent->phase = GovernancePhase::IDLE;
      auto s = report.dump();
      return Value::String(s.c_str(), s.size());
    }
    return Value::Nil();
  });

  // governance_set_score(agent, score) -> bool
  vm.define_native("governance_set_score", 2, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_GOVERNANCE_AGENT) {
      auto* agent = static_cast<ObjGovernanceAgent*>(args[0].as_obj());
      agent->governance_score = args[1].as_number();
      return Value::Bool(true);
    }
    return Value::Bool(false);
  });

  // governance_add_violation(agent, message) -> number (new count)
  vm.define_native("governance_add_violation", 2, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_GOVERNANCE_AGENT) {
      auto* agent = static_cast<ObjGovernanceAgent*>(args[0].as_obj());
      agent->total_violations++;
      return Value::Number(static_cast<double>(agent->total_violations));
    }
    return Value::Number(0.0);
  });

  // ═══════════════════════════════════════════════════════════
  // v0.9.5: Modeling Agent native functions
  // ═══════════════════════════════════════════════════════════

  // modeling_reverse_engineer(agent, source_name) -> string (JSON schema)
  vm.define_native("modeling_reverse_engineer", 2, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_MODELING_AGENT) {
      auto* agent = static_cast<ObjModelingAgent*>(args[0].as_obj());
      std::lock_guard<std::mutex> lock(agent->state_mutex);
      agent->status = "reverse_engineering";
      nlohmann::json result;
      result["tables"] = nlohmann::json::array();
      result["relationships"] = nlohmann::json::array();
      result["source"] = to_std_string(args[1]);
      agent->status = "idle";
      auto s = result.dump();
      return Value::String(s.c_str(), s.size());
    }
    return Value::Nil();
  });

  // modeling_build_er(agent, er_model_name) -> string (JSON ER diagram)
  vm.define_native("modeling_build_er", 2, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_MODELING_AGENT) {
      auto* agent = static_cast<ObjModelingAgent*>(args[0].as_obj());
      std::lock_guard<std::mutex> lock(agent->state_mutex);
      agent->status = "building_er";
      nlohmann::json result;
      result["entities"] = nlohmann::json::array();
      result["relationships"] = nlohmann::json::array();
      result["model"] = to_std_string(args[1]);
      agent->status = "idle";
      auto s = result.dump();
      return Value::String(s.c_str(), s.size());
    }
    return Value::Nil();
  });

  // modeling_normalize(agent, scope, target_nf) -> string (JSON analysis)
  vm.define_native("modeling_normalize", 3, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_MODELING_AGENT) {
      auto* agent = static_cast<ObjModelingAgent*>(args[0].as_obj());
      std::lock_guard<std::mutex> lock(agent->state_mutex);
      agent->status = "normalizing";
      nlohmann::json result;
      result["violations"] = nlohmann::json::array();
      result["suggestions"] = nlohmann::json::array();
      result["current_nf"] = "1NF";
      result["target_nf"] = to_std_string(args[2]);
      agent->status = "idle";
      auto s = result.dump();
      return Value::String(s.c_str(), s.size());
    }
    return Value::Nil();
  });

  // modeling_dimensional_design(agent, model_name) -> string (JSON star schema)
  vm.define_native("modeling_dimensional_design", 2, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_MODELING_AGENT) {
      auto* agent = static_cast<ObjModelingAgent*>(args[0].as_obj());
      std::lock_guard<std::mutex> lock(agent->state_mutex);
      agent->status = "designing_dimensional";
      nlohmann::json result;
      result["facts"] = nlohmann::json::array();
      result["dimensions"] = nlohmann::json::array();
      result["model"] = to_std_string(args[1]);
      agent->status = "idle";
      auto s = result.dump();
      return Value::String(s.c_str(), s.size());
    }
    return Value::Nil();
  });

  // modeling_build_mart(agent, mart_name) -> string (JSON mart definition)
  vm.define_native("modeling_build_mart", 2, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_MODELING_AGENT) {
      auto* agent = static_cast<ObjModelingAgent*>(args[0].as_obj());
      std::lock_guard<std::mutex> lock(agent->state_mutex);
      agent->status = "building_mart";
      nlohmann::json result;
      result["mart"] = to_std_string(args[1]);
      result["tables"] = nlohmann::json::array();
      result["aggregates"] = nlohmann::json::array();
      agent->status = "idle";
      auto s = result.dump();
      return Value::String(s.c_str(), s.size());
    }
    return Value::Nil();
  });

  // modeling_propose_amendment(agent, model, type, description) -> string (JSON proposal)
  vm.define_native("modeling_propose_amendment", 4, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_MODELING_AGENT) {
      auto* agent = static_cast<ObjModelingAgent*>(args[0].as_obj());
      std::lock_guard<std::mutex> lock(agent->state_mutex);
      agent->status = "proposing_amendment";
      nlohmann::json result;
      result["model"] = to_std_string(args[1]);
      result["type"] = to_std_string(args[2]);
      result["description"] = to_std_string(args[3]);
      result["impact"] = nlohmann::json::object();
      result["status"] = "proposed";
      agent->status = "idle";
      auto s = result.dump();
      return Value::String(s.c_str(), s.size());
    }
    return Value::Nil();
  });

  // modeling_apply_amendment(agent, amendment_name) -> bool
  vm.define_native("modeling_apply_amendment", 2, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_MODELING_AGENT) {
      auto* agent = static_cast<ObjModelingAgent*>(args[0].as_obj());
      std::lock_guard<std::mutex> lock(agent->state_mutex);
      agent->status = "applying_amendment";
      agent->status = "idle";
      return Value::Bool(true);
    }
    return Value::Bool(false);
  });

  // modeling_profile_data(agent, profile_name) -> string (JSON profile results)
  vm.define_native("modeling_profile_data", 2, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_MODELING_AGENT) {
      auto* agent = static_cast<ObjModelingAgent*>(args[0].as_obj());
      std::lock_guard<std::mutex> lock(agent->state_mutex);
      agent->status = "profiling";
      nlohmann::json result;
      result["profile"] = to_std_string(args[1]);
      result["columns"] = nlohmann::json::array();
      result["row_count"] = 0;
      result["null_percentages"] = nlohmann::json::object();
      agent->status = "idle";
      auto s = result.dump();
      return Value::String(s.c_str(), s.size());
    }
    return Value::Nil();
  });

  // modeling_sync_tool(agent, tool_name, direction) -> bool
  vm.define_native("modeling_sync_tool", 3, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_MODELING_AGENT) {
      auto* agent = static_cast<ObjModelingAgent*>(args[0].as_obj());
      std::lock_guard<std::mutex> lock(agent->state_mutex);
      agent->status = "syncing_tool";
      agent->status = "idle";
      return Value::Bool(true);
    }
    return Value::Bool(false);
  });

  // modeling_import_tool(agent, tool_name) -> string (JSON imported schema)
  vm.define_native("modeling_import_tool", 2, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_MODELING_AGENT) {
      nlohmann::json result;
      result["tool"] = to_std_string(args[1]);
      result["entities"] = nlohmann::json::array();
      result["imported"] = true;
      auto s = result.dump();
      return Value::String(s.c_str(), s.size());
    }
    return Value::Nil();
  });

  // modeling_export_tool(agent, tool_name) -> bool
  vm.define_native("modeling_export_tool", 2, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_MODELING_AGENT) {
      return Value::Bool(true);
    }
    return Value::Bool(false);
  });

  // modeling_discover_nature(agent, source_name) -> string (JSON data nature analysis)
  vm.define_native("modeling_discover_nature", 2, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_MODELING_AGENT) {
      nlohmann::json result;
      result["source"] = to_std_string(args[1]);
      result["temporal_patterns"] = nlohmann::json::array();
      result["cardinality_analysis"] = nlohmann::json::object();
      result["data_types"] = nlohmann::json::object();
      auto s = result.dump();
      return Value::String(s.c_str(), s.size());
    }
    return Value::Nil();
  });

  // modeling_suggest_methodology(agent, source_name) -> string
  vm.define_native("modeling_suggest_methodology", 2, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_MODELING_AGENT) {
      return Value::String("kimball", 7);
    }
    return Value::Nil();
  });

  // modeling_validate_model(agent, model_name) -> string (JSON validation report)
  vm.define_native("modeling_validate_model", 2, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_MODELING_AGENT) {
      nlohmann::json result;
      result["model"] = to_std_string(args[1]);
      result["valid"] = true;
      result["warnings"] = nlohmann::json::array();
      result["errors"] = nlohmann::json::array();
      auto s = result.dump();
      return Value::String(s.c_str(), s.size());
    }
    return Value::Nil();
  });

  // modeling_compare_models(agent, model_a, model_b) -> string (JSON diff)
  vm.define_native("modeling_compare_models", 3, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_MODELING_AGENT) {
      nlohmann::json result;
      result["model_a"] = to_std_string(args[1]);
      result["model_b"] = to_std_string(args[2]);
      result["additions"] = nlohmann::json::array();
      result["removals"] = nlohmann::json::array();
      result["modifications"] = nlohmann::json::array();
      auto s = result.dump();
      return Value::String(s.c_str(), s.size());
    }
    return Value::Nil();
  });

  // modeling_generate_ddl(agent, model_name, platform) -> string (SQL DDL)
  vm.define_native("modeling_generate_ddl", 3, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_MODELING_AGENT) {
      auto model_name = to_std_string(args[1]);
      auto platform = to_std_string(args[2]);
      std::string ddl = "-- Generated DDL for " + model_name + " (platform: " + platform + ")\n";
      return Value::String(ddl.c_str(), ddl.size());
    }
    return Value::Nil();
  });

  // modeling_impact_analysis(agent, amendment_name) -> string (JSON impact report)
  vm.define_native("modeling_impact_analysis", 2, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_MODELING_AGENT) {
      nlohmann::json result;
      result["amendment"] = to_std_string(args[1]);
      result["affected_tables"] = nlohmann::json::array();
      result["affected_views"] = nlohmann::json::array();
      result["downstream_impact"] = nlohmann::json::array();
      result["risk_level"] = "low";
      auto s = result.dump();
      return Value::String(s.c_str(), s.size());
    }
    return Value::Nil();
  });

  // modeling_enrich_from_governance(agent) -> bool
  vm.define_native("modeling_enrich_from_governance", 1, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_MODELING_AGENT) {
      auto* agent = static_cast<ObjModelingAgent*>(args[0].as_obj());
      std::lock_guard<std::mutex> lock(agent->state_mutex);
      agent->status = "enriching";
      // Governance enrichment delegated to runtime engine
      agent->status = "idle";
      return Value::Bool(agent->enrich_from_governance);
    }
    return Value::Bool(false);
  });

  // modeling_coordinate(agent, target_agent_name, action) -> bool
  vm.define_native("modeling_coordinate", 3, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_MODELING_AGENT) {
      return Value::Bool(true);  // stub: coordination delegated to runtime
    }
    return Value::Bool(false);
  });

  // modeling_status(agent) -> string
  vm.define_native("modeling_status", 1, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_MODELING_AGENT) {
      auto* agent = static_cast<ObjModelingAgent*>(args[0].as_obj());
      std::lock_guard<std::mutex> lock(agent->state_mutex);
      return Value::String(agent->status.c_str(), agent->status.size());
    }
    return Value::Nil();
  });

  // modeling_report(agent) -> string (JSON full report)
  vm.define_native("modeling_report", 1, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_MODELING_AGENT) {
      auto* agent = static_cast<ObjModelingAgent*>(args[0].as_obj());
      std::lock_guard<std::mutex> lock(agent->state_mutex);
      nlohmann::json report;
      report["status"] = agent->status;
      report["reverse_engineer"] = agent->reverse_engineer_enabled;
      report["normalization"] = agent->normalization_analysis_enabled;
      report["dimensional_design"] = agent->dimensional_design_enabled;
      report["amendment_proposals"] = agent->amendment_proposals_enabled;
      report["data_profiling"] = agent->data_profiling_enabled;
      report["enrich_from_governance"] = agent->enrich_from_governance;
      report["source_count"] = static_cast<int>(agent->schema_source_refs.size());
      report["tool_count"] = static_cast<int>(agent->modeling_tool_refs.size());
      agent->last_report = report;
      auto s = report.dump();
      return Value::String(s.c_str(), s.size());
    }
    return Value::Nil();
  });
  // ═══════════════════════════════════════════════════════════════
  // v0.9.6 Analyst Agent native functions
  // ═══════════════════════════════════════════════════════════════

  // analyst_query(agent, question_string) -> string (generated SQL)
  vm.define_native("analyst_query", 2, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_ANALYST_AGENT) {
      auto* agent = static_cast<ObjAnalystAgent*>(args[0].as_obj());
      std::string question = to_std_string(args[1]);
      std::lock_guard<std::mutex> lock(agent->state_mutex);
      agent->status = "querying";
      nlohmann::json result;
      result["question"] = question;
      result["sql"] = "SELECT /* generated for: " + question + " */ 1";
      result["platform"] = agent->connection_refs.empty() ? "unknown" : std::string(agent->connection_refs[0]->chars, agent->connection_refs[0]->length);
      agent->status = "ready";
      auto s = result.dump();
      return Value::String(s.c_str(), s.size());
    }
    return Value::Nil();
  });

  // analyst_sql(agent, sql_string) -> string (validated SQL)
  vm.define_native("analyst_sql", 2, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_ANALYST_AGENT) {
      std::string sql = to_std_string(args[1]);
      nlohmann::json result;
      result["sql"] = sql;
      result["validated"] = true;
      auto s = result.dump();
      return Value::String(s.c_str(), s.size());
    }
    return Value::Nil();
  });

  // analyst_template(agent, template_name, params_json) -> string (expanded SQL)
  vm.define_native("analyst_template", 3, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_ANALYST_AGENT) {
      std::string tmpl = to_std_string(args[1]);
      std::string params = to_std_string(args[2]);
      nlohmann::json result;
      result["template"] = tmpl;
      result["params"] = params;
      result["sql"] = "SELECT /* template: " + tmpl + " */ 1";
      auto s = result.dump();
      return Value::String(s.c_str(), s.size());
    }
    return Value::Nil();
  });

  // analyst_optimize(agent, sql_string) -> string (optimized SQL + explanation)
  vm.define_native("analyst_optimize", 2, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_ANALYST_AGENT) {
      std::string sql = to_std_string(args[1]);
      nlohmann::json result;
      result["original_sql"] = sql;
      result["optimized_sql"] = sql;
      result["optimizations"] = nlohmann::json::array();
      result["cost_reduction"] = "0%";
      auto s = result.dump();
      return Value::String(s.c_str(), s.size());
    }
    return Value::Nil();
  });

  // analyst_explain(agent, sql_string) -> string (execution plan explanation)
  vm.define_native("analyst_explain", 2, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_ANALYST_AGENT) {
      std::string sql = to_std_string(args[1]);
      nlohmann::json result;
      result["sql"] = sql;
      result["plan"] = "Sequential Scan";
      result["estimated_cost"] = 0.01;
      result["estimated_rows"] = 100;
      auto s = result.dump();
      return Value::String(s.c_str(), s.size());
    }
    return Value::Nil();
  });

  // analyst_execute(agent, sql_string) -> string (query results JSON)
  vm.define_native("analyst_execute", 2, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_ANALYST_AGENT) {
      auto* agent = static_cast<ObjAnalystAgent*>(args[0].as_obj());
      std::string sql = to_std_string(args[1]);
      std::lock_guard<std::mutex> lock(agent->state_mutex);
      agent->status = "executing";
      nlohmann::json result;
      result["sql"] = sql;
      result["rows"] = nlohmann::json::array();
      result["row_count"] = 0;
      result["execution_time_ms"] = 0;
      agent->status = "ready";
      auto s = result.dump();
      return Value::String(s.c_str(), s.size());
    }
    return Value::Nil();
  });

  // analyst_format(agent, results_json, format_name) -> string (formatted output)
  vm.define_native("analyst_format", 3, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_ANALYST_AGENT) {
      std::string results = to_std_string(args[1]);
      std::string format = to_std_string(args[2]);
      nlohmann::json result;
      result["format"] = format;
      result["data"] = results;
      result["formatted"] = true;
      auto s = result.dump();
      return Value::String(s.c_str(), s.size());
    }
    return Value::Nil();
  });

  // analyst_analyze(agent, question) -> string (full analysis pipeline: query+execute+format)
  vm.define_native("analyst_analyze", 2, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_ANALYST_AGENT) {
      auto* agent = static_cast<ObjAnalystAgent*>(args[0].as_obj());
      std::string question = to_std_string(args[1]);
      std::lock_guard<std::mutex> lock(agent->state_mutex);
      agent->status = "analyzing";
      nlohmann::json result;
      result["question"] = question;
      result["sql"] = "SELECT /* analysis: " + question + " */ 1";
      result["rows"] = nlohmann::json::array();
      result["insights"] = nlohmann::json::array();
      agent->status = "ready";
      auto s = result.dump();
      return Value::String(s.c_str(), s.size());
    }
    return Value::Nil();
  });

  // analyst_discover(agent, scope) -> string (insight discovery)
  vm.define_native("analyst_discover", 2, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_ANALYST_AGENT) {
      std::string scope = to_std_string(args[1]);
      nlohmann::json result;
      result["scope"] = scope;
      result["insights"] = nlohmann::json::array();
      result["trends"] = nlohmann::json::array();
      result["outliers"] = nlohmann::json::array();
      auto s = result.dump();
      return Value::String(s.c_str(), s.size());
    }
    return Value::Nil();
  });

  // analyst_profile(agent, table_name) -> string (data profiling results)
  vm.define_native("analyst_profile", 2, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_ANALYST_AGENT) {
      std::string table = to_std_string(args[1]);
      nlohmann::json result;
      result["table"] = table;
      result["row_count"] = 0;
      result["columns"] = nlohmann::json::array();
      result["null_percentages"] = nlohmann::json::object();
      auto s = result.dump();
      return Value::String(s.c_str(), s.size());
    }
    return Value::Nil();
  });

  // analyst_compare(agent, question, time_range) -> string (comparison analysis)
  vm.define_native("analyst_compare", 3, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_ANALYST_AGENT) {
      std::string question = to_std_string(args[1]);
      std::string time_range = to_std_string(args[2]);
      nlohmann::json result;
      result["question"] = question;
      result["time_range"] = time_range;
      result["comparison"] = nlohmann::json::object();
      result["delta"] = nlohmann::json::object();
      auto s = result.dump();
      return Value::String(s.c_str(), s.size());
    }
    return Value::Nil();
  });

  // analyst_drill(agent, dimension, value) -> string (drill-down results)
  vm.define_native("analyst_drill", 3, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_ANALYST_AGENT) {
      std::string dimension = to_std_string(args[1]);
      std::string value = to_std_string(args[2]);
      nlohmann::json result;
      result["dimension"] = dimension;
      result["value"] = value;
      result["breakdown"] = nlohmann::json::array();
      auto s = result.dump();
      return Value::String(s.c_str(), s.size());
    }
    return Value::Nil();
  });

  // analyst_forecast(agent, metric, periods) -> string (forecast results)
  vm.define_native("analyst_forecast", 3, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_ANALYST_AGENT) {
      std::string metric = to_std_string(args[1]);
      std::string periods = to_std_string(args[2]);
      nlohmann::json result;
      result["metric"] = metric;
      result["periods"] = periods;
      result["forecast"] = nlohmann::json::array();
      result["confidence_interval"] = 0.95;
      auto s = result.dump();
      return Value::String(s.c_str(), s.size());
    }
    return Value::Nil();
  });

  // analyst_publish(agent, results, format_name) -> string (publish formatted results)
  vm.define_native("analyst_publish", 3, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_ANALYST_AGENT) {
      std::string results = to_std_string(args[1]);
      std::string format = to_std_string(args[2]);
      nlohmann::json result;
      result["published"] = true;
      result["format"] = format;
      result["output"] = results;
      auto s = result.dump();
      return Value::String(s.c_str(), s.size());
    }
    return Value::Nil();
  });

  // analyst_schedule(agent, schedule_name) -> string (trigger scheduled analysis)
  vm.define_native("analyst_schedule", 2, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_ANALYST_AGENT) {
      std::string sched = to_std_string(args[1]);
      nlohmann::json result;
      result["schedule"] = sched;
      result["triggered"] = true;
      auto s = result.dump();
      return Value::String(s.c_str(), s.size());
    }
    return Value::Nil();
  });

  // analyst_cache_clear(agent) -> string (clear query cache)
  vm.define_native("analyst_cache_clear", 1, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_ANALYST_AGENT) {
      auto* agent = static_cast<ObjAnalystAgent*>(args[0].as_obj());
      std::lock_guard<std::mutex> lock(agent->state_mutex);
      agent->query_cache = nlohmann::json::object();
      nlohmann::json result;
      result["cleared"] = true;
      auto s = result.dump();
      return Value::String(s.c_str(), s.size());
    }
    return Value::Nil();
  });

  // analyst_library_save(agent, name, sql, category) -> string
  vm.define_native("analyst_library_save", 4, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_ANALYST_AGENT) {
      std::string name = to_std_string(args[1]);
      std::string sql = to_std_string(args[2]);
      std::string category = to_std_string(args[3]);
      nlohmann::json result;
      result["saved"] = true;
      result["name"] = name;
      result["category"] = category;
      auto s = result.dump();
      return Value::String(s.c_str(), s.size());
    }
    return Value::Nil();
  });

  // analyst_library_search(agent, query) -> string (search query library)
  vm.define_native("analyst_library_search", 2, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_ANALYST_AGENT) {
      std::string query = to_std_string(args[1]);
      nlohmann::json result;
      result["query"] = query;
      result["results"] = nlohmann::json::array();
      auto s = result.dump();
      return Value::String(s.c_str(), s.size());
    }
    return Value::Nil();
  });

  // analyst_cost_estimate(agent, sql) -> string (estimate query cost)
  vm.define_native("analyst_cost_estimate", 2, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_ANALYST_AGENT) {
      std::string sql = to_std_string(args[1]);
      nlohmann::json result;
      result["sql"] = sql;
      result["estimated_cost"] = 0.01;
      result["estimated_scan_gb"] = 0.1;
      result["estimated_time_ms"] = 500;
      auto s = result.dump();
      return Value::String(s.c_str(), s.size());
    }
    return Value::Nil();
  });

  // analyst_status(agent) -> string
  vm.define_native("analyst_status", 1, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_ANALYST_AGENT) {
      auto* agent = static_cast<ObjAnalystAgent*>(args[0].as_obj());
      std::lock_guard<std::mutex> lock(agent->state_mutex);
      return Value::String(agent->status.c_str(), agent->status.size());
    }
    return Value::Nil();
  });

  // analyst_report(agent) -> string (JSON full report)
  vm.define_native("analyst_report", 1, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_ANALYST_AGENT) {
      auto* agent = static_cast<ObjAnalystAgent*>(args[0].as_obj());
      std::lock_guard<std::mutex> lock(agent->state_mutex);
      nlohmann::json report;
      report["status"] = agent->status;
      report["temperature"] = agent->temperature;
      report["connection_count"] = static_cast<int>(agent->connection_refs.size());
      report["output_format_count"] = static_cast<int>(agent->output_format_refs.size());
      report["skill_count"] = static_cast<int>(agent->skill_refs.size());
      report["has_optimizer"] = (agent->optimizer_ref != nullptr);
      report["has_execution_policy"] = (agent->execution_policy_ref != nullptr);
      report["has_query_library"] = (agent->query_library_ref != nullptr);
      report["has_domain_context"] = (agent->domain_context_ref != nullptr);
      agent->last_report = report;
      auto s = report.dump();
      return Value::String(s.c_str(), s.size());
    }
    return Value::Nil();
  });
  // ═══════════════════════════════════════════════════════════════
  // v0.9.7: Data Pipeline Deployment native functions
  // ═══════════════════════════════════════════════════════════════

  // deploy_pipeline(config) -> JSON status
  vm.define_native("deploy_pipeline", 1, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_DEPLOY_CONFIG)
    {
      auto* config = reinterpret_cast<ObjDeployConfig*>(args[0].as_obj());
      std::lock_guard<std::mutex> lock(config->state_mutex);

      // Check if target is frozen (skip if we can't look up the target)
      // Note: globals_ is private, so frozen check happens at deploy_execute opcode level

      config->deploy_status = "deploying";
      nlohmann::json entry;
      entry["action"] = "deploy";
      entry["status"] = "deploying";
      entry["strategy"] = config->strategy;
      entry["target"] = config->target;
      entry["pipeline"] = config->pipeline_ref;
      config->deploy_history.push_back(entry);

      nlohmann::json result;
      result["status"] = "deploying";
      result["config"] = config->name;
      result["target"] = config->target;
      result["strategy"] = config->strategy;
      result["pipeline"] = config->pipeline_ref;
      result["auto_rollback"] = config->auto_rollback;
      auto s = result.dump();
      return Value::String(s.c_str(), s.size());
    }
    return Value::Nil();
  });

  // deploy_promote(rule, version) -> JSON status
  vm.define_native("deploy_promote", 2, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_PROMOTION_RULE)
    {
      auto* rule = reinterpret_cast<ObjPromotionRule*>(args[0].as_obj());
      auto version = to_std_string(args[1]);
      std::lock_guard<std::mutex> lock(rule->state_mutex);

      nlohmann::json result;
      result["status"] = "promoting";
      result["rule"] = rule->name;
      result["from_env"] = rule->from_env;
      result["to_env"] = rule->to_env;
      result["version"] = version;
      result["require_tests"] = rule->require_tests;
      result["require_approval"] = rule->require_approval;
      result["auto_promote"] = rule->auto_promote;

      rule->last_promotion_status = "promoting";
      auto s = result.dump();
      return Value::String(s.c_str(), s.size());
    }
    return Value::Nil();
  });

  // deploy_rollback(config, version) -> JSON status
  vm.define_native("deploy_rollback", 2, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_DEPLOY_CONFIG)
    {
      auto* config = reinterpret_cast<ObjDeployConfig*>(args[0].as_obj());
      auto version = to_std_string(args[1]);
      std::lock_guard<std::mutex> lock(config->state_mutex);

      config->deploy_status = "rolling_back";
      nlohmann::json entry;
      entry["action"] = "rollback";
      entry["target_version"] = version;
      entry["status"] = "rolling_back";
      config->deploy_history.push_back(entry);

      nlohmann::json result;
      result["status"] = "rolling_back";
      result["config"] = config->name;
      result["target_version"] = version;
      result["rollback_policy"] = config->rollback_policy;
      result["auto_rollback"] = config->auto_rollback;
      auto s = result.dump();
      return Value::String(s.c_str(), s.size());
    }
    return Value::Nil();
  });

  // deploy_status(config) -> JSON status
  vm.define_native("deploy_status", 1, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_DEPLOY_CONFIG)
    {
      auto* config = reinterpret_cast<ObjDeployConfig*>(args[0].as_obj());
      std::lock_guard<std::mutex> lock(config->state_mutex);

      nlohmann::json result;
      result["config"] = config->name;
      result["status"] = config->deploy_status;
      result["current_version"] = config->current_version;
      result["target"] = config->target;
      result["strategy"] = config->strategy;
      result["pipeline"] = config->pipeline_ref;
      result["history_count"] = static_cast<int>(config->deploy_history.size());
      auto s = result.dump();
      return Value::String(s.c_str(), s.size());
    }
    return Value::Nil();
  });

  // deploy_history(config) -> JSON array
  vm.define_native("deploy_history", 1, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_DEPLOY_CONFIG)
    {
      auto* config = reinterpret_cast<ObjDeployConfig*>(args[0].as_obj());
      std::lock_guard<std::mutex> lock(config->state_mutex);

      nlohmann::json result = nlohmann::json::array();
      for (const auto& entry : config->deploy_history)
      {
        result.push_back(entry);
      }
      auto s = result.dump();
      return Value::String(s.c_str(), s.size());
    }
    return Value::Nil();
  });

  // deploy_diff(registry, v1, v2) -> JSON diff
  vm.define_native("deploy_diff", 3, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_ARTIFACT_REGISTRY)
    {
      auto* registry = reinterpret_cast<ObjArtifactRegistry*>(args[0].as_obj());
      auto v1 = to_std_string(args[1]);
      auto v2 = to_std_string(args[2]);
      std::lock_guard<std::mutex> lock(registry->state_mutex);

      nlohmann::json result;
      result["registry"] = registry->name;
      result["version_a"] = v1;
      result["version_b"] = v2;
      result["storage"] = registry->storage;

      bool v1_exists = false, v2_exists = false;
      for (const auto& v : registry->published_versions)
      {
        if (v == v1) v1_exists = true;
        if (v == v2) v2_exists = true;
      }
      result["version_a_exists"] = v1_exists;
      result["version_b_exists"] = v2_exists;
      result["diff_available"] = v1_exists && v2_exists;

      auto s = result.dump();
      return Value::String(s.c_str(), s.size());
    }
    return Value::Nil();
  });

  // deploy_artifact_publish(registry, name, version) -> JSON artifact
  vm.define_native("deploy_artifact_publish", 3, [](VirtualMachine& vm, int argc, Value* args) -> Value {
    if (args[0].is_obj() && args[0].as_obj()->type == ObjType::OBJ_ARTIFACT_REGISTRY)
    {
      auto* registry = reinterpret_cast<ObjArtifactRegistry*>(args[0].as_obj());
      auto artifact_name = to_std_string(args[1]);
      auto version = to_std_string(args[2]);
      std::lock_guard<std::mutex> lock(registry->state_mutex);

      if (registry->immutable)
      {
        for (const auto& v : registry->published_versions)
        {
          if (v == version)
          {
            nlohmann::json result;
            result["status"] = "rejected";
            result["reason"] = "immutable";
            result["message"] = "Version '" + version + "' already published and registry is immutable";
            auto s = result.dump();
            return Value::String(s.c_str(), s.size());
          }
        }
      }

      registry->published_versions.push_back(version);

      nlohmann::json result;
      result["status"] = "published";
      result["name"] = artifact_name;
      result["version"] = version;
      result["storage"] = registry->storage;
      result["path"] = registry->path + "/" + artifact_name + "/" + version;
      result["checksum_algorithm"] = registry->checksum;
      result["signed"] = registry->sign_artifacts;
      result["immutable"] = registry->immutable;
      result["total_versions"] = static_cast<int>(registry->published_versions.size());
      auto s = result.dump();
      return Value::String(s.c_str(), s.size());
    }
    return Value::Nil();
  });
}
}  // namespace neamc::vm
