"""
Neam Tools - Tool registration utilities.

This module provides decorators and utilities for registering
Python functions as Neam tools.
"""

from __future__ import annotations

import functools
import inspect
import json
from typing import Any, Callable, Dict, List, Optional, Type, TypeVar, get_type_hints

F = TypeVar("F", bound=Callable[..., Any])


class ToolRegistry:
    """
    Registry for managing tools across runtimes.

    The ToolRegistry allows you to define tools once and register
    them with multiple runtimes.

    Example:
        >>> registry = ToolRegistry()
        >>>
        >>> @registry.tool(description="Add two numbers")
        ... def add(a: float, b: float) -> float:
        ...     return a + b
        >>>
        >>> runtime = Runtime()
        >>> registry.register_all(runtime)
    """

    def __init__(self):
        self._tools: Dict[str, Dict[str, Any]] = {}

    def tool(
        self,
        name: Optional[str] = None,
        description: str = "",
    ) -> Callable[[F], F]:
        """
        Decorator to register a function as a tool.

        Args:
            name: Tool name (defaults to function name)
            description: Tool description

        Returns:
            Decorator function
        """
        def decorator(func: F) -> F:
            tool_name = name or func.__name__
            self._tools[tool_name] = {
                "func": func,
                "description": description or func.__doc__ or "",
                "name": tool_name,
            }
            return func
        return decorator

    def register_all(self, runtime) -> None:
        """
        Register all tools with a runtime.

        Args:
            runtime: The Neam runtime
        """
        for tool_name, tool_info in self._tools.items():
            runtime.register_tool(
                tool_name,
                tool_info["func"],
                tool_info["description"],
            )

    def list_tools(self) -> List[str]:
        """Get a list of registered tool names."""
        return list(self._tools.keys())


# Global default registry
_default_registry = ToolRegistry()


def tool(
    func: Optional[F] = None,
    *,
    name: Optional[str] = None,
    description: str = "",
) -> Any:
    """
    Decorator to register a function as a Neam tool.

    Can be used with or without arguments:

        @tool
        def my_tool(x: int) -> int:
            return x * 2

        @tool(description="Multiply by two")
        def my_tool(x: int) -> int:
            return x * 2

    The decorated function can then be registered with a runtime:

        runtime.register_tool("my_tool", my_tool)

    Or use the get_tool_info() function to get metadata:

        info = get_tool_info(my_tool)
        print(info["description"])

    Args:
        func: The function to decorate
        name: Tool name (defaults to function name)
        description: Tool description

    Returns:
        Decorated function with tool metadata
    """
    def decorator(f: F) -> F:
        tool_name = name or f.__name__
        tool_desc = description or f.__doc__ or ""

        # Store tool metadata
        f._neam_tool_info = {
            "name": tool_name,
            "description": tool_desc,
            "parameters": _extract_parameters(f),
        }

        # Register with default registry
        _default_registry._tools[tool_name] = {
            "func": f,
            "description": tool_desc,
            "name": tool_name,
        }

        @functools.wraps(f)
        def wrapper(*args, **kwargs):
            return f(*args, **kwargs)

        wrapper._neam_tool_info = f._neam_tool_info
        return wrapper

    if func is not None:
        return decorator(func)
    return decorator


def _extract_parameters(func: Callable) -> Dict[str, Dict[str, Any]]:
    """Extract parameter information from a function."""
    params = {}
    sig = inspect.signature(func)

    try:
        hints = get_type_hints(func)
    except Exception:
        hints = {}

    for param_name, param in sig.parameters.items():
        if param_name in ("self", "cls"):
            continue

        param_info: Dict[str, Any] = {}

        # Get type hint
        if param_name in hints:
            param_info["type"] = _type_to_string(hints[param_name])

        # Check for default value
        if param.default is not inspect.Parameter.empty:
            param_info["default"] = param.default
            param_info["required"] = False
        else:
            param_info["required"] = True

        params[param_name] = param_info

    return params


def _type_to_string(t: Type) -> str:
    """Convert a Python type to a string representation."""
    if t is str:
        return "string"
    elif t is int or t is float:
        return "number"
    elif t is bool:
        return "boolean"
    elif t is list:
        return "array"
    elif t is dict:
        return "object"
    elif hasattr(t, "__origin__"):
        # Handle generic types like List[int], Dict[str, Any]
        origin = getattr(t, "__origin__", None)
        if origin is list:
            return "array"
        elif origin is dict:
            return "object"
    return "any"


def get_tool_info(func: Callable) -> Optional[Dict[str, Any]]:
    """
    Get tool metadata from a decorated function.

    Args:
        func: A function decorated with @tool

    Returns:
        Tool info dictionary, or None if not a tool
    """
    return getattr(func, "_neam_tool_info", None)


def generate_schema(func: Callable) -> Dict[str, Any]:
    """
    Generate a JSON Schema for a tool function.

    This creates a schema that describes the function's parameters
    in JSON Schema format, suitable for use with LLM tool calling.

    Args:
        func: The function to generate schema for

    Returns:
        JSON Schema dictionary

    Example:
        >>> @tool(description="Search the web")
        ... def search(query: str, limit: int = 10) -> List[Dict]:
        ...     pass
        >>>
        >>> schema = generate_schema(search)
        >>> print(json.dumps(schema, indent=2))
    """
    info = get_tool_info(func)
    if not info:
        # Try to extract from function directly
        params = _extract_parameters(func)
        info = {
            "name": func.__name__,
            "description": func.__doc__ or "",
            "parameters": params,
        }

    properties = {}
    required = []

    for param_name, param_info in info.get("parameters", {}).items():
        prop = {"type": param_info.get("type", "string")}
        if "default" in param_info:
            prop["default"] = param_info["default"]
        properties[param_name] = prop

        if param_info.get("required", True):
            required.append(param_name)

    return {
        "type": "object",
        "properties": properties,
        "required": required,
    }


def register_tools(runtime, *funcs: Callable) -> None:
    """
    Register multiple tool functions with a runtime.

    Args:
        runtime: The Neam runtime
        *funcs: Functions decorated with @tool

    Example:
        >>> @tool(description="Add numbers")
        ... def add(a: float, b: float) -> float:
        ...     return a + b
        >>>
        >>> @tool(description="Multiply numbers")
        ... def multiply(a: float, b: float) -> float:
        ...     return a * b
        >>>
        >>> register_tools(runtime, add, multiply)
    """
    for func in funcs:
        info = get_tool_info(func)
        if info:
            runtime.register_tool(
                info["name"],
                func,
                info["description"],
            )
        else:
            # Register using function name
            runtime.register_tool(
                func.__name__,
                func,
                func.__doc__ or "",
            )
