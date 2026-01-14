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
#include "neamc/vm/runtime_type.hpp"
#include "neamc/vm/vm.hpp"

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
}
}  // namespace neamc::vm
