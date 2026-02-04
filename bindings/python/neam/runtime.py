"""
Neam Runtime - Core execution environment.

This module provides the Runtime class which is the main entry point
for executing Neam code from Python.
"""

from __future__ import annotations

import json
from typing import Any, Callable, Dict, List, Optional, Union

from . import (
    _lib,
    _NeamRuntime,
    _NeamResult,
    _NeamValue,
    _NeamBytecode,
    _ToolFunc,
    _encode,
    _decode,
    ValueType,
    ErrorType,
    NeamError,
    ParseError,
    CompileError,
    RuntimeError as NeamRuntimeError,
)


class Value:
    """
    Represents a Neam value.

    Values can be nil, bool, number, string, list, map, or other types.
    Use the type property to check the type, and the appropriate accessor
    to get the Python value.
    """

    def __init__(self, handle):
        self._handle = handle

    def __del__(self):
        if self._handle:
            _lib.neam_value_free(self._handle)
            self._handle = None

    @property
    def type(self) -> int:
        """Get the value type."""
        if not self._handle:
            return ValueType.NIL
        return _lib.neam_value_type(self._handle)

    @property
    def is_nil(self) -> bool:
        """Check if value is nil."""
        return self.type == ValueType.NIL

    @property
    def is_bool(self) -> bool:
        """Check if value is a boolean."""
        return self.type == ValueType.BOOL

    @property
    def is_number(self) -> bool:
        """Check if value is a number."""
        return self.type == ValueType.NUMBER

    @property
    def is_string(self) -> bool:
        """Check if value is a string."""
        return self.type == ValueType.STRING

    @property
    def is_list(self) -> bool:
        """Check if value is a list."""
        return self.type == ValueType.LIST

    @property
    def is_map(self) -> bool:
        """Check if value is a map."""
        return self.type == ValueType.MAP

    def as_bool(self) -> bool:
        """Get the boolean value."""
        return bool(_lib.neam_value_as_bool(self._handle))

    def as_number(self) -> float:
        """Get the numeric value."""
        return _lib.neam_value_as_number(self._handle)

    def as_string(self) -> str:
        """Get the string value."""
        return _decode(_lib.neam_value_as_string(self._handle)) or ""

    def as_list(self) -> List[Any]:
        """Get the value as a Python list."""
        if self.type != ValueType.LIST:
            raise TypeError("Value is not a list")
        length = _lib.neam_value_list_length(self._handle)
        result = []
        for i in range(length):
            item_handle = _lib.neam_value_list_get(self._handle, i)
            if item_handle:
                result.append(Value(item_handle).to_python())
        return result

    def as_dict(self) -> Dict[str, Any]:
        """Get the value as a Python dict."""
        if self.type != ValueType.MAP:
            raise TypeError("Value is not a map")
        # Convert to JSON and parse
        json_str = _decode(_lib.neam_value_to_json(self._handle))
        if json_str:
            return json.loads(json_str)
        return {}

    def to_python(self) -> Any:
        """Convert the value to a Python object."""
        t = self.type
        if t == ValueType.NIL:
            return None
        elif t == ValueType.BOOL:
            return self.as_bool()
        elif t == ValueType.NUMBER:
            return self.as_number()
        elif t == ValueType.STRING:
            return self.as_string()
        elif t == ValueType.LIST:
            return self.as_list()
        elif t == ValueType.MAP:
            return self.as_dict()
        else:
            # For other types, try JSON conversion
            json_str = _decode(_lib.neam_value_to_json(self._handle))
            if json_str:
                return json.loads(json_str)
            return None

    def to_json(self) -> str:
        """Convert the value to a JSON string."""
        return _decode(_lib.neam_value_to_json(self._handle)) or "null"

    @staticmethod
    def from_python(obj: Any) -> "Value":
        """Create a Value from a Python object."""
        if obj is None:
            return Value(_lib.neam_value_nil())
        elif isinstance(obj, bool):
            return Value(_lib.neam_value_bool(1 if obj else 0))
        elif isinstance(obj, (int, float)):
            return Value(_lib.neam_value_number(float(obj)))
        elif isinstance(obj, str):
            return Value(_lib.neam_value_string(_encode(obj), len(obj)))
        elif isinstance(obj, (list, tuple)):
            list_handle = _lib.neam_value_list_new()
            for item in obj:
                item_value = Value.from_python(item)
                _lib.neam_value_list_append(list_handle, item_value._handle)
                item_value._handle = None  # Ownership transferred
            return Value(list_handle)
        elif isinstance(obj, dict):
            map_handle = _lib.neam_value_map_new()
            for key, val in obj.items():
                val_value = Value.from_python(val)
                _lib.neam_value_map_set(map_handle, _encode(str(key)), val_value._handle)
                val_value._handle = None  # Ownership transferred
            return Value(map_handle)
        else:
            # Try JSON serialization
            json_str = json.dumps(obj)
            return Value(_lib.neam_value_from_json(_encode(json_str)))

    @staticmethod
    def from_json(json_str: str) -> "Value":
        """Create a Value from a JSON string."""
        handle = _lib.neam_value_from_json(_encode(json_str))
        if not handle:
            raise ValueError(f"Invalid JSON: {json_str}")
        return Value(handle)

    def __repr__(self) -> str:
        return f"Value({self.to_json()})"


class Error:
    """Represents a Neam error."""

    def __init__(self, handle):
        self._handle = handle

    @property
    def type(self) -> int:
        """Get the error type."""
        return _lib.neam_error_type(self._handle)

    @property
    def message(self) -> str:
        """Get the error message."""
        return _decode(_lib.neam_error_message(self._handle)) or ""

    @property
    def line(self) -> int:
        """Get the source line number."""
        return _lib.neam_error_line(self._handle)

    @property
    def column(self) -> int:
        """Get the source column number."""
        return _lib.neam_error_column(self._handle)

    @property
    def stack_trace(self) -> Optional[str]:
        """Get the stack trace."""
        return _decode(_lib.neam_error_stack_trace(self._handle))

    def to_exception(self) -> NeamError:
        """Convert to a Python exception."""
        t = self.type
        if t == ErrorType.PARSE:
            return ParseError(self.message, self.line, self.column)
        elif t == ErrorType.COMPILE:
            return CompileError(self.message, self.line, self.column)
        else:
            return NeamRuntimeError(self.message, self.stack_trace)

    def to_json(self) -> str:
        """Convert to JSON string."""
        return _decode(_lib.neam_error_to_json(self._handle)) or "{}"

    def __repr__(self) -> str:
        return f"Error({self.message})"


class Bytecode:
    """Represents compiled Neam bytecode."""

    def __init__(self, handle):
        self._handle = handle

    def __del__(self):
        if self._handle:
            _lib.neam_bytecode_free(self._handle)
            self._handle = None

    def to_bytes(self) -> bytes:
        """Serialize bytecode to bytes."""
        import ctypes
        size = ctypes.c_size_t()
        data = _lib.neam_bytecode_to_bytes(self._handle, ctypes.byref(size))
        if not data:
            raise RuntimeError("Failed to serialize bytecode")
        result = bytes(data[:size.value])
        _lib.neam_bytes_free(data)
        return result

    @staticmethod
    def from_bytes(data: bytes) -> "Bytecode":
        """Load bytecode from bytes."""
        import ctypes
        arr = (ctypes.c_uint8 * len(data))(*data)
        handle = _lib.neam_bytecode_from_bytes(arr, len(data))
        if not handle:
            raise ValueError("Invalid bytecode data")
        return Bytecode(handle)


class Result:
    """Represents the result of executing Neam code."""

    def __init__(self, handle):
        self._handle = handle

    def __del__(self):
        if self._handle:
            _lib.neam_result_free(self._handle)
            self._handle = None

    @property
    def is_error(self) -> bool:
        """Check if the result represents an error."""
        return bool(_lib.neam_result_is_error(self._handle))

    @property
    def output(self) -> str:
        """Get all output emitted during execution."""
        return _decode(_lib.neam_result_output(self._handle)) or ""

    @property
    def value(self) -> Optional[Value]:
        """Get the return value."""
        handle = _lib.neam_result_value(self._handle)
        if handle:
            return Value(handle)
        return None

    @property
    def error(self) -> Optional[Error]:
        """Get the error if any."""
        if not self.is_error:
            return None
        handle = _lib.neam_result_error(self._handle)
        if handle:
            return Error(handle)
        return None

    def raise_for_error(self) -> None:
        """Raise an exception if the result is an error."""
        if self.is_error:
            err = self.error
            if err:
                raise err.to_exception()
            raise NeamError("Unknown error")

    def __repr__(self) -> str:
        if self.is_error:
            return f"Result(error={self.error})"
        return f"Result(output={repr(self.output)}, value={self.value})"


class Runtime:
    """
    Neam Runtime - The core execution environment.

    The Runtime is the main entry point for executing Neam code from Python.
    It manages the virtual machine, global variables, and registered tools.

    Example:
        >>> runtime = Runtime()
        >>> result = runtime.run('let x = 1 + 2; emit str(x);')
        >>> print(result.output)
        3

    Args:
        config: Optional configuration dictionary with keys:
            - default_provider: Default LLM provider (e.g., "ollama", "openai")
            - default_model: Default model name
            - thread_safe: Enable thread-safe mode
            - max_stack_depth: Maximum call stack depth
            - enable_file_io: Allow file operations
            - enable_network: Allow network operations
            - enable_shell: Allow shell command execution
            - working_directory: Working directory for file operations
    """

    def __init__(self, config: Optional[Dict[str, Any]] = None):
        if config:
            config_json = json.dumps(config)
            self._handle = _lib.neam_runtime_new_with_config(_encode(config_json))
        else:
            self._handle = _lib.neam_runtime_new()

        if not self._handle:
            raise RuntimeError("Failed to create Neam runtime")

        # Keep references to registered tool callbacks to prevent GC
        self._tool_callbacks: Dict[str, Any] = {}

    def __del__(self):
        if self._handle:
            _lib.neam_runtime_free(self._handle)
            self._handle = None

    def __enter__(self) -> "Runtime":
        return self

    def __exit__(self, exc_type, exc_val, exc_tb) -> None:
        self.close()

    def close(self) -> None:
        """Close the runtime and free resources."""
        if self._handle:
            _lib.neam_runtime_free(self._handle)
            self._handle = None
            self._tool_callbacks.clear()

    @property
    def is_valid(self) -> bool:
        """Check if the runtime is valid."""
        return bool(self._handle and _lib.neam_runtime_is_valid(self._handle))

    def reset(self) -> None:
        """Reset the runtime state without destroying it."""
        _lib.neam_runtime_reset(self._handle)

    def run(self, source: str) -> Result:
        """
        Execute Neam source code.

        Args:
            source: Neam source code string

        Returns:
            Result object containing output and return value

        Example:
            >>> result = runtime.run('emit "Hello, World!"')
            >>> print(result.output)
            Hello, World!
        """
        handle = _lib.neam_run(self._handle, _encode(source))
        return Result(handle)

    def run_file(self, path: str) -> Result:
        """
        Execute a Neam source file.

        Args:
            path: Path to the source file

        Returns:
            Result object containing output and return value
        """
        handle = _lib.neam_run_file(self._handle, _encode(path))
        return Result(handle)

    def compile(self, source: str) -> Bytecode:
        """
        Compile Neam source code to bytecode.

        Args:
            source: Neam source code string

        Returns:
            Bytecode object

        Raises:
            ParseError: If the source has syntax errors
            CompileError: If compilation fails
        """
        handle = _lib.neam_compile(self._handle, _encode(source))
        if not handle:
            err = _lib.neam_get_last_error(self._handle)
            if err:
                raise Error(err).to_exception()
            raise CompileError("Compilation failed")
        return Bytecode(handle)

    def compile_file(self, path: str) -> Bytecode:
        """
        Compile a Neam source file to bytecode.

        Args:
            path: Path to the source file

        Returns:
            Bytecode object
        """
        handle = _lib.neam_compile_file(self._handle, _encode(path))
        if not handle:
            err = _lib.neam_get_last_error(self._handle)
            if err:
                raise Error(err).to_exception()
            raise CompileError("Compilation failed")
        return Bytecode(handle)

    def run_bytecode(self, bytecode: Union[Bytecode, bytes]) -> Result:
        """
        Execute compiled bytecode.

        Args:
            bytecode: Bytecode object or raw bytes

        Returns:
            Result object containing output and return value
        """
        if isinstance(bytecode, bytes):
            import ctypes
            arr = (ctypes.c_uint8 * len(bytecode))(*bytecode)
            handle = _lib.neam_run_bytecode_bytes(self._handle, arr, len(bytecode))
        else:
            handle = _lib.neam_run_bytecode(self._handle, bytecode._handle)
        return Result(handle)

    def get_global(self, name: str) -> Optional[Value]:
        """
        Get a global variable by name.

        Args:
            name: Variable name

        Returns:
            Value if found, None otherwise
        """
        handle = _lib.neam_get_global(self._handle, _encode(name))
        if handle:
            return Value(handle)
        return None

    def set_global(self, name: str, value: Any) -> None:
        """
        Set a global variable.

        Args:
            name: Variable name
            value: Python value to set
        """
        v = Value.from_python(value)
        result = _lib.neam_set_global(self._handle, _encode(name), v._handle)
        v._handle = None  # Ownership transferred
        if result != 0:
            raise RuntimeError(f"Failed to set global '{name}'")

    def register_tool(
        self,
        name: str,
        func: Callable,
        description: str = "",
    ) -> None:
        """
        Register a Python function as a Neam tool.

        Args:
            name: Tool name (used in Neam code)
            func: Python callable
            description: Tool description for agent context

        Example:
            >>> def add(a, b):
            ...     return a + b
            >>> runtime.register_tool("add", add, "Add two numbers")
        """
        import ctypes

        def c_callback(runtime_ptr, arg_count, args_ptr, user_data):
            try:
                # Convert arguments
                py_args = []
                for i in range(arg_count):
                    arg_handle = args_ptr[i]
                    if arg_handle:
                        py_args.append(Value(arg_handle).to_python())
                    else:
                        py_args.append(None)

                # Call Python function
                result = func(*py_args)

                # Convert result back to Neam value
                result_value = Value.from_python(result)
                handle = result_value._handle
                result_value._handle = None  # Transfer ownership
                return handle
            except Exception as e:
                # Return nil on error (could improve error handling)
                return _lib.neam_value_nil()

        # Create callback and keep reference
        callback = _ToolFunc(c_callback)
        self._tool_callbacks[name] = callback

        result = _lib.neam_register_tool(
            self._handle,
            _encode(name),
            _encode(description),
            callback,
            None
        )
        if result != 0:
            del self._tool_callbacks[name]
            raise RuntimeError(f"Failed to register tool '{name}'")

    def unregister_tool(self, name: str) -> bool:
        """
        Unregister a tool.

        Args:
            name: Tool name

        Returns:
            True if unregistered, False if not found
        """
        result = _lib.neam_unregister_tool(self._handle, _encode(name))
        if result == 0:
            self._tool_callbacks.pop(name, None)
            return True
        return False

    def has_tool(self, name: str) -> bool:
        """
        Check if a tool is registered.

        Args:
            name: Tool name

        Returns:
            True if registered
        """
        return bool(_lib.neam_has_tool(self._handle, _encode(name)))

    def gc_pause(self) -> None:
        """Pause garbage collection."""
        _lib.neam_gc_pause(self._handle)

    def gc_resume(self) -> None:
        """Resume garbage collection."""
        _lib.neam_gc_resume(self._handle)

    def gc_collect(self) -> None:
        """Force a garbage collection cycle."""
        _lib.neam_gc_collect(self._handle)

    def get_last_error(self) -> Optional[Error]:
        """Get the last error from the runtime."""
        handle = _lib.neam_get_last_error(self._handle)
        if handle:
            return Error(handle)
        return None

    def clear_error(self) -> None:
        """Clear the last error."""
        _lib.neam_clear_error(self._handle)
