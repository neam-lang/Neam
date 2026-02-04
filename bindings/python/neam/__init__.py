"""
Neam Python Bindings

This module provides Python bindings for the Neam agentic programming language.
It uses ctypes to interface with the libneam shared library.

Example usage:
    >>> import neam
    >>> runtime = neam.Runtime()
    >>> result = runtime.run('emit "Hello from Neam!"')
    >>> print(result.output)
    Hello from Neam!

    >>> agent = neam.Agent(runtime, "assistant", provider="ollama", model="llama3.2:3b")
    >>> response = agent.ask("What is 2 + 2?")
    >>> print(response)
"""

from __future__ import annotations

import ctypes
import ctypes.util
import os
import platform
import sys
from pathlib import Path
from typing import Any, Callable, Dict, List, Optional, Union

__version__ = "0.3.0"
__all__ = [
    "Runtime",
    "Result",
    "Value",
    "Error",
    "Agent",
    "Runner",
    "Task",
    "Bytecode",
    "tool",
    "NeamError",
    "ParseError",
    "CompileError",
    "RuntimeError",
    "ValueType",
    "TaskStatus",
    "ErrorType",
]


# ============================================================================
# Library Loading
# ============================================================================

def _get_lib_extension() -> str:
    """Get the shared library extension for the current platform."""
    system = platform.system()
    if system == "Darwin":
        return ".dylib"
    elif system == "Windows":
        return ".dll"
    return ".so"


def _find_library() -> Optional[str]:
    """Find the libneam shared library."""
    lib_name = "libneam" + _get_lib_extension()

    # Check environment variable first
    env_path = os.environ.get("NEAM_LIBRARY_PATH")
    if env_path and Path(env_path).exists():
        return env_path

    # Search paths in order of preference
    search_paths = [
        # Relative to this package (for development)
        Path(__file__).parent.parent.parent.parent / "build" / lib_name,
        # Next to the package
        Path(__file__).parent / lib_name,
        # System locations
        Path("/usr/local/lib") / lib_name,
        Path("/usr/lib") / lib_name,
        # Homebrew on macOS
        Path("/opt/homebrew/lib") / lib_name,
        # Windows
        Path("C:/Program Files/Neam/lib") / lib_name,
    ]

    for path in search_paths:
        if path.exists():
            return str(path)

    # Try ctypes.util as last resort
    lib = ctypes.util.find_library("neam")
    return lib


# Load the library
_lib_path = _find_library()
if _lib_path is None:
    raise ImportError(
        "Could not find libneam. Please ensure it is built and either:\n"
        "1. Set NEAM_LIBRARY_PATH environment variable\n"
        "2. Install it to a system library path\n"
        "3. Build from source: cd build && cmake --build ."
    )

_lib = ctypes.CDLL(_lib_path)


# ============================================================================
# Enumerations
# ============================================================================

class ValueType:
    """Enumeration of Neam value types."""
    NIL = 0
    BOOL = 1
    NUMBER = 2
    STRING = 3
    LIST = 4
    MAP = 5
    FUNCTION = 6
    AGENT = 7
    OBJECT = 8


class ErrorType:
    """Enumeration of error types."""
    NONE = 0
    PARSE = 1
    COMPILE = 2
    RUNTIME = 3
    TOOL = 4
    TIMEOUT = 5
    BUDGET = 6
    AGENT = 7
    MEMORY = 8
    IO = 9
    INVALID_ARGUMENT = 10


class TaskStatus:
    """Enumeration of task statuses (A2A Protocol)."""
    PENDING = 0
    RUNNING = 1
    COMPLETED = 2
    FAILED = 3
    CANCELLED = 4


# ============================================================================
# Exception Classes
# ============================================================================

class NeamError(Exception):
    """Base exception for Neam errors."""

    def __init__(
        self,
        message: str,
        error_type: int = ErrorType.RUNTIME,
        line: int = -1,
        column: int = -1,
        stack_trace: Optional[str] = None,
    ):
        super().__init__(message)
        self.error_type = error_type
        self.line = line
        self.column = column
        self.stack_trace = stack_trace


class ParseError(NeamError):
    """Exception for parse errors."""

    def __init__(self, message: str, line: int = -1, column: int = -1):
        super().__init__(message, ErrorType.PARSE, line, column)


class CompileError(NeamError):
    """Exception for compile errors."""

    def __init__(self, message: str, line: int = -1, column: int = -1):
        super().__init__(message, ErrorType.COMPILE, line, column)


class RuntimeError(NeamError):
    """Exception for runtime errors."""

    def __init__(self, message: str, stack_trace: Optional[str] = None):
        super().__init__(message, ErrorType.RUNTIME, stack_trace=stack_trace)


# ============================================================================
# C Function Signatures
# ============================================================================

# Opaque handle types
class _NeamRuntime(ctypes.Structure):
    pass

class _NeamResult(ctypes.Structure):
    pass

class _NeamValue(ctypes.Structure):
    pass

class _NeamError(ctypes.Structure):
    pass

class _NeamAgent(ctypes.Structure):
    pass

class _NeamRunner(ctypes.Structure):
    pass

class _NeamTask(ctypes.Structure):
    pass

class _NeamBytecode(ctypes.Structure):
    pass


# Version functions
_lib.neam_version.restype = ctypes.c_char_p
_lib.neam_version.argtypes = []
_lib.neam_version_major.restype = ctypes.c_int
_lib.neam_version_major.argtypes = []
_lib.neam_version_minor.restype = ctypes.c_int
_lib.neam_version_minor.argtypes = []
_lib.neam_version_patch.restype = ctypes.c_int
_lib.neam_version_patch.argtypes = []

# Runtime lifecycle
_lib.neam_runtime_new.restype = ctypes.POINTER(_NeamRuntime)
_lib.neam_runtime_new.argtypes = []
_lib.neam_runtime_new_with_config.restype = ctypes.POINTER(_NeamRuntime)
_lib.neam_runtime_new_with_config.argtypes = [ctypes.c_char_p]
_lib.neam_runtime_free.restype = None
_lib.neam_runtime_free.argtypes = [ctypes.POINTER(_NeamRuntime)]
_lib.neam_runtime_is_valid.restype = ctypes.c_int
_lib.neam_runtime_is_valid.argtypes = [ctypes.POINTER(_NeamRuntime)]
_lib.neam_runtime_reset.restype = None
_lib.neam_runtime_reset.argtypes = [ctypes.POINTER(_NeamRuntime)]

# Compilation
_lib.neam_compile.restype = ctypes.POINTER(_NeamBytecode)
_lib.neam_compile.argtypes = [ctypes.POINTER(_NeamRuntime), ctypes.c_char_p]
_lib.neam_compile_file.restype = ctypes.POINTER(_NeamBytecode)
_lib.neam_compile_file.argtypes = [ctypes.POINTER(_NeamRuntime), ctypes.c_char_p]
_lib.neam_bytecode_free.restype = None
_lib.neam_bytecode_free.argtypes = [ctypes.POINTER(_NeamBytecode)]
_lib.neam_bytecode_to_bytes.restype = ctypes.POINTER(ctypes.c_uint8)
_lib.neam_bytecode_to_bytes.argtypes = [ctypes.POINTER(_NeamBytecode), ctypes.POINTER(ctypes.c_size_t)]
_lib.neam_bytecode_from_bytes.restype = ctypes.POINTER(_NeamBytecode)
_lib.neam_bytecode_from_bytes.argtypes = [ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t]
_lib.neam_bytes_free.restype = None
_lib.neam_bytes_free.argtypes = [ctypes.POINTER(ctypes.c_uint8)]

# Execution
_lib.neam_run.restype = ctypes.POINTER(_NeamResult)
_lib.neam_run.argtypes = [ctypes.POINTER(_NeamRuntime), ctypes.c_char_p]
_lib.neam_run_file.restype = ctypes.POINTER(_NeamResult)
_lib.neam_run_file.argtypes = [ctypes.POINTER(_NeamRuntime), ctypes.c_char_p]
_lib.neam_run_bytecode.restype = ctypes.POINTER(_NeamResult)
_lib.neam_run_bytecode.argtypes = [ctypes.POINTER(_NeamRuntime), ctypes.POINTER(_NeamBytecode)]
_lib.neam_run_bytecode_bytes.restype = ctypes.POINTER(_NeamResult)
_lib.neam_run_bytecode_bytes.argtypes = [ctypes.POINTER(_NeamRuntime), ctypes.POINTER(ctypes.c_uint8), ctypes.c_size_t]

# Result handling
_lib.neam_result_is_error.restype = ctypes.c_int
_lib.neam_result_is_error.argtypes = [ctypes.POINTER(_NeamResult)]
_lib.neam_result_output.restype = ctypes.c_char_p
_lib.neam_result_output.argtypes = [ctypes.POINTER(_NeamResult)]
_lib.neam_result_value.restype = ctypes.POINTER(_NeamValue)
_lib.neam_result_value.argtypes = [ctypes.POINTER(_NeamResult)]
_lib.neam_result_error.restype = ctypes.POINTER(_NeamError)
_lib.neam_result_error.argtypes = [ctypes.POINTER(_NeamResult)]
_lib.neam_result_free.restype = None
_lib.neam_result_free.argtypes = [ctypes.POINTER(_NeamResult)]

# Value creation
_lib.neam_value_nil.restype = ctypes.POINTER(_NeamValue)
_lib.neam_value_nil.argtypes = []
_lib.neam_value_bool.restype = ctypes.POINTER(_NeamValue)
_lib.neam_value_bool.argtypes = [ctypes.c_int]
_lib.neam_value_number.restype = ctypes.POINTER(_NeamValue)
_lib.neam_value_number.argtypes = [ctypes.c_double]
_lib.neam_value_string.restype = ctypes.POINTER(_NeamValue)
_lib.neam_value_string.argtypes = [ctypes.c_char_p, ctypes.c_int]
_lib.neam_value_list_new.restype = ctypes.POINTER(_NeamValue)
_lib.neam_value_list_new.argtypes = []
_lib.neam_value_list_append.restype = None
_lib.neam_value_list_append.argtypes = [ctypes.POINTER(_NeamValue), ctypes.POINTER(_NeamValue)]
_lib.neam_value_map_new.restype = ctypes.POINTER(_NeamValue)
_lib.neam_value_map_new.argtypes = []
_lib.neam_value_map_set.restype = None
_lib.neam_value_map_set.argtypes = [ctypes.POINTER(_NeamValue), ctypes.c_char_p, ctypes.POINTER(_NeamValue)]

# Value access
_lib.neam_value_type.restype = ctypes.c_int
_lib.neam_value_type.argtypes = [ctypes.POINTER(_NeamValue)]
_lib.neam_value_as_bool.restype = ctypes.c_int
_lib.neam_value_as_bool.argtypes = [ctypes.POINTER(_NeamValue)]
_lib.neam_value_as_number.restype = ctypes.c_double
_lib.neam_value_as_number.argtypes = [ctypes.POINTER(_NeamValue)]
_lib.neam_value_as_string.restype = ctypes.c_char_p
_lib.neam_value_as_string.argtypes = [ctypes.POINTER(_NeamValue)]
_lib.neam_value_string_length.restype = ctypes.c_size_t
_lib.neam_value_string_length.argtypes = [ctypes.POINTER(_NeamValue)]
_lib.neam_value_list_length.restype = ctypes.c_size_t
_lib.neam_value_list_length.argtypes = [ctypes.POINTER(_NeamValue)]
_lib.neam_value_list_get.restype = ctypes.POINTER(_NeamValue)
_lib.neam_value_list_get.argtypes = [ctypes.POINTER(_NeamValue), ctypes.c_size_t]
_lib.neam_value_map_get.restype = ctypes.POINTER(_NeamValue)
_lib.neam_value_map_get.argtypes = [ctypes.POINTER(_NeamValue), ctypes.c_char_p]
_lib.neam_value_map_has.restype = ctypes.c_int
_lib.neam_value_map_has.argtypes = [ctypes.POINTER(_NeamValue), ctypes.c_char_p]
_lib.neam_value_to_json.restype = ctypes.c_char_p
_lib.neam_value_to_json.argtypes = [ctypes.POINTER(_NeamValue)]
_lib.neam_value_from_json.restype = ctypes.POINTER(_NeamValue)
_lib.neam_value_from_json.argtypes = [ctypes.c_char_p]
_lib.neam_value_free.restype = None
_lib.neam_value_free.argtypes = [ctypes.POINTER(_NeamValue)]
_lib.neam_string_free.restype = None
_lib.neam_string_free.argtypes = [ctypes.c_char_p]

# Global access
_lib.neam_get_global.restype = ctypes.POINTER(_NeamValue)
_lib.neam_get_global.argtypes = [ctypes.POINTER(_NeamRuntime), ctypes.c_char_p]
_lib.neam_set_global.restype = ctypes.c_int
_lib.neam_set_global.argtypes = [ctypes.POINTER(_NeamRuntime), ctypes.c_char_p, ctypes.POINTER(_NeamValue)]

# Tool registration callback type
_ToolFunc = ctypes.CFUNCTYPE(
    ctypes.POINTER(_NeamValue),
    ctypes.POINTER(_NeamRuntime),
    ctypes.c_int,
    ctypes.POINTER(ctypes.POINTER(_NeamValue)),
    ctypes.c_void_p
)

_lib.neam_register_tool.restype = ctypes.c_int
_lib.neam_register_tool.argtypes = [
    ctypes.POINTER(_NeamRuntime),
    ctypes.c_char_p,
    ctypes.c_char_p,
    _ToolFunc,
    ctypes.c_void_p
]
_lib.neam_unregister_tool.restype = ctypes.c_int
_lib.neam_unregister_tool.argtypes = [ctypes.POINTER(_NeamRuntime), ctypes.c_char_p]
_lib.neam_has_tool.restype = ctypes.c_int
_lib.neam_has_tool.argtypes = [ctypes.POINTER(_NeamRuntime), ctypes.c_char_p]

# Agent API
_lib.neam_agent_create.restype = ctypes.POINTER(_NeamAgent)
_lib.neam_agent_create.argtypes = [
    ctypes.POINTER(_NeamRuntime),
    ctypes.c_char_p,
    ctypes.c_char_p,
    ctypes.c_char_p,
    ctypes.c_char_p
]
_lib.neam_agent_set_temperature.restype = None
_lib.neam_agent_set_temperature.argtypes = [ctypes.POINTER(_NeamAgent), ctypes.c_double]
_lib.neam_agent_set_context_from.restype = ctypes.c_int
_lib.neam_agent_set_context_from.argtypes = [ctypes.POINTER(_NeamAgent), ctypes.c_char_p]
_lib.neam_agent_ask.restype = ctypes.c_char_p
_lib.neam_agent_ask.argtypes = [ctypes.POINTER(_NeamAgent), ctypes.c_char_p]
_lib.neam_agent_add_handoff.restype = ctypes.c_int
_lib.neam_agent_add_handoff.argtypes = [
    ctypes.POINTER(_NeamAgent),
    ctypes.c_char_p,
    ctypes.c_char_p,
    ctypes.c_char_p
]
_lib.neam_agent_reset.restype = None
_lib.neam_agent_reset.argtypes = [ctypes.POINTER(_NeamAgent)]
_lib.neam_agent_free.restype = None
_lib.neam_agent_free.argtypes = [ctypes.POINTER(_NeamAgent)]

# Runner API
_lib.neam_runner_create.restype = ctypes.POINTER(_NeamRunner)
_lib.neam_runner_create.argtypes = [ctypes.POINTER(_NeamRuntime), ctypes.c_char_p, ctypes.c_int]
_lib.neam_runner_enable_tracing.restype = None
_lib.neam_runner_enable_tracing.argtypes = [ctypes.POINTER(_NeamRunner), ctypes.c_int]
_lib.neam_runner_run.restype = ctypes.POINTER(_NeamResult)
_lib.neam_runner_run.argtypes = [ctypes.POINTER(_NeamRunner), ctypes.c_char_p]
_lib.neam_runner_final_agent.restype = ctypes.c_char_p
_lib.neam_runner_final_agent.argtypes = [ctypes.POINTER(_NeamRunner)]
_lib.neam_runner_total_turns.restype = ctypes.c_int
_lib.neam_runner_total_turns.argtypes = [ctypes.POINTER(_NeamRunner)]
_lib.neam_runner_free.restype = None
_lib.neam_runner_free.argtypes = [ctypes.POINTER(_NeamRunner)]

# Task API
_lib.neam_task_create.restype = ctypes.POINTER(_NeamTask)
_lib.neam_task_create.argtypes = [ctypes.POINTER(_NeamRuntime), ctypes.c_char_p, ctypes.c_char_p]
_lib.neam_task_id.restype = ctypes.c_char_p
_lib.neam_task_id.argtypes = [ctypes.POINTER(_NeamTask)]
_lib.neam_task_submit.restype = ctypes.c_int
_lib.neam_task_submit.argtypes = [ctypes.POINTER(_NeamTask)]
_lib.neam_task_status.restype = ctypes.c_int
_lib.neam_task_status.argtypes = [ctypes.POINTER(_NeamTask)]
_lib.neam_task_wait.restype = ctypes.c_int
_lib.neam_task_wait.argtypes = [ctypes.POINTER(_NeamTask), ctypes.c_int]
_lib.neam_task_result.restype = ctypes.c_char_p
_lib.neam_task_result.argtypes = [ctypes.POINTER(_NeamTask)]
_lib.neam_task_cancel.restype = ctypes.c_int
_lib.neam_task_cancel.argtypes = [ctypes.POINTER(_NeamTask)]
_lib.neam_task_free.restype = None
_lib.neam_task_free.argtypes = [ctypes.POINTER(_NeamTask)]

# Agent Card API
_lib.neam_get_agent_card.restype = ctypes.c_char_p
_lib.neam_get_agent_card.argtypes = [ctypes.POINTER(_NeamRuntime), ctypes.c_char_p]
_lib.neam_register_agent_card.restype = ctypes.c_int
_lib.neam_register_agent_card.argtypes = [ctypes.POINTER(_NeamRuntime), ctypes.c_char_p, ctypes.c_char_p]

# Error handling
_lib.neam_get_last_error.restype = ctypes.POINTER(_NeamError)
_lib.neam_get_last_error.argtypes = [ctypes.POINTER(_NeamRuntime)]
_lib.neam_error_type.restype = ctypes.c_int
_lib.neam_error_type.argtypes = [ctypes.POINTER(_NeamError)]
_lib.neam_error_message.restype = ctypes.c_char_p
_lib.neam_error_message.argtypes = [ctypes.POINTER(_NeamError)]
_lib.neam_error_line.restype = ctypes.c_int
_lib.neam_error_line.argtypes = [ctypes.POINTER(_NeamError)]
_lib.neam_error_column.restype = ctypes.c_int
_lib.neam_error_column.argtypes = [ctypes.POINTER(_NeamError)]
_lib.neam_error_stack_trace.restype = ctypes.c_char_p
_lib.neam_error_stack_trace.argtypes = [ctypes.POINTER(_NeamError)]
_lib.neam_error_to_json.restype = ctypes.c_char_p
_lib.neam_error_to_json.argtypes = [ctypes.POINTER(_NeamError)]
_lib.neam_clear_error.restype = None
_lib.neam_clear_error.argtypes = [ctypes.POINTER(_NeamRuntime)]

# GC functions
_lib.neam_gc_pause.restype = None
_lib.neam_gc_pause.argtypes = [ctypes.POINTER(_NeamRuntime)]
_lib.neam_gc_resume.restype = None
_lib.neam_gc_resume.argtypes = [ctypes.POINTER(_NeamRuntime)]
_lib.neam_gc_collect.restype = None
_lib.neam_gc_collect.argtypes = [ctypes.POINTER(_NeamRuntime)]


# ============================================================================
# Helper Functions
# ============================================================================

def _encode(s: Optional[str]) -> Optional[bytes]:
    """Encode a string to bytes for C API."""
    if s is None:
        return None
    return s.encode("utf-8")


def _decode(b: Optional[bytes]) -> Optional[str]:
    """Decode bytes from C API to string."""
    if b is None:
        return None
    return b.decode("utf-8")


# ============================================================================
# Version Functions
# ============================================================================

def version() -> str:
    """Get the Neam runtime version string."""
    return _decode(_lib.neam_version()) or "unknown"


def version_major() -> int:
    """Get the major version number."""
    return _lib.neam_version_major()


def version_minor() -> int:
    """Get the minor version number."""
    return _lib.neam_version_minor()


def version_patch() -> int:
    """Get the patch version number."""
    return _lib.neam_version_patch()


# ============================================================================
# Import submodules
# ============================================================================

from .runtime import Runtime
from .agent import Agent
from .runner import Runner
from .tools import tool
