// SPDX-License-Identifier: Apache-2.0
/**
 * @file neam_c_api.cpp
 * @brief Implementation of the Neam C API
 */

#include "neamc/neam_c_api.h"

#include <atomic>
#include <cstring>
#include <fstream>
#include <memory>
#include <mutex>
#include <sstream>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "neamc/pipeline.hpp"
#include "neamc/vm/bytecode.hpp"
#include "neamc/vm/native.hpp"
#include "neamc/vm/object.hpp"
#include "neamc/vm/value.hpp"
#include "neamc/vm/vm.hpp"

using namespace neamc;
using namespace neamc::vm;

/* ============================================================================
 * Internal Structures
 * ============================================================================ */

namespace
{

// Version information
constexpr int kVersionMajor = 0;
constexpr int kVersionMinor = 6;
constexpr int kVersionPatch = 0;
constexpr const char* kVersionString = "0.6.0";

// Internal wrapper for NeamValue
struct NeamValueInternal
{
  Value value;
  bool owns_memory{true};

  explicit NeamValueInternal(Value v) : value(std::move(v)) {}
};

// Internal wrapper for NeamError
struct NeamErrorInternal
{
  NeamErrorType type{NEAM_ERROR_NONE};
  std::string message;
  int line{-1};
  int column{-1};
  std::string stack_trace;
  std::string file;
};

// Internal wrapper for NeamResult
struct NeamResultInternal
{
  std::unique_ptr<NeamValueInternal> value;
  std::unique_ptr<NeamErrorInternal> error;
  std::string output;
  bool is_error{false};
};

// Internal wrapper for NeamBytecode
struct NeamBytecodeInternal
{
  Chunk bytecode;
};

// Runtime configuration
struct RuntimeConfig
{
  std::string default_provider{"ollama"};
  std::string default_model{"llama3.2:3b"};
  bool thread_safe{false};
  size_t max_stack_depth{1000};
  bool enable_file_io{true};
  bool enable_network{true};
  bool enable_shell{false};
  std::string working_directory{"."};
};

// External tool registration
struct ExternalTool
{
  std::string name;
  std::string description;
  std::string parameters_schema;
  NeamToolFunc callback;
  void* user_data;
};

// Internal wrapper for NeamRuntime
struct NeamRuntimeInternal
{
  std::unique_ptr<VirtualMachine> vm;
  RuntimeConfig config;
  std::unique_ptr<NeamErrorInternal> last_error;
  std::mutex mutex;  // For thread-safe mode
  std::unordered_map<std::string, ExternalTool> external_tools;
  NeamEmitCallback emit_callback{nullptr};
  void* emit_user_data{nullptr};
  std::stringstream output_buffer;
  bool gc_paused{false};

  NeamRuntimeInternal() : vm(std::make_unique<VirtualMachine>())
  {
    register_core_natives(*vm);
  }

  void set_error(NeamErrorType type, const std::string& msg, int line = -1, int col = -1)
  {
    last_error = std::make_unique<NeamErrorInternal>();
    last_error->type = type;
    last_error->message = msg;
    last_error->line = line;
    last_error->column = col;
  }

  void clear_error() { last_error.reset(); }
};

// Internal wrapper for NeamAgent
struct NeamAgentInternal
{
  NeamRuntimeInternal* runtime;
  std::string name;
  std::string provider;
  std::string model;
  std::string system;
  double temperature{0.7};
  std::vector<std::pair<std::string, std::string>> handoffs;  // target, description
};

// Internal wrapper for NeamRunner
struct NeamRunnerInternal
{
  NeamRuntimeInternal* runtime;
  std::string entry_agent;
  int max_turns;
  bool tracing_enabled{false};
  NeamTraceCallback trace_callback{nullptr};
  void* trace_user_data{nullptr};
  std::string final_agent;
  int total_turns{0};
};

// Internal wrapper for NeamTask
struct NeamTaskInternal
{
  NeamRuntimeInternal* runtime;
  std::string id;
  std::string agent_name;
  nlohmann::json input;
  nlohmann::json result;
  NeamTaskStatus status{NEAM_TASK_PENDING};
  static std::atomic<uint64_t> counter;
};

std::atomic<uint64_t> NeamTaskInternal::counter{0};

// Helper to duplicate a C string
char* duplicate_string(const std::string& s)
{
  char* result = static_cast<char*>(std::malloc(s.size() + 1));
  if (result)
  {
    std::memcpy(result, s.c_str(), s.size() + 1);
  }
  return result;
}

// Convert internal Value to NeamValue*
NeamValue* wrap_value(const Value& v)
{
  auto* internal = new NeamValueInternal(v);
  return reinterpret_cast<NeamValue*>(internal);
}

// Unwrap NeamValue* to internal Value
Value& unwrap_value(NeamValue* v)
{
  auto* internal = reinterpret_cast<NeamValueInternal*>(v);
  return internal->value;
}

// Convert Value to JSON string
std::string value_to_json_impl(const Value& value)
{
  if (value.is_nil())
  {
    return "null";
  }
  if (value.is_bool())
  {
    return value.as_bool() ? "true" : "false";
  }
  if (value.is_number())
  {
    std::ostringstream ss;
    ss << value.as_number();
    return ss.str();
  }
  if (value.is_string())
  {
    nlohmann::json j = as_string(value)->chars;
    return j.dump();
  }
  if (value.is_list())
  {
    nlohmann::json arr = nlohmann::json::array();
    ObjList* list = as_list(value);
    for (const auto& item : list->items)
    {
      arr.push_back(nlohmann::json::parse(value_to_json_impl(item)));
    }
    return arr.dump();
  }
  if (value.is_map())
  {
    nlohmann::json obj = nlohmann::json::object();
    ObjMap* map = as_map(value);
    for (const auto& [key, val] : map->entries)
    {
      obj[key] = nlohmann::json::parse(value_to_json_impl(val));
    }
    return obj.dump();
  }
  return "null";
}

// Convert JSON to Value
Value json_to_value_impl(const nlohmann::json& j)
{
  if (j.is_null())
  {
    return Value::Nil();
  }
  if (j.is_boolean())
  {
    return Value::Bool(j.get<bool>());
  }
  if (j.is_number())
  {
    return Value::Number(j.get<double>());
  }
  if (j.is_string())
  {
    std::string s = j.get<std::string>();
    return Value::String(s.c_str(), s.size());
  }
  if (j.is_array())
  {
    std::vector<Value> items;
    for (const auto& item : j)
    {
      items.push_back(json_to_value_impl(item));
    }
    ObjList* list = new_list(std::move(items));
    return Value::List(list);
  }
  if (j.is_object())
  {
    std::unordered_map<std::string, Value> entries;
    for (const auto& [key, val] : j.items())
    {
      entries[key] = json_to_value_impl(val);
    }
    ObjMap* map = new_map(std::move(entries));
    return Value::Map(map);
  }
  return Value::Nil();
}

}  // namespace

/* ============================================================================
 * Version Information
 * ============================================================================ */

extern "C"
{

  NEAM_API const char* neam_version(void) { return kVersionString; }

  NEAM_API int neam_version_major(void) { return kVersionMajor; }

  NEAM_API int neam_version_minor(void) { return kVersionMinor; }

  NEAM_API int neam_version_patch(void) { return kVersionPatch; }

  /* ============================================================================
   * Runtime Lifecycle
   * ============================================================================ */

  NEAM_API NeamRuntime* neam_runtime_new(void)
  {
    try
    {
      auto* runtime = new NeamRuntimeInternal();
      return reinterpret_cast<NeamRuntime*>(runtime);
    }
    catch (...)
    {
      return nullptr;
    }
  }

  NEAM_API NeamRuntime* neam_runtime_new_with_config(const char* config_json)
  {
    try
    {
      auto* runtime = new NeamRuntimeInternal();

      if (config_json != nullptr)
      {
        nlohmann::json config = nlohmann::json::parse(config_json);

        if (config.contains("default_provider"))
        {
          runtime->config.default_provider = config["default_provider"].get<std::string>();
        }
        if (config.contains("default_model"))
        {
          runtime->config.default_model = config["default_model"].get<std::string>();
        }
        if (config.contains("thread_safe"))
        {
          runtime->config.thread_safe = config["thread_safe"].get<bool>();
        }
        if (config.contains("max_stack_depth"))
        {
          runtime->config.max_stack_depth = config["max_stack_depth"].get<size_t>();
        }
        if (config.contains("enable_file_io"))
        {
          runtime->config.enable_file_io = config["enable_file_io"].get<bool>();
        }
        if (config.contains("enable_network"))
        {
          runtime->config.enable_network = config["enable_network"].get<bool>();
        }
        if (config.contains("enable_shell"))
        {
          runtime->config.enable_shell = config["enable_shell"].get<bool>();
        }
        if (config.contains("working_directory"))
        {
          runtime->config.working_directory = config["working_directory"].get<std::string>();
        }
      }

      return reinterpret_cast<NeamRuntime*>(runtime);
    }
    catch (...)
    {
      return nullptr;
    }
  }

  NEAM_API void neam_runtime_free(NeamRuntime* runtime)
  {
    if (runtime != nullptr)
    {
      delete reinterpret_cast<NeamRuntimeInternal*>(runtime);
    }
  }

  NEAM_API int neam_runtime_is_valid(NeamRuntime* runtime)
  {
    return runtime != nullptr ? 1 : 0;
  }

  NEAM_API void neam_runtime_reset(NeamRuntime* runtime)
  {
    if (runtime == nullptr) return;

    auto* r = reinterpret_cast<NeamRuntimeInternal*>(runtime);
    r->vm = std::make_unique<VirtualMachine>();
    register_core_natives(*r->vm);
    r->clear_error();
    r->output_buffer.str("");
  }

  /* ============================================================================
   * Code Compilation
   * ============================================================================ */

  NEAM_API NeamBytecode* neam_compile(NeamRuntime* runtime, const char* source)
  {
    if (runtime == nullptr || source == nullptr) return nullptr;

    auto* r = reinterpret_cast<NeamRuntimeInternal*>(runtime);

    try
    {
      Pipeline pipeline;
      auto unit = pipeline.compile(source);

      auto* bc = new NeamBytecodeInternal();
      bc->bytecode = std::move(unit.chunk);
      return reinterpret_cast<NeamBytecode*>(bc);
    }
    catch (const std::exception& e)
    {
      r->set_error(NEAM_ERROR_COMPILE, e.what());
      return nullptr;
    }
  }

  NEAM_API NeamBytecode* neam_compile_file(NeamRuntime* runtime, const char* path)
  {
    if (runtime == nullptr || path == nullptr) return nullptr;

    auto* r = reinterpret_cast<NeamRuntimeInternal*>(runtime);

    try
    {
      // Read file content
      std::ifstream file(path);
      if (!file)
      {
        r->set_error(NEAM_ERROR_IO, std::string("Failed to open file: ") + path);
        return nullptr;
      }
      std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

      Pipeline pipeline;
      auto unit = pipeline.compile(source);

      auto* bc = new NeamBytecodeInternal();
      bc->bytecode = std::move(unit.chunk);
      return reinterpret_cast<NeamBytecode*>(bc);
    }
    catch (const std::exception& e)
    {
      r->set_error(NEAM_ERROR_COMPILE, e.what());
      return nullptr;
    }
  }

  NEAM_API void neam_bytecode_free(NeamBytecode* bytecode)
  {
    if (bytecode != nullptr)
    {
      delete reinterpret_cast<NeamBytecodeInternal*>(bytecode);
    }
  }

  NEAM_API uint8_t* neam_bytecode_to_bytes(NeamBytecode* bytecode, size_t* out_size)
  {
    if (bytecode == nullptr || out_size == nullptr) return nullptr;

    auto* bc = reinterpret_cast<NeamBytecodeInternal*>(bytecode);

    // Serialize to a string stream
    std::ostringstream stream;
    bc->bytecode.serialize(stream);
    std::string data = stream.str();

    *out_size = data.size();
    uint8_t* result = static_cast<uint8_t*>(std::malloc(data.size()));
    if (result != nullptr)
    {
      std::memcpy(result, data.data(), data.size());
    }
    return result;
  }

  NEAM_API NeamBytecode* neam_bytecode_from_bytes(const uint8_t* bytes, size_t size)
  {
    if (bytes == nullptr || size == 0) return nullptr;

    try
    {
      auto* bc = new NeamBytecodeInternal();
      std::string data(reinterpret_cast<const char*>(bytes), size);
      std::istringstream stream(data);
      bc->bytecode = Chunk::deserialize(stream);
      return reinterpret_cast<NeamBytecode*>(bc);
    }
    catch (...)
    {
      return nullptr;
    }
  }

  NEAM_API void neam_bytes_free(uint8_t* bytes) { std::free(bytes); }

  /* ============================================================================
   * Code Execution
   * ============================================================================ */

  NEAM_API NeamResult* neam_run(NeamRuntime* runtime, const char* source)
  {
    if (runtime == nullptr || source == nullptr) return nullptr;

    auto* r = reinterpret_cast<NeamRuntimeInternal*>(runtime);
    auto* result = new NeamResultInternal();

    try
    {
      // Compile using Pipeline
      Pipeline pipeline;
      auto unit = pipeline.compile(source);

      // Set up output capture
      std::ostringstream output;
      r->vm->set_io(nullptr, &output);

      // Run
      Value value = r->vm->run(unit.chunk);

      result->value = std::make_unique<NeamValueInternal>(value);
      result->output = output.str();

      // Also capture emitted values
      for (const auto& emitted : r->vm->emitted())
      {
        if (!result->output.empty()) result->output += "\n";
        // Convert emitted value to string
        if (emitted.is_string())
        {
          auto* str = as_string(emitted);
          result->output += std::string(str->chars, str->length);
        }
        else if (emitted.is_number())
        {
          std::ostringstream ss;
          ss << emitted.as_number();
          result->output += ss.str();
        }
        else if (emitted.is_bool())
        {
          result->output += emitted.as_bool() ? "true" : "false";
        }
        else if (emitted.is_nil())
        {
          result->output += "nil";
        }
        else
        {
          result->output += "<object>";
        }
      }
    }
    catch (const std::exception& e)
    {
      result->is_error = true;
      result->error = std::make_unique<NeamErrorInternal>();
      result->error->type = NEAM_ERROR_RUNTIME;
      result->error->message = e.what();
    }

    return reinterpret_cast<NeamResult*>(result);
  }

  NEAM_API NeamResult* neam_run_file(NeamRuntime* runtime, const char* path)
  {
    if (runtime == nullptr || path == nullptr) return nullptr;

    auto* r = reinterpret_cast<NeamRuntimeInternal*>(runtime);
    auto* result = new NeamResultInternal();

    try
    {
      // Read file
      std::ifstream file(path);
      if (!file)
      {
        result->is_error = true;
        result->error = std::make_unique<NeamErrorInternal>();
        result->error->type = NEAM_ERROR_IO;
        result->error->message = std::string("Failed to open file: ") + path;
        return reinterpret_cast<NeamResult*>(result);
      }
      std::string source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

      Pipeline pipeline;
      auto unit = pipeline.compile(source);

      std::ostringstream output;
      r->vm->set_io(nullptr, &output);

      Value value = r->vm->run(unit.chunk);

      result->value = std::make_unique<NeamValueInternal>(value);
      result->output = output.str();
    }
    catch (const std::exception& e)
    {
      result->is_error = true;
      result->error = std::make_unique<NeamErrorInternal>();
      result->error->type = NEAM_ERROR_RUNTIME;
      result->error->message = e.what();
    }

    return reinterpret_cast<NeamResult*>(result);
  }

  NEAM_API NeamResult* neam_run_bytecode(NeamRuntime* runtime, NeamBytecode* bytecode)
  {
    if (runtime == nullptr || bytecode == nullptr) return nullptr;

    auto* r = reinterpret_cast<NeamRuntimeInternal*>(runtime);
    auto* bc = reinterpret_cast<NeamBytecodeInternal*>(bytecode);
    auto* result = new NeamResultInternal();

    try
    {
      std::ostringstream output;
      r->vm->set_io(nullptr, &output);

      Value value = r->vm->run(bc->bytecode);

      result->value = std::make_unique<NeamValueInternal>(value);
      result->output = output.str();
    }
    catch (const std::exception& e)
    {
      result->is_error = true;
      result->error = std::make_unique<NeamErrorInternal>();
      result->error->type = NEAM_ERROR_RUNTIME;
      result->error->message = e.what();
    }

    return reinterpret_cast<NeamResult*>(result);
  }

  NEAM_API NeamResult* neam_run_bytecode_bytes(NeamRuntime* runtime, const uint8_t* bytes,
                                               size_t size)
  {
    NeamBytecode* bc = neam_bytecode_from_bytes(bytes, size);
    if (bc == nullptr)
    {
      auto* result = new NeamResultInternal();
      result->is_error = true;
      result->error = std::make_unique<NeamErrorInternal>();
      result->error->type = NEAM_ERROR_COMPILE;
      result->error->message = "Failed to deserialize bytecode";
      return reinterpret_cast<NeamResult*>(result);
    }

    NeamResult* result = neam_run_bytecode(runtime, bc);
    neam_bytecode_free(bc);
    return result;
  }

  /* ============================================================================
   * Result Handling
   * ============================================================================ */

  NEAM_API int neam_result_is_error(NeamResult* result)
  {
    if (result == nullptr) return 1;
    return reinterpret_cast<NeamResultInternal*>(result)->is_error ? 1 : 0;
  }

  NEAM_API const char* neam_result_output(NeamResult* result)
  {
    if (result == nullptr) return nullptr;
    return reinterpret_cast<NeamResultInternal*>(result)->output.c_str();
  }

  NEAM_API NeamValue* neam_result_value(NeamResult* result)
  {
    if (result == nullptr) return nullptr;
    auto* r = reinterpret_cast<NeamResultInternal*>(result);
    if (r->is_error || !r->value) return nullptr;

    // Create a copy for the caller
    return wrap_value(r->value->value);
  }

  NEAM_API NeamError* neam_result_error(NeamResult* result)
  {
    if (result == nullptr) return nullptr;
    auto* r = reinterpret_cast<NeamResultInternal*>(result);
    return reinterpret_cast<NeamError*>(r->error.get());
  }

  NEAM_API void neam_result_free(NeamResult* result)
  {
    if (result != nullptr)
    {
      delete reinterpret_cast<NeamResultInternal*>(result);
    }
  }

  /* ============================================================================
   * Value Creation
   * ============================================================================ */

  NEAM_API NeamValue* neam_value_nil(void) { return wrap_value(Value::Nil()); }

  NEAM_API NeamValue* neam_value_bool(int value) { return wrap_value(Value::Bool(value != 0)); }

  NEAM_API NeamValue* neam_value_number(double value) { return wrap_value(Value::Number(value)); }

  NEAM_API NeamValue* neam_value_string(const char* value, int length)
  {
    if (value == nullptr) return wrap_value(Value::Nil());
    size_t len = length < 0 ? std::strlen(value) : static_cast<size_t>(length);
    return wrap_value(Value::String(value, len));
  }

  NEAM_API NeamValue* neam_value_list_new(void)
  {
    ObjList* list = new_list({});
    return wrap_value(Value::List(list));
  }

  NEAM_API void neam_value_list_append(NeamValue* list, NeamValue* item)
  {
    if (list == nullptr || item == nullptr) return;
    Value& list_val = unwrap_value(list);
    if (!list_val.is_list()) return;

    ObjList* obj_list = as_list(list_val);
    obj_list->items.push_back(unwrap_value(item));
  }

  NEAM_API NeamValue* neam_value_map_new(void)
  {
    ObjMap* map = new_map({});
    return wrap_value(Value::Map(map));
  }

  NEAM_API void neam_value_map_set(NeamValue* map, const char* key, NeamValue* value)
  {
    if (map == nullptr || key == nullptr || value == nullptr) return;
    Value& map_val = unwrap_value(map);
    if (!map_val.is_map()) return;

    ObjMap* obj_map = as_map(map_val);
    obj_map->entries[key] = unwrap_value(value);
  }

  /* ============================================================================
   * Value Access
   * ============================================================================ */

  NEAM_API NeamValueType neam_value_type(NeamValue* value)
  {
    if (value == nullptr) return NEAM_TYPE_NIL;
    const Value& v = unwrap_value(value);

    if (v.is_nil()) return NEAM_TYPE_NIL;
    if (v.is_bool()) return NEAM_TYPE_BOOL;
    if (v.is_number()) return NEAM_TYPE_NUMBER;
    if (v.is_string()) return NEAM_TYPE_STRING;
    if (v.is_list()) return NEAM_TYPE_LIST;
    if (v.is_map()) return NEAM_TYPE_MAP;
    if (v.is_function()) return NEAM_TYPE_FUNCTION;
    if (v.is_agent()) return NEAM_TYPE_AGENT;
    return NEAM_TYPE_OBJECT;
  }

  NEAM_API int neam_value_as_bool(NeamValue* value)
  {
    if (value == nullptr) return 0;
    const Value& v = unwrap_value(value);
    if (v.is_bool()) return v.as_bool() ? 1 : 0;
    return 0;
  }

  NEAM_API double neam_value_as_number(NeamValue* value)
  {
    if (value == nullptr) return 0.0;
    const Value& v = unwrap_value(value);
    if (v.is_number()) return v.as_number();
    return 0.0;
  }

  NEAM_API const char* neam_value_as_string(NeamValue* value)
  {
    if (value == nullptr) return nullptr;
    const Value& v = unwrap_value(value);
    if (!v.is_string()) return nullptr;
    return as_string(v)->chars;
  }

  NEAM_API size_t neam_value_string_length(NeamValue* value)
  {
    if (value == nullptr) return 0;
    const Value& v = unwrap_value(value);
    if (!v.is_string()) return 0;
    return as_string(v)->length;
  }

  NEAM_API size_t neam_value_list_length(NeamValue* value)
  {
    if (value == nullptr) return 0;
    const Value& v = unwrap_value(value);
    if (!v.is_list()) return 0;
    return as_list(v)->items.size();
  }

  NEAM_API NeamValue* neam_value_list_get(NeamValue* value, size_t index)
  {
    if (value == nullptr) return nullptr;
    const Value& v = unwrap_value(value);
    if (!v.is_list()) return nullptr;

    ObjList* list = as_list(v);
    if (index >= list->items.size()) return nullptr;

    return wrap_value(list->items[index]);
  }

  NEAM_API NeamValue* neam_value_map_get(NeamValue* value, const char* key)
  {
    if (value == nullptr || key == nullptr) return nullptr;
    const Value& v = unwrap_value(value);
    if (!v.is_map()) return nullptr;

    ObjMap* map = as_map(v);
    auto it = map->entries.find(key);
    if (it == map->entries.end()) return nullptr;

    return wrap_value(it->second);
  }

  NEAM_API int neam_value_map_has(NeamValue* value, const char* key)
  {
    if (value == nullptr || key == nullptr) return 0;
    const Value& v = unwrap_value(value);
    if (!v.is_map()) return 0;

    ObjMap* map = as_map(v);
    return map->entries.count(key) > 0 ? 1 : 0;
  }

  NEAM_API const char** neam_value_map_keys(NeamValue* value, size_t* out_count)
  {
    if (value == nullptr || out_count == nullptr) return nullptr;
    const Value& v = unwrap_value(value);
    if (!v.is_map())
    {
      *out_count = 0;
      return nullptr;
    }

    ObjMap* map = as_map(v);
    *out_count = map->entries.size();

    if (*out_count == 0) return nullptr;

    const char** keys = static_cast<const char**>(std::malloc(*out_count * sizeof(const char*)));
    if (keys == nullptr) return nullptr;

    size_t i = 0;
    for (const auto& [key, _] : map->entries)
    {
      keys[i++] = key.c_str();
    }

    return keys;
  }

  NEAM_API char* neam_value_to_json(NeamValue* value)
  {
    if (value == nullptr) return duplicate_string("null");
    return duplicate_string(value_to_json_impl(unwrap_value(value)));
  }

  NEAM_API NeamValue* neam_value_from_json(const char* json)
  {
    if (json == nullptr) return wrap_value(Value::Nil());

    try
    {
      nlohmann::json j = nlohmann::json::parse(json);
      return wrap_value(json_to_value_impl(j));
    }
    catch (...)
    {
      return nullptr;
    }
  }

  NEAM_API void neam_value_free(NeamValue* value)
  {
    if (value != nullptr)
    {
      delete reinterpret_cast<NeamValueInternal*>(value);
    }
  }

  NEAM_API void neam_string_free(char* str) { std::free(str); }

  /* ============================================================================
   * Global Variable Access
   * ============================================================================ */

  NEAM_API NeamValue* neam_get_global(NeamRuntime* runtime, const char* name)
  {
    if (runtime == nullptr || name == nullptr) return nullptr;

    auto* r = reinterpret_cast<NeamRuntimeInternal*>(runtime);
    Table& globals = r->vm->globals();

    // Create ObjString key
    ObjString* key = copy_string(name, std::strlen(name));
    Value value;
    if (globals.get(key, &value))
    {
      return wrap_value(value);
    }
    return nullptr;
  }

  NEAM_API int neam_set_global(NeamRuntime* runtime, const char* name, NeamValue* value)
  {
    if (runtime == nullptr || name == nullptr || value == nullptr) return 1;

    auto* r = reinterpret_cast<NeamRuntimeInternal*>(runtime);
    ObjString* key = copy_string(name, std::strlen(name));
    r->vm->globals().set(key, unwrap_value(value));
    return 0;
  }

  NEAM_API int neam_call_function(NeamRuntime* runtime, const char* name, int arg_count,
                                  NeamValue** args, NeamValue** out_result)
  {
    if (runtime == nullptr || name == nullptr) return 1;

    auto* r = reinterpret_cast<NeamRuntimeInternal*>(runtime);

    // Build a Neam expression to call the function with arguments
    std::ostringstream code;
    code << "{ emit " << name << "(";
    for (int i = 0; i < arg_count; ++i)
    {
      if (i > 0) code << ", ";
      if (args && args[i])
      {
        const Value& v = unwrap_value(args[i]);
        if (v.is_number()) code << v.as.number;
        else if (v.is_bool()) code << (v.as_bool() ? "true" : "false");
        else if (v.is_obj() && v.as.obj->type == ObjType::OBJ_STRING)
        {
          auto* s = reinterpret_cast<ObjString*>(v.as.obj);
          code << "\"" << std::string(s->chars, s->length) << "\"";
        }
        else code << "nil";
      }
      else code << "nil";
    }
    code << "); }\n";

    NeamResult* result = neam_run(runtime, code.str().c_str());
    if (result == nullptr) return 1;

    if (neam_result_is_error(result))
    {
      neam_result_free(result);
      return 1;
    }

    if (out_result)
    {
      const char* output = neam_result_output(result);
      if (output)
      {
        *out_result = reinterpret_cast<NeamValue*>(
            new NeamValueInternal(Value::ObjVal(reinterpret_cast<Obj*>(
                copy_string(output, std::strlen(output))))));
      }
      else
      {
        *out_result = nullptr;
      }
    }

    neam_result_free(result);
    return 0;
  }

  /* ============================================================================
   * Tool Registration
   * ============================================================================ */

  NEAM_API int neam_register_tool(NeamRuntime* runtime, const char* name, const char* description,
                                  NeamToolFunc func, void* user_data)
  {
    return neam_register_tool_with_schema(runtime, name, description, nullptr, func, user_data);
  }

  NEAM_API int neam_register_tool_with_schema(NeamRuntime* runtime, const char* name,
                                              const char* description,
                                              const char* parameters_schema, NeamToolFunc func,
                                              void* user_data)
  {
    if (runtime == nullptr || name == nullptr || func == nullptr) return 1;

    auto* r = reinterpret_cast<NeamRuntimeInternal*>(runtime);

    ExternalTool tool;
    tool.name = name;
    tool.description = description ? description : "";
    tool.parameters_schema = parameters_schema ? parameters_schema : "";
    tool.callback = func;
    tool.user_data = user_data;

    r->external_tools[name] = std::move(tool);

    // Also register as native function in VM
    // (This would need additional work to actually call back to the C callback)

    return 0;
  }

  NEAM_API int neam_unregister_tool(NeamRuntime* runtime, const char* name)
  {
    if (runtime == nullptr || name == nullptr) return 1;

    auto* r = reinterpret_cast<NeamRuntimeInternal*>(runtime);
    return r->external_tools.erase(name) > 0 ? 0 : 1;
  }

  NEAM_API int neam_has_tool(NeamRuntime* runtime, const char* name)
  {
    if (runtime == nullptr || name == nullptr) return 0;

    auto* r = reinterpret_cast<NeamRuntimeInternal*>(runtime);
    return r->external_tools.count(name) > 0 ? 1 : 0;
  }

  /* ============================================================================
   * Agent API
   * ============================================================================ */

  NEAM_API NeamAgent* neam_agent_create(NeamRuntime* runtime, const char* name,
                                        const char* provider, const char* model,
                                        const char* system)
  {
    if (runtime == nullptr || name == nullptr) return nullptr;

    auto* r = reinterpret_cast<NeamRuntimeInternal*>(runtime);
    auto* agent = new NeamAgentInternal();

    agent->runtime = r;
    agent->name = name;
    agent->provider = provider ? provider : r->config.default_provider;
    agent->model = model ? model : r->config.default_model;
    agent->system = system ? system : "";

    return reinterpret_cast<NeamAgent*>(agent);
  }

  NEAM_API void neam_agent_set_temperature(NeamAgent* agent, double temperature)
  {
    if (agent == nullptr) return;
    reinterpret_cast<NeamAgentInternal*>(agent)->temperature = temperature;
  }

  NEAM_API int neam_agent_set_context_from(NeamAgent* agent, const char* path)
  {
    if (agent == nullptr || path == nullptr) return 1;

    auto* a = reinterpret_cast<NeamAgentInternal*>(agent);

    // Load context from file
    std::ifstream file(path);
    if (!file.is_open()) return 1;

    std::ostringstream content;
    content << file.rdbuf();
    a->system = content.str();

    return 0;
  }

  NEAM_API char* neam_agent_ask(NeamAgent* agent, const char* query)
  {
    if (agent == nullptr || query == nullptr) return nullptr;

    auto* a = reinterpret_cast<NeamAgentInternal*>(agent);

    // Build Neam code to call the agent
    std::ostringstream code;
    code << "agent " << a->name << " {\n";
    code << "  provider: \"" << a->provider << "\"\n";
    code << "  model: \"" << a->model << "\"\n";
    if (!a->system.empty())
    {
      code << "  system: \"" << a->system << "\"\n";
    }
    code << "  temperature: " << a->temperature << "\n";
    code << "}\n";
    code << "{ emit " << a->name << ".ask(\"" << query << "\"); }\n";

    NeamResult* result = neam_run(reinterpret_cast<NeamRuntime*>(a->runtime), code.str().c_str());
    if (result == nullptr) return nullptr;

    if (neam_result_is_error(result))
    {
      neam_result_free(result);
      return nullptr;
    }

    const char* output = neam_result_output(result);
    char* response = output ? duplicate_string(output) : nullptr;

    neam_result_free(result);
    return response;
  }

  NEAM_API int neam_agent_add_handoff(NeamAgent* agent, const char* target_name,
                                      const char* tool_name, const char* description)
  {
    if (agent == nullptr || target_name == nullptr) return 1;

    auto* a = reinterpret_cast<NeamAgentInternal*>(agent);
    a->handoffs.emplace_back(target_name, description ? description : "");
    (void)tool_name;  // TODO: Use tool_name

    return 0;
  }

  NEAM_API void neam_agent_reset(NeamAgent* agent)
  {
    if (agent == nullptr) return;
    auto* a = reinterpret_cast<NeamAgentInternal*>(agent);
    a->system.clear();
    a->handoffs.clear();
    a->temperature = 0.7;
  }

  NEAM_API void neam_agent_free(NeamAgent* agent)
  {
    if (agent != nullptr)
    {
      delete reinterpret_cast<NeamAgentInternal*>(agent);
    }
  }

  /* ============================================================================
   * Runner API
   * ============================================================================ */

  NEAM_API NeamRunner* neam_runner_create(NeamRuntime* runtime, const char* entry_agent,
                                          int max_turns)
  {
    if (runtime == nullptr || entry_agent == nullptr) return nullptr;

    auto* runner = new NeamRunnerInternal();
    runner->runtime = reinterpret_cast<NeamRuntimeInternal*>(runtime);
    runner->entry_agent = entry_agent;
    runner->max_turns = max_turns > 0 ? max_turns : 10;

    return reinterpret_cast<NeamRunner*>(runner);
  }

  NEAM_API void neam_runner_enable_tracing(NeamRunner* runner, int enabled)
  {
    if (runner == nullptr) return;
    reinterpret_cast<NeamRunnerInternal*>(runner)->tracing_enabled = enabled != 0;
  }

  NEAM_API void neam_runner_set_trace_callback(NeamRunner* runner, NeamTraceCallback callback,
                                               void* user_data)
  {
    if (runner == nullptr) return;
    auto* r = reinterpret_cast<NeamRunnerInternal*>(runner);
    r->trace_callback = callback;
    r->trace_user_data = user_data;
  }

  NEAM_API NeamResult* neam_runner_run(NeamRunner* runner, const char* input)
  {
    if (runner == nullptr || input == nullptr) return nullptr;

    auto* r = reinterpret_cast<NeamRunnerInternal*>(runner);
    auto* result = new NeamResultInternal();

    try
    {
      // Use VM's run_agent_loop
      Value input_val = Value::String(input, std::strlen(input));
      auto loop_result = r->runtime->vm->run_agent_loop(r->entry_agent, input_val,
                                                        static_cast<size_t>(r->max_turns));

      r->final_agent = loop_result.final_agent;
      r->total_turns = static_cast<int>(loop_result.total_turns);

      result->value = std::make_unique<NeamValueInternal>(loop_result.final_output);
      // Convert output to string
      const Value& out_val = loop_result.final_output;
      if (out_val.is_string())
      {
        auto* str = as_string(out_val);
        result->output = std::string(str->chars, str->length);
      }
      else if (out_val.is_number())
      {
        std::ostringstream ss;
        ss << out_val.as_number();
        result->output = ss.str();
      }
      else if (out_val.is_bool())
      {
        result->output = out_val.as_bool() ? "true" : "false";
      }
      else
      {
        result->output = "";
      }

      if (!loop_result.error_message.empty())
      {
        result->is_error = true;
        result->error = std::make_unique<NeamErrorInternal>();
        result->error->type = NEAM_ERROR_AGENT;
        result->error->message = loop_result.error_message;
      }
    }
    catch (const std::exception& e)
    {
      result->is_error = true;
      result->error = std::make_unique<NeamErrorInternal>();
      result->error->type = NEAM_ERROR_RUNTIME;
      result->error->message = e.what();
    }

    return reinterpret_cast<NeamResult*>(result);
  }

  NEAM_API const char* neam_runner_final_agent(NeamRunner* runner)
  {
    if (runner == nullptr) return nullptr;
    return reinterpret_cast<NeamRunnerInternal*>(runner)->final_agent.c_str();
  }

  NEAM_API int neam_runner_total_turns(NeamRunner* runner)
  {
    if (runner == nullptr) return 0;
    return reinterpret_cast<NeamRunnerInternal*>(runner)->total_turns;
  }

  NEAM_API void neam_runner_free(NeamRunner* runner)
  {
    if (runner != nullptr)
    {
      delete reinterpret_cast<NeamRunnerInternal*>(runner);
    }
  }

  /* ============================================================================
   * Task API
   * ============================================================================ */

  NEAM_API NeamTask* neam_task_create(NeamRuntime* runtime, const char* agent_name,
                                      const char* input_json)
  {
    if (runtime == nullptr || agent_name == nullptr) return nullptr;

    auto* task = new NeamTaskInternal();
    task->runtime = reinterpret_cast<NeamRuntimeInternal*>(runtime);
    task->id = "task_" + std::to_string(NeamTaskInternal::counter.fetch_add(1));
    task->agent_name = agent_name;

    if (input_json != nullptr)
    {
      try
      {
        task->input = nlohmann::json::parse(input_json);
      }
      catch (...)
      {
        task->input = nlohmann::json::object();
      }
    }

    return reinterpret_cast<NeamTask*>(task);
  }

  NEAM_API const char* neam_task_id(NeamTask* task)
  {
    if (task == nullptr) return nullptr;
    return reinterpret_cast<NeamTaskInternal*>(task)->id.c_str();
  }

  NEAM_API int neam_task_submit(NeamTask* task)
  {
    if (task == nullptr) return 1;

    auto* t = reinterpret_cast<NeamTaskInternal*>(task);
    t->status = NEAM_TASK_RUNNING;

    // Execute the task synchronously via neam_agent_ask
    NeamAgent* agent_handle = reinterpret_cast<NeamAgent*>(
        new NeamAgentInternal{t->runtime, t->agent_name,
                              t->runtime->config.default_provider,
                              t->runtime->config.default_model,
                              "", 0.7, {}});

    std::string query = t->input.is_string() ? t->input.get<std::string>() : t->input.dump();
    char* response = neam_agent_ask(agent_handle, query.c_str());

    if (response)
    {
      t->result = nlohmann::json{{"output", response}};
      t->status = NEAM_TASK_COMPLETED;
      std::free(response);
    }
    else
    {
      t->result = nlohmann::json{{"error", "Task execution failed"}};
      t->status = NEAM_TASK_FAILED;
    }

    neam_agent_free(agent_handle);
    return 0;
  }

  NEAM_API NeamTaskStatus neam_task_status(NeamTask* task)
  {
    if (task == nullptr) return NEAM_TASK_FAILED;
    return reinterpret_cast<NeamTaskInternal*>(task)->status;
  }

  NEAM_API int neam_task_wait(NeamTask* task, int timeout_ms)
  {
    if (task == nullptr) return 1;
    (void)timeout_ms;

    auto* t = reinterpret_cast<NeamTaskInternal*>(task);

    // Since task_submit runs synchronously, the task is already done
    if (t->status == NEAM_TASK_COMPLETED || t->status == NEAM_TASK_FAILED)
    {
      return 0;
    }

    // Task hasn't been submitted yet
    return 1;
  }

  NEAM_API char* neam_task_result(NeamTask* task)
  {
    if (task == nullptr) return nullptr;
    auto* t = reinterpret_cast<NeamTaskInternal*>(task);
    return duplicate_string(t->result.dump());
  }

  NEAM_API int neam_task_cancel(NeamTask* task)
  {
    if (task == nullptr) return 1;
    reinterpret_cast<NeamTaskInternal*>(task)->status = NEAM_TASK_CANCELLED;
    return 0;
  }

  NEAM_API void neam_task_free(NeamTask* task)
  {
    if (task != nullptr)
    {
      delete reinterpret_cast<NeamTaskInternal*>(task);
    }
  }

  /* ============================================================================
   * Agent Card API
   * ============================================================================ */

  NEAM_API char* neam_get_agent_card(NeamRuntime* runtime, const char* agent_name)
  {
    if (runtime == nullptr || agent_name == nullptr) return nullptr;

    auto* r = reinterpret_cast<NeamRuntimeInternal*>(runtime);
    nlohmann::json card = r->vm->get_agent_card_json(agent_name);

    if (card.is_null())
    {
      return nullptr;
    }

    return duplicate_string(card.dump());
  }

  NEAM_API int neam_register_agent_card(NeamRuntime* runtime, const char* agent_name,
                                        const char* card_json)
  {
    if (runtime == nullptr || agent_name == nullptr || card_json == nullptr) return 1;

    try
    {
      // Parse JSON and store - the actual card registration would be done via Neam code
      // For now, we just validate the JSON is parseable
      nlohmann::json card = nlohmann::json::parse(card_json);
      (void)agent_name;  // Agent cards are typically defined in Neam code
      (void)card;        // Just validate it's valid JSON
      return 0;
    }
    catch (...)
    {
      return 1;
    }
  }

  /* ============================================================================
   * Error Handling
   * ============================================================================ */

  NEAM_API NeamError* neam_get_last_error(NeamRuntime* runtime)
  {
    if (runtime == nullptr) return nullptr;
    auto* r = reinterpret_cast<NeamRuntimeInternal*>(runtime);
    return reinterpret_cast<NeamError*>(r->last_error.get());
  }

  NEAM_API NeamErrorType neam_error_type(NeamError* error)
  {
    if (error == nullptr) return NEAM_ERROR_NONE;
    return reinterpret_cast<NeamErrorInternal*>(error)->type;
  }

  NEAM_API const char* neam_error_message(NeamError* error)
  {
    if (error == nullptr) return nullptr;
    return reinterpret_cast<NeamErrorInternal*>(error)->message.c_str();
  }

  NEAM_API int neam_error_line(NeamError* error)
  {
    if (error == nullptr) return -1;
    return reinterpret_cast<NeamErrorInternal*>(error)->line;
  }

  NEAM_API int neam_error_column(NeamError* error)
  {
    if (error == nullptr) return -1;
    return reinterpret_cast<NeamErrorInternal*>(error)->column;
  }

  NEAM_API const char* neam_error_stack_trace(NeamError* error)
  {
    if (error == nullptr) return nullptr;
    return reinterpret_cast<NeamErrorInternal*>(error)->stack_trace.c_str();
  }

  NEAM_API char* neam_error_to_json(NeamError* error)
  {
    if (error == nullptr) return duplicate_string("null");

    auto* e = reinterpret_cast<NeamErrorInternal*>(error);
    nlohmann::json j = {{"type", static_cast<int>(e->type)},
                        {"message", e->message},
                        {"line", e->line},
                        {"column", e->column},
                        {"stack_trace", e->stack_trace}};

    return duplicate_string(j.dump());
  }

  NEAM_API void neam_clear_error(NeamRuntime* runtime)
  {
    if (runtime == nullptr) return;
    reinterpret_cast<NeamRuntimeInternal*>(runtime)->clear_error();
  }

  /* ============================================================================
   * Callback Registration
   * ============================================================================ */

  NEAM_API void neam_set_emit_callback(NeamRuntime* runtime, NeamEmitCallback callback,
                                       void* user_data)
  {
    if (runtime == nullptr) return;
    auto* r = reinterpret_cast<NeamRuntimeInternal*>(runtime);
    r->emit_callback = callback;
    r->emit_user_data = user_data;
  }

  /* ============================================================================
   * Memory Management
   * ============================================================================ */

  NEAM_API void neam_gc_pause(NeamRuntime* runtime)
  {
    if (runtime == nullptr) return;
    reinterpret_cast<NeamRuntimeInternal*>(runtime)->gc_paused = true;
  }

  NEAM_API void neam_gc_resume(NeamRuntime* runtime)
  {
    if (runtime == nullptr) return;
    reinterpret_cast<NeamRuntimeInternal*>(runtime)->gc_paused = false;
  }

  NEAM_API void neam_gc_collect(NeamRuntime* runtime)
  {
    if (runtime == nullptr) return;
    auto* r = reinterpret_cast<NeamRuntimeInternal*>(runtime);
    if (r->gc_paused) return;
    neamc::vm::collect_garbage(*r->vm);
  }

  NEAM_API void neam_gc_stats(NeamRuntime* runtime, size_t* out_heap_size, size_t* out_heap_used,
                              size_t* out_object_count)
  {
    if (runtime == nullptr)
    {
      if (out_heap_size) *out_heap_size = 0;
      if (out_heap_used) *out_heap_used = 0;
      if (out_object_count) *out_object_count = 0;
      return;
    }

    if (out_heap_size) *out_heap_size = neamc::vm::next_gc;
    if (out_heap_used) *out_heap_used = neamc::vm::bytes_allocated;

    if (out_object_count)
    {
      size_t count = 0;
      Obj* obj = neamc::vm::objects;
      while (obj)
      {
        ++count;
        obj = obj->next;
      }
      *out_object_count = count;
    }
  }

}  // extern "C"
