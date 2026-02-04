"""
Tests for the Neam Python runtime bindings.
"""

import pytest
import os
import sys

# Skip tests if libneam is not available
try:
    import neam
    NEAM_AVAILABLE = True
except ImportError:
    NEAM_AVAILABLE = False

pytestmark = pytest.mark.skipif(
    not NEAM_AVAILABLE,
    reason="libneam not available"
)


class TestVersion:
    """Test version information."""

    def test_version_string(self):
        """Version string should be valid."""
        version = neam.version()
        assert version
        assert "." in version

    def test_version_numbers(self):
        """Version numbers should be non-negative."""
        assert neam.version_major() >= 0
        assert neam.version_minor() >= 0
        assert neam.version_patch() >= 0


class TestRuntime:
    """Test Runtime class."""

    def test_create_runtime(self):
        """Should create a runtime."""
        runtime = neam.Runtime()
        assert runtime.is_valid

    def test_create_runtime_with_config(self):
        """Should create a runtime with config."""
        runtime = neam.Runtime({
            "max_stack_depth": 500,
            "enable_shell": False,
        })
        assert runtime.is_valid

    def test_context_manager(self):
        """Runtime should work as context manager."""
        with neam.Runtime() as runtime:
            assert runtime.is_valid
        assert not runtime.is_valid

    def test_run_simple(self):
        """Should run simple code."""
        runtime = neam.Runtime()
        result = runtime.run('emit "Hello"')
        assert not result.is_error
        assert "Hello" in result.output

    def test_run_math(self):
        """Should handle math expressions."""
        runtime = neam.Runtime()
        result = runtime.run('let x = 1 + 2 + 3; emit str(x);')
        assert not result.is_error
        assert "6" in result.output

    def test_run_error(self):
        """Should handle errors."""
        runtime = neam.Runtime()
        result = runtime.run('undefined_variable')
        assert result.is_error
        assert result.error is not None


class TestValue:
    """Test Value class."""

    def test_nil_value(self):
        """Should handle nil values."""
        value = neam.runtime.Value.from_python(None)
        assert value.is_nil
        assert value.to_python() is None

    def test_bool_value(self):
        """Should handle boolean values."""
        value = neam.runtime.Value.from_python(True)
        assert value.is_bool
        assert value.as_bool() is True
        assert value.to_python() is True

    def test_number_value(self):
        """Should handle number values."""
        value = neam.runtime.Value.from_python(42.5)
        assert value.is_number
        assert value.as_number() == 42.5
        assert value.to_python() == 42.5

    def test_string_value(self):
        """Should handle string values."""
        value = neam.runtime.Value.from_python("hello")
        assert value.is_string
        assert value.as_string() == "hello"
        assert value.to_python() == "hello"

    def test_list_value(self):
        """Should handle list values."""
        value = neam.runtime.Value.from_python([1, 2, 3])
        assert value.is_list
        result = value.to_python()
        assert result == [1.0, 2.0, 3.0]  # Numbers are floats

    def test_dict_value(self):
        """Should handle dict values."""
        value = neam.runtime.Value.from_python({"a": 1, "b": 2})
        assert value.is_map
        result = value.to_python()
        assert result["a"] == 1
        assert result["b"] == 2


class TestGlobals:
    """Test global variable access."""

    def test_set_and_get_global(self):
        """Should set and get globals."""
        runtime = neam.Runtime()
        runtime.set_global("my_var", 42)
        value = runtime.get_global("my_var")
        assert value is not None
        assert value.as_number() == 42.0

    def test_use_global_in_code(self):
        """Should use globals in Neam code."""
        runtime = neam.Runtime()
        runtime.set_global("greeting", "Hello")
        result = runtime.run('emit greeting')
        assert "Hello" in result.output


class TestTools:
    """Test tool registration."""

    def test_register_tool(self):
        """Should register a tool."""
        runtime = neam.Runtime()

        def add(a, b):
            return a + b

        runtime.register_tool("add", add, "Add two numbers")
        assert runtime.has_tool("add")

    def test_unregister_tool(self):
        """Should unregister a tool."""
        runtime = neam.Runtime()

        def dummy():
            pass

        runtime.register_tool("dummy", dummy)
        assert runtime.has_tool("dummy")

        runtime.unregister_tool("dummy")
        assert not runtime.has_tool("dummy")

    def test_tool_decorator(self):
        """Should use tool decorator."""
        @neam.tool(description="Multiply numbers")
        def multiply(a: float, b: float) -> float:
            return a * b

        info = neam.tools.get_tool_info(multiply)
        assert info is not None
        assert info["name"] == "multiply"
        assert "Multiply" in info["description"]


class TestBytecode:
    """Test bytecode compilation and execution."""

    def test_compile_and_run(self):
        """Should compile and run bytecode."""
        runtime = neam.Runtime()
        bytecode = runtime.compile('emit "Compiled!"')
        result = runtime.run_bytecode(bytecode)
        assert not result.is_error
        assert "Compiled!" in result.output

    def test_serialize_bytecode(self):
        """Should serialize and deserialize bytecode."""
        runtime = neam.Runtime()
        bytecode = runtime.compile('emit "Test"')
        data = bytecode.to_bytes()
        assert len(data) > 0

        loaded = neam.runtime.Bytecode.from_bytes(data)
        result = runtime.run_bytecode(loaded)
        assert "Test" in result.output


if __name__ == "__main__":
    pytest.main([__file__, "-v"])
