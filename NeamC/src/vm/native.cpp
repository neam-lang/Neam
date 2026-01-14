//
// Neam Virtual Machine - Native function registry
//

#include "neamc/vm/native.hpp"

#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <sstream>
#include <unordered_map>

#include <curl/curl.h>

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

Value make_result_bool(bool value)
{
  return Value::Bool(value);
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
}
}  // namespace neamc::vm
