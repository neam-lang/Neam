//
// Neam Virtual Machine - Native function registry
//

#include "neamc/vm/native.hpp"

#include <chrono>
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
std::string to_std_string(const Value& value)
{
  if (!value.is_string())
  {
    throw std::runtime_error("Expected string value");
  }
  auto* str = as_string(value);
  return std::string(str->chars, str->length);
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
  vm.define_native("file_read_string", 1, file_read_string_native);
  vm.define_native("file_write_string", 2, file_write_string_native);
  vm.define_native("http_get", 1, http_get_native);
}
}  // namespace neamc::vm
