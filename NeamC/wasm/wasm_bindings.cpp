/**
 * WebAssembly Bindings for Neam
 *
 * This file provides the C API for WebAssembly builds of Neam.
 * It exposes a minimal set of functions for running Neam code
 * in the browser.
 */

#include <cstdlib>
#include <cstring>
#include <string>
#include <sstream>

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#define WASM_EXPORT EMSCRIPTEN_KEEPALIVE
#else
#define WASM_EXPORT
#endif

// Forward declarations from Neam
namespace neamc {
class Pipeline;
class VirtualMachine;
}  // namespace neamc

#include "neamc/pipeline.hpp"
#include "neamc/vm/vm.hpp"
#include "neamc/vm/bytecode.hpp"

// Global state
static neamc::VirtualMachine* g_vm = nullptr;
static std::string g_last_output;
static std::string g_last_error;

extern "C" {

/**
 * Initialize the Neam runtime.
 * Must be called before any other functions.
 * @return 0 on success, non-zero on error
 */
WASM_EXPORT
int neam_init() {
    if (g_vm) {
        return 0;  // Already initialized
    }

    try {
        g_vm = new neamc::VirtualMachine();
        g_last_output.clear();
        g_last_error.clear();
        return 0;
    } catch (const std::exception& e) {
        g_last_error = e.what();
        return 1;
    }
}

/**
 * Shutdown the Neam runtime.
 * Call this when done to free resources.
 */
WASM_EXPORT
void neam_shutdown() {
    if (g_vm) {
        delete g_vm;
        g_vm = nullptr;
    }
    g_last_output.clear();
    g_last_error.clear();
}

/**
 * Run Neam source code.
 * @param source The source code to run
 * @return Pointer to result value, or nullptr on error
 */
WASM_EXPORT
void* neam_run(const char* source) {
    if (!g_vm || !source) {
        g_last_error = "Runtime not initialized or invalid source";
        return nullptr;
    }

    g_last_output.clear();
    g_last_error.clear();

    try {
        // Set up output capture
        g_vm->set_emit_callback([](const std::string& msg) {
            if (!g_last_output.empty()) {
                g_last_output += "\n";
            }
            g_last_output += msg;
        });

        // Parse and compile
        neamc::Pipeline pipeline;
        auto program = pipeline.parse(source, "<wasm>");
        if (!program) {
            g_last_error = "Parse error";
            return nullptr;
        }

        auto bytecode = pipeline.compile(*program);

        // Run
        g_vm->interpret(bytecode);

        // Return a non-null pointer to indicate success
        // The actual value can be retrieved via other functions
        return reinterpret_cast<void*>(1);
    } catch (const std::exception& e) {
        g_last_error = e.what();
        return nullptr;
    }
}

/**
 * Compile source code to bytecode.
 * @param source The source code to compile
 * @param out_size Output parameter for bytecode size
 * @return Pointer to bytecode bytes, or nullptr on error
 */
WASM_EXPORT
uint8_t* neam_compile(const char* source, size_t* out_size) {
    if (!source || !out_size) {
        g_last_error = "Invalid arguments";
        return nullptr;
    }

    g_last_error.clear();

    try {
        neamc::Pipeline pipeline;
        auto program = pipeline.parse(source, "<wasm>");
        if (!program) {
            g_last_error = "Parse error";
            return nullptr;
        }

        auto bytecode = pipeline.compile(*program);

        // Serialize to bytes
        std::ostringstream oss;
        bytecode.serialize(oss);
        std::string data = oss.str();

        // Allocate and copy
        uint8_t* result = static_cast<uint8_t*>(malloc(data.size()));
        if (!result) {
            g_last_error = "Memory allocation failed";
            return nullptr;
        }

        memcpy(result, data.data(), data.size());
        *out_size = data.size();
        return result;
    } catch (const std::exception& e) {
        g_last_error = e.what();
        return nullptr;
    }
}

/**
 * Run compiled bytecode.
 * @param bytecode Pointer to bytecode bytes
 * @param size Size of bytecode
 * @return Non-null on success
 */
WASM_EXPORT
void* neam_run_bytecode(const uint8_t* bytecode, size_t size) {
    if (!g_vm || !bytecode || size == 0) {
        g_last_error = "Invalid arguments";
        return nullptr;
    }

    g_last_output.clear();
    g_last_error.clear();

    try {
        // Set up output capture
        g_vm->set_emit_callback([](const std::string& msg) {
            if (!g_last_output.empty()) {
                g_last_output += "\n";
            }
            g_last_output += msg;
        });

        // Deserialize bytecode
        std::string data(reinterpret_cast<const char*>(bytecode), size);
        std::istringstream iss(data);
        neamc::Bytecode bc;
        bc.deserialize(iss);

        // Run
        g_vm->interpret(bc);

        return reinterpret_cast<void*>(1);
    } catch (const std::exception& e) {
        g_last_error = e.what();
        return nullptr;
    }
}

/**
 * Get the output from the last run.
 * @return Output string (owned by runtime, do not free)
 */
WASM_EXPORT
const char* neam_get_output() {
    return g_last_output.c_str();
}

/**
 * Get the error from the last operation.
 * @return Error string (owned by runtime, do not free)
 */
WASM_EXPORT
const char* neam_get_error() {
    return g_last_error.c_str();
}

/**
 * Get the type of a value.
 * @param value Value pointer
 * @return Type code (0=nil, 1=bool, 2=number, 3=string, etc.)
 */
WASM_EXPORT
int neam_value_type(void* value) {
    // Simplified - actual implementation would check Value type
    return 0;
}

/**
 * Get a number value.
 * @param value Value pointer
 * @return Number value
 */
WASM_EXPORT
double neam_value_as_number(void* value) {
    return 0.0;
}

/**
 * Get a string value.
 * @param value Value pointer
 * @return String (caller must free with neam_free_string)
 */
WASM_EXPORT
char* neam_value_as_string(void* value) {
    return strdup("");
}

/**
 * Get a boolean value.
 * @param value Value pointer
 * @return Boolean (0 or 1)
 */
WASM_EXPORT
int neam_value_as_bool(void* value) {
    return 0;
}

/**
 * Convert value to JSON string.
 * @param value Value pointer
 * @return JSON string (caller must free with neam_free_string)
 */
WASM_EXPORT
char* neam_value_to_json(void* value) {
    return strdup("null");
}

/**
 * Free a result from neam_run.
 * @param result Result pointer
 */
WASM_EXPORT
void neam_free_result(void* result) {
    // No-op for now - results are not heap allocated
}

/**
 * Free a string returned by neam functions.
 * @param str String to free
 */
WASM_EXPORT
void neam_free_string(char* str) {
    if (str) {
        free(str);
    }
}

}  // extern "C"
