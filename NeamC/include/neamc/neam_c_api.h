// SPDX-License-Identifier: Apache-2.0
/**
 * @file neam_c_api.h
 * @brief Neam C API - Public interface for embedding Neam in other languages
 *
 * This header provides a stable C ABI for embedding the Neam runtime in applications
 * written in C, Python, JavaScript, Rust, Go, and other languages that support
 * C foreign function interfaces (FFI).
 *
 * Thread Safety:
 * - By default, the runtime is single-threaded. A single runtime instance should
 *   only be used from one thread at a time.
 * - Use neam_runtime_new_with_config() with thread_safe=true for multi-threaded use.
 *
 * Memory Management:
 * - All neam_*_new() or neam_*_create() functions return owned pointers that must
 *   be freed with the corresponding neam_*_free() function.
 * - Strings returned by neam_*_as_string() are owned by the value and should not
 *   be freed directly. Copy them if needed beyond the value's lifetime.
 *
 * Error Handling:
 * - Functions that can fail return NULL or a non-zero error code.
 * - Use neam_get_last_error() to retrieve detailed error information.
 *
 * @version 0.6.0
 */

#ifndef NEAM_C_API_H
#define NEAM_C_API_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ============================================================================
 * Platform Detection and Export Macros
 * ============================================================================ */

#if defined(_WIN32) || defined(__CYGWIN__)
  #ifdef NEAM_BUILDING_DLL
    #define NEAM_API __declspec(dllexport)
  #else
    #define NEAM_API __declspec(dllimport)
  #endif
#elif defined(__GNUC__) && __GNUC__ >= 4
  #define NEAM_API __attribute__((visibility("default")))
#else
  #define NEAM_API
#endif

/* ============================================================================
 * Opaque Handle Types
 * ============================================================================ */

/** @brief Opaque handle to a Neam runtime instance */
typedef struct NeamRuntime NeamRuntime;

/** @brief Opaque handle to an execution result */
typedef struct NeamResult NeamResult;

/** @brief Opaque handle to a Neam value */
typedef struct NeamValue NeamValue;

/** @brief Opaque handle to an error */
typedef struct NeamError NeamError;

/** @brief Opaque handle to an agent */
typedef struct NeamAgent NeamAgent;

/** @brief Opaque handle to a runner (agent loop) */
typedef struct NeamRunner NeamRunner;

/** @brief Opaque handle to a task */
typedef struct NeamTask NeamTask;

/** @brief Opaque handle to bytecode bundle */
typedef struct NeamBytecode NeamBytecode;

/* ============================================================================
 * Value Type Enumeration
 * ============================================================================ */

/**
 * @brief Enumeration of Neam value types
 */
typedef enum NeamValueType {
  NEAM_TYPE_NIL = 0,
  NEAM_TYPE_BOOL = 1,
  NEAM_TYPE_NUMBER = 2,
  NEAM_TYPE_STRING = 3,
  NEAM_TYPE_LIST = 4,
  NEAM_TYPE_MAP = 5,
  NEAM_TYPE_FUNCTION = 6,
  NEAM_TYPE_AGENT = 7,
  NEAM_TYPE_OBJECT = 8
} NeamValueType;

/* ============================================================================
 * Error Type Enumeration
 * ============================================================================ */

/**
 * @brief Enumeration of error types
 */
typedef enum NeamErrorType {
  NEAM_ERROR_NONE = 0,
  NEAM_ERROR_PARSE = 1,
  NEAM_ERROR_COMPILE = 2,
  NEAM_ERROR_RUNTIME = 3,
  NEAM_ERROR_TOOL = 4,
  NEAM_ERROR_TIMEOUT = 5,
  NEAM_ERROR_BUDGET = 6,
  NEAM_ERROR_AGENT = 7,
  NEAM_ERROR_MEMORY = 8,
  NEAM_ERROR_IO = 9,
  NEAM_ERROR_INVALID_ARGUMENT = 10
} NeamErrorType;

/* ============================================================================
 * Task Status Enumeration
 * ============================================================================ */

/**
 * @brief Enumeration of task statuses (A2A Protocol)
 */
typedef enum NeamTaskStatus {
  NEAM_TASK_PENDING = 0,
  NEAM_TASK_RUNNING = 1,
  NEAM_TASK_COMPLETED = 2,
  NEAM_TASK_FAILED = 3,
  NEAM_TASK_CANCELLED = 4
} NeamTaskStatus;

/* ============================================================================
 * Callback Function Types
 * ============================================================================ */

/**
 * @brief Callback for tool implementation
 *
 * @param runtime The runtime context
 * @param arg_count Number of arguments
 * @param args Array of argument values
 * @param user_data User-provided context pointer
 * @return Result value (caller takes ownership)
 */
typedef NeamValue* (*NeamToolFunc)(
    NeamRuntime* runtime,
    int arg_count,
    NeamValue** args,
    void* user_data
);

/**
 * @brief Callback for output (emit statements)
 *
 * @param message The emitted message
 * @param user_data User-provided context pointer
 */
typedef void (*NeamEmitCallback)(const char* message, void* user_data);

/**
 * @brief Callback for trace events
 *
 * @param agent_name Name of the agent
 * @param action The action taken ("response", "handoff", "tool_call")
 * @param message The content
 * @param user_data User-provided context pointer
 */
typedef void (*NeamTraceCallback)(
    const char* agent_name,
    const char* action,
    const char* message,
    void* user_data
);

/* ============================================================================
 * Version Information
 * ============================================================================ */

/**
 * @brief Get the Neam runtime version string
 * @return Version string (e.g., "0.3.0")
 */
NEAM_API const char* neam_version(void);

/**
 * @brief Get the major version number
 * @return Major version
 */
NEAM_API int neam_version_major(void);

/**
 * @brief Get the minor version number
 * @return Minor version
 */
NEAM_API int neam_version_minor(void);

/**
 * @brief Get the patch version number
 * @return Patch version
 */
NEAM_API int neam_version_patch(void);

/* ============================================================================
 * Runtime Lifecycle
 * ============================================================================ */

/**
 * @brief Create a new Neam runtime with default settings
 * @return New runtime handle, or NULL on failure
 */
NEAM_API NeamRuntime* neam_runtime_new(void);

/**
 * @brief Create a new Neam runtime with JSON configuration
 *
 * Configuration options (all optional):
 * - "default_provider": string - Default LLM provider ("ollama", "openai")
 * - "default_model": string - Default model name
 * - "thread_safe": bool - Enable thread-safe mode
 * - "max_stack_depth": int - Maximum call stack depth
 * - "enable_file_io": bool - Allow file operations
 * - "enable_network": bool - Allow network operations
 * - "enable_shell": bool - Allow shell command execution
 * - "working_directory": string - Working directory for file operations
 *
 * @param config_json JSON configuration string
 * @return New runtime handle, or NULL on failure
 */
NEAM_API NeamRuntime* neam_runtime_new_with_config(const char* config_json);

/**
 * @brief Free a runtime instance
 * @param runtime Runtime to free (may be NULL)
 */
NEAM_API void neam_runtime_free(NeamRuntime* runtime);

/**
 * @brief Check if a runtime handle is valid
 * @param runtime Runtime to check
 * @return Non-zero if valid, zero if NULL or invalid
 */
NEAM_API int neam_runtime_is_valid(NeamRuntime* runtime);

/**
 * @brief Reset runtime state without destroying it
 * @param runtime Runtime to reset
 */
NEAM_API void neam_runtime_reset(NeamRuntime* runtime);

/* ============================================================================
 * Code Compilation
 * ============================================================================ */

/**
 * @brief Compile Neam source code to bytecode
 *
 * @param runtime Runtime context
 * @param source Source code string
 * @return Bytecode handle, or NULL on error
 */
NEAM_API NeamBytecode* neam_compile(NeamRuntime* runtime, const char* source);

/**
 * @brief Compile Neam source file to bytecode
 *
 * @param runtime Runtime context
 * @param path Path to source file
 * @return Bytecode handle, or NULL on error
 */
NEAM_API NeamBytecode* neam_compile_file(NeamRuntime* runtime, const char* path);

/**
 * @brief Free compiled bytecode
 * @param bytecode Bytecode to free (may be NULL)
 */
NEAM_API void neam_bytecode_free(NeamBytecode* bytecode);

/**
 * @brief Serialize bytecode to bytes
 *
 * @param bytecode Bytecode to serialize
 * @param out_size Output parameter for size
 * @return Byte array (caller must free with neam_bytes_free), or NULL on error
 */
NEAM_API uint8_t* neam_bytecode_to_bytes(NeamBytecode* bytecode, size_t* out_size);

/**
 * @brief Load bytecode from bytes
 *
 * @param bytes Byte array
 * @param size Size of byte array
 * @return Bytecode handle, or NULL on error
 */
NEAM_API NeamBytecode* neam_bytecode_from_bytes(const uint8_t* bytes, size_t size);

/**
 * @brief Free bytes returned by neam_bytecode_to_bytes
 * @param bytes Bytes to free
 */
NEAM_API void neam_bytes_free(uint8_t* bytes);

/* ============================================================================
 * Code Execution
 * ============================================================================ */

/**
 * @brief Run Neam source code directly
 *
 * @param runtime Runtime context
 * @param source Source code string
 * @return Execution result (caller takes ownership)
 */
NEAM_API NeamResult* neam_run(NeamRuntime* runtime, const char* source);

/**
 * @brief Run a Neam source file
 *
 * @param runtime Runtime context
 * @param path Path to source file
 * @return Execution result (caller takes ownership)
 */
NEAM_API NeamResult* neam_run_file(NeamRuntime* runtime, const char* path);

/**
 * @brief Run compiled bytecode
 *
 * @param runtime Runtime context
 * @param bytecode Compiled bytecode
 * @return Execution result (caller takes ownership)
 */
NEAM_API NeamResult* neam_run_bytecode(NeamRuntime* runtime, NeamBytecode* bytecode);

/**
 * @brief Run bytecode from raw bytes
 *
 * @param runtime Runtime context
 * @param bytes Bytecode bytes
 * @param size Size of byte array
 * @return Execution result (caller takes ownership)
 */
NEAM_API NeamResult* neam_run_bytecode_bytes(
    NeamRuntime* runtime,
    const uint8_t* bytes,
    size_t size
);

/* ============================================================================
 * Result Handling
 * ============================================================================ */

/**
 * @brief Check if result represents an error
 * @param result Result to check
 * @return Non-zero if error, zero if success
 */
NEAM_API int neam_result_is_error(NeamResult* result);

/**
 * @brief Get all output emitted during execution
 * @param result Execution result
 * @return Combined output string (owned by result)
 */
NEAM_API const char* neam_result_output(NeamResult* result);

/**
 * @brief Get the return value from execution
 * @param result Execution result
 * @return Value (caller takes ownership), or NULL if error
 */
NEAM_API NeamValue* neam_result_value(NeamResult* result);

/**
 * @brief Get error from result
 * @param result Execution result
 * @return Error handle (owned by result), or NULL if no error
 */
NEAM_API NeamError* neam_result_error(NeamResult* result);

/**
 * @brief Free an execution result
 * @param result Result to free (may be NULL)
 */
NEAM_API void neam_result_free(NeamResult* result);

/* ============================================================================
 * Value Creation
 * ============================================================================ */

/**
 * @brief Create a nil value
 * @return New value (caller takes ownership)
 */
NEAM_API NeamValue* neam_value_nil(void);

/**
 * @brief Create a boolean value
 * @param value Boolean value (0 = false, non-zero = true)
 * @return New value (caller takes ownership)
 */
NEAM_API NeamValue* neam_value_bool(int value);

/**
 * @brief Create a number value
 * @param value Numeric value
 * @return New value (caller takes ownership)
 */
NEAM_API NeamValue* neam_value_number(double value);

/**
 * @brief Create a string value
 * @param value String content
 * @param length String length (or -1 for null-terminated)
 * @return New value (caller takes ownership)
 */
NEAM_API NeamValue* neam_value_string(const char* value, int length);

/**
 * @brief Create an empty list value
 * @return New list value (caller takes ownership)
 */
NEAM_API NeamValue* neam_value_list_new(void);

/**
 * @brief Append a value to a list
 *
 * @param list List to append to
 * @param item Value to append (ownership transferred to list)
 */
NEAM_API void neam_value_list_append(NeamValue* list, NeamValue* item);

/**
 * @brief Create an empty map value
 * @return New map value (caller takes ownership)
 */
NEAM_API NeamValue* neam_value_map_new(void);

/**
 * @brief Set a key-value pair in a map
 *
 * @param map Map to modify
 * @param key Key string
 * @param value Value to set (ownership transferred to map)
 */
NEAM_API void neam_value_map_set(NeamValue* map, const char* key, NeamValue* value);

/* ============================================================================
 * Value Access
 * ============================================================================ */

/**
 * @brief Get the type of a value
 * @param value Value to check
 * @return Value type
 */
NEAM_API NeamValueType neam_value_type(NeamValue* value);

/**
 * @brief Get boolean from value
 * @param value Value to extract from
 * @return Boolean value (0 or 1)
 */
NEAM_API int neam_value_as_bool(NeamValue* value);

/**
 * @brief Get number from value
 * @param value Value to extract from
 * @return Numeric value
 */
NEAM_API double neam_value_as_number(NeamValue* value);

/**
 * @brief Get string from value
 * @param value Value to extract from
 * @return String pointer (owned by value), or NULL if not a string
 */
NEAM_API const char* neam_value_as_string(NeamValue* value);

/**
 * @brief Get string length
 * @param value String value
 * @return Length in bytes
 */
NEAM_API size_t neam_value_string_length(NeamValue* value);

/**
 * @brief Get list length
 * @param value List value
 * @return Number of items
 */
NEAM_API size_t neam_value_list_length(NeamValue* value);

/**
 * @brief Get item from list by index
 *
 * @param value List value
 * @param index Index (0-based)
 * @return Value at index (caller takes ownership), or NULL if out of bounds
 */
NEAM_API NeamValue* neam_value_list_get(NeamValue* value, size_t index);

/**
 * @brief Get value from map by key
 *
 * @param value Map value
 * @param key Key string
 * @return Value for key (caller takes ownership), or NULL if not found
 */
NEAM_API NeamValue* neam_value_map_get(NeamValue* value, const char* key);

/**
 * @brief Check if map contains a key
 *
 * @param value Map value
 * @param key Key to check
 * @return Non-zero if key exists
 */
NEAM_API int neam_value_map_has(NeamValue* value, const char* key);

/**
 * @brief Get all keys from a map
 *
 * @param value Map value
 * @param out_count Output parameter for count
 * @return Array of key strings (caller must free array but not strings)
 */
NEAM_API const char** neam_value_map_keys(NeamValue* value, size_t* out_count);

/**
 * @brief Convert value to JSON string
 *
 * @param value Value to convert
 * @return JSON string (caller must free with neam_string_free)
 */
NEAM_API char* neam_value_to_json(NeamValue* value);

/**
 * @brief Create value from JSON string
 *
 * @param json JSON string
 * @return Value (caller takes ownership), or NULL on parse error
 */
NEAM_API NeamValue* neam_value_from_json(const char* json);

/**
 * @brief Free a value
 * @param value Value to free (may be NULL)
 */
NEAM_API void neam_value_free(NeamValue* value);

/**
 * @brief Free a string returned by neam_value_to_json
 * @param str String to free
 */
NEAM_API void neam_string_free(char* str);

/* ============================================================================
 * Global Variable Access
 * ============================================================================ */

/**
 * @brief Get a global variable by name
 *
 * @param runtime Runtime context
 * @param name Variable name
 * @return Value (caller takes ownership), or NULL if not found
 */
NEAM_API NeamValue* neam_get_global(NeamRuntime* runtime, const char* name);

/**
 * @brief Set a global variable
 *
 * @param runtime Runtime context
 * @param name Variable name
 * @param value Value to set (ownership transferred)
 * @return Zero on success, non-zero on error
 */
NEAM_API int neam_set_global(NeamRuntime* runtime, const char* name, NeamValue* value);

/**
 * @brief Call a global function
 *
 * @param runtime Runtime context
 * @param name Function name
 * @param arg_count Number of arguments
 * @param args Array of argument values
 * @param out_result Output parameter for result (caller takes ownership)
 * @return Zero on success, non-zero on error
 */
NEAM_API int neam_call_function(
    NeamRuntime* runtime,
    const char* name,
    int arg_count,
    NeamValue** args,
    NeamValue** out_result
);

/* ============================================================================
 * Tool Registration
 * ============================================================================ */

/**
 * @brief Register an external tool
 *
 * @param runtime Runtime context
 * @param name Tool name
 * @param description Tool description (for agent context)
 * @param func Tool implementation callback
 * @param user_data User context (passed to callback)
 * @return Zero on success, non-zero on error
 */
NEAM_API int neam_register_tool(
    NeamRuntime* runtime,
    const char* name,
    const char* description,
    NeamToolFunc func,
    void* user_data
);

/**
 * @brief Register a tool with JSON schema for parameters
 *
 * @param runtime Runtime context
 * @param name Tool name
 * @param description Tool description
 * @param parameters_schema JSON Schema for parameters
 * @param func Tool implementation callback
 * @param user_data User context
 * @return Zero on success, non-zero on error
 */
NEAM_API int neam_register_tool_with_schema(
    NeamRuntime* runtime,
    const char* name,
    const char* description,
    const char* parameters_schema,
    NeamToolFunc func,
    void* user_data
);

/**
 * @brief Unregister a tool
 *
 * @param runtime Runtime context
 * @param name Tool name
 * @return Zero on success, non-zero if not found
 */
NEAM_API int neam_unregister_tool(NeamRuntime* runtime, const char* name);

/**
 * @brief Check if a tool is registered
 *
 * @param runtime Runtime context
 * @param name Tool name
 * @return Non-zero if registered
 */
NEAM_API int neam_has_tool(NeamRuntime* runtime, const char* name);

/* ============================================================================
 * Agent API
 * ============================================================================ */

/**
 * @brief Create a new agent
 *
 * @param runtime Runtime context
 * @param name Agent name
 * @param provider LLM provider ("ollama", "openai", etc.)
 * @param model Model name
 * @param system System prompt
 * @return Agent handle (caller takes ownership), or NULL on error
 */
NEAM_API NeamAgent* neam_agent_create(
    NeamRuntime* runtime,
    const char* name,
    const char* provider,
    const char* model,
    const char* system
);

/**
 * @brief Set agent temperature
 *
 * @param agent Agent handle
 * @param temperature Temperature value (0.0 to 2.0)
 */
NEAM_API void neam_agent_set_temperature(NeamAgent* agent, double temperature);

/**
 * @brief Set agent context from file or directory
 *
 * @param agent Agent handle
 * @param path Path to context file or directory
 * @return Zero on success, non-zero on error
 */
NEAM_API int neam_agent_set_context_from(NeamAgent* agent, const char* path);

/**
 * @brief Send a query to an agent
 *
 * @param agent Agent handle
 * @param query Query string
 * @return Response string (caller must free with neam_string_free)
 */
NEAM_API char* neam_agent_ask(NeamAgent* agent, const char* query);

/**
 * @brief Add a handoff target to an agent
 *
 * @param agent Source agent
 * @param target_name Target agent name
 * @param tool_name Tool name for handoff (or NULL for default)
 * @param description Handoff description
 * @return Zero on success
 */
NEAM_API int neam_agent_add_handoff(
    NeamAgent* agent,
    const char* target_name,
    const char* tool_name,
    const char* description
);

/**
 * @brief Reset agent conversation history
 * @param agent Agent handle
 */
NEAM_API void neam_agent_reset(NeamAgent* agent);

/**
 * @brief Free an agent
 * @param agent Agent to free (may be NULL)
 */
NEAM_API void neam_agent_free(NeamAgent* agent);

/* ============================================================================
 * Runner API (Agent Loop)
 * ============================================================================ */

/**
 * @brief Create a runner for agent loop execution
 *
 * @param runtime Runtime context
 * @param entry_agent Entry agent name
 * @param max_turns Maximum turns before stopping
 * @return Runner handle (caller takes ownership)
 */
NEAM_API NeamRunner* neam_runner_create(
    NeamRuntime* runtime,
    const char* entry_agent,
    int max_turns
);

/**
 * @brief Enable or disable tracing for a runner
 *
 * @param runner Runner handle
 * @param enabled Non-zero to enable
 */
NEAM_API void neam_runner_enable_tracing(NeamRunner* runner, int enabled);

/**
 * @brief Set trace callback
 *
 * @param runner Runner handle
 * @param callback Trace callback function
 * @param user_data User context
 */
NEAM_API void neam_runner_set_trace_callback(
    NeamRunner* runner,
    NeamTraceCallback callback,
    void* user_data
);

/**
 * @brief Run the agent loop
 *
 * @param runner Runner handle
 * @param input Input string
 * @return Execution result (caller takes ownership)
 */
NEAM_API NeamResult* neam_runner_run(NeamRunner* runner, const char* input);

/**
 * @brief Get the final agent name after a run
 *
 * @param runner Runner handle
 * @return Agent name (owned by runner)
 */
NEAM_API const char* neam_runner_final_agent(NeamRunner* runner);

/**
 * @brief Get total turns used in last run
 *
 * @param runner Runner handle
 * @return Number of turns
 */
NEAM_API int neam_runner_total_turns(NeamRunner* runner);

/**
 * @brief Free a runner
 * @param runner Runner to free (may be NULL)
 */
NEAM_API void neam_runner_free(NeamRunner* runner);

/* ============================================================================
 * Task API (A2A Protocol)
 * ============================================================================ */

/**
 * @brief Create a new task
 *
 * @param runtime Runtime context
 * @param agent_name Target agent name
 * @param input_json Input as JSON string
 * @return Task handle (caller takes ownership)
 */
NEAM_API NeamTask* neam_task_create(
    NeamRuntime* runtime,
    const char* agent_name,
    const char* input_json
);

/**
 * @brief Get task ID
 *
 * @param task Task handle
 * @return Task ID string (owned by task)
 */
NEAM_API const char* neam_task_id(NeamTask* task);

/**
 * @brief Submit a task for execution
 *
 * @param task Task handle
 * @return Zero on success
 */
NEAM_API int neam_task_submit(NeamTask* task);

/**
 * @brief Get task status
 *
 * @param task Task handle
 * @return Current status
 */
NEAM_API NeamTaskStatus neam_task_status(NeamTask* task);

/**
 * @brief Wait for task completion with timeout
 *
 * @param task Task handle
 * @param timeout_ms Timeout in milliseconds (-1 for infinite)
 * @return Zero if completed, non-zero if timeout or error
 */
NEAM_API int neam_task_wait(NeamTask* task, int timeout_ms);

/**
 * @brief Get task result
 *
 * @param task Task handle
 * @return Result JSON string (caller must free with neam_string_free)
 */
NEAM_API char* neam_task_result(NeamTask* task);

/**
 * @brief Cancel a task
 *
 * @param task Task handle
 * @return Zero on success
 */
NEAM_API int neam_task_cancel(NeamTask* task);

/**
 * @brief Free a task
 * @param task Task to free (may be NULL)
 */
NEAM_API void neam_task_free(NeamTask* task);

/* ============================================================================
 * Agent Card API (A2A Protocol)
 * ============================================================================ */

/**
 * @brief Get agent card as JSON
 *
 * @param runtime Runtime context
 * @param agent_name Agent name
 * @return Agent card JSON (caller must free with neam_string_free)
 */
NEAM_API char* neam_get_agent_card(NeamRuntime* runtime, const char* agent_name);

/**
 * @brief Register an agent card from JSON
 *
 * @param runtime Runtime context
 * @param agent_name Agent name
 * @param card_json Agent card as JSON
 * @return Zero on success
 */
NEAM_API int neam_register_agent_card(
    NeamRuntime* runtime,
    const char* agent_name,
    const char* card_json
);

/* ============================================================================
 * Error Handling
 * ============================================================================ */

/**
 * @brief Get the last error from a runtime
 *
 * @param runtime Runtime context
 * @return Error handle (owned by runtime), or NULL if no error
 */
NEAM_API NeamError* neam_get_last_error(NeamRuntime* runtime);

/**
 * @brief Get error type
 *
 * @param error Error handle
 * @return Error type
 */
NEAM_API NeamErrorType neam_error_type(NeamError* error);

/**
 * @brief Get error message
 *
 * @param error Error handle
 * @return Error message (owned by error)
 */
NEAM_API const char* neam_error_message(NeamError* error);

/**
 * @brief Get error source line
 *
 * @param error Error handle
 * @return Line number, or -1 if unavailable
 */
NEAM_API int neam_error_line(NeamError* error);

/**
 * @brief Get error source column
 *
 * @param error Error handle
 * @return Column number, or -1 if unavailable
 */
NEAM_API int neam_error_column(NeamError* error);

/**
 * @brief Get error stack trace
 *
 * @param error Error handle
 * @return Stack trace string (owned by error)
 */
NEAM_API const char* neam_error_stack_trace(NeamError* error);

/**
 * @brief Convert error to JSON
 *
 * @param error Error handle
 * @return JSON string (caller must free with neam_string_free)
 */
NEAM_API char* neam_error_to_json(NeamError* error);

/**
 * @brief Clear the last error
 *
 * @param runtime Runtime context
 */
NEAM_API void neam_clear_error(NeamRuntime* runtime);

/* ============================================================================
 * Callback Registration
 * ============================================================================ */

/**
 * @brief Set callback for emit statements
 *
 * @param runtime Runtime context
 * @param callback Emit callback function
 * @param user_data User context
 */
NEAM_API void neam_set_emit_callback(
    NeamRuntime* runtime,
    NeamEmitCallback callback,
    void* user_data
);

/* ============================================================================
 * Memory Management Helpers
 * ============================================================================ */

/**
 * @brief Pause garbage collection
 *
 * Use this when holding Neam values in host code to prevent them from
 * being collected. Must be balanced with neam_gc_resume().
 *
 * @param runtime Runtime context
 */
NEAM_API void neam_gc_pause(NeamRuntime* runtime);

/**
 * @brief Resume garbage collection
 *
 * @param runtime Runtime context
 */
NEAM_API void neam_gc_resume(NeamRuntime* runtime);

/**
 * @brief Force a garbage collection cycle
 *
 * @param runtime Runtime context
 */
NEAM_API void neam_gc_collect(NeamRuntime* runtime);

/**
 * @brief Get memory usage statistics
 *
 * @param runtime Runtime context
 * @param out_heap_size Output for total heap size
 * @param out_heap_used Output for used heap
 * @param out_object_count Output for object count
 */
NEAM_API void neam_gc_stats(
    NeamRuntime* runtime,
    size_t* out_heap_size,
    size_t* out_heap_used,
    size_t* out_object_count
);

#ifdef __cplusplus
}
#endif

#endif /* NEAM_C_API_H */
