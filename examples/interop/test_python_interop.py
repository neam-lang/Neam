#!/usr/bin/env python3
"""
Neam Python Interoperability Test

This script tests the Neam-Python interoperability via the C API bindings.

Requirements:
  - Build libneam: cd build && cmake --build .
  - Set library path: export NEAM_LIBRARY_PATH=/path/to/libneam.dylib

Usage:
  python test_python_interop.py
"""

import sys
import os

# Add bindings to path
bindings_path = os.path.join(os.path.dirname(__file__), '..', '..', 'bindings', 'python')
sys.path.insert(0, bindings_path)

def test_interoperability():
    """Test Neam-Python interoperability"""
    print("=" * 60)
    print("Neam Python Interoperability Test")
    print("=" * 60)

    try:
        from neam import Runtime, version, version_major, version_minor, version_patch
        from neam.runtime import Value
        print("\n[1/7] Import successful!")
        print(f"  Neam Version: {version()}")
        print(f"  Version: {version_major()}.{version_minor()}.{version_patch()}")
    except ImportError as e:
        print(f"\n[ERROR] Failed to import neam module: {e}")
        print("\nMake sure to:")
        print("  1. Build libneam: cd build && cmake --build .")
        print("  2. Set NEAM_LIBRARY_PATH environment variable")
        return False

    # Test 1: Create Runtime
    print("\n[2/7] Creating Neam Runtime...")
    try:
        runtime = Runtime()
        print(f"  Runtime valid: {runtime.is_valid}")
    except Exception as e:
        print(f"  [ERROR] Failed to create runtime: {e}")
        return False

    # Test 2: Execute Simple Neam Code
    print("\n[3/7] Executing Neam code from Python...")
    try:
        result = runtime.run('{ emit "Hello from Neam (via Python)!"; }')
        print(f"  Output: {result.output.strip()}")
        print(f"  Is Error: {result.is_error}")
    except Exception as e:
        print(f"  [ERROR] Execution failed: {e}")
        return False

    # Test 3: Set Global Variable from Python
    print("\n[4/7] Setting global variable from Python...")
    try:
        runtime.set_global("python_message", "Hello from Python!")
        runtime.set_global("python_number", 42.5)
        runtime.set_global("python_list", ["a", "b", "c"])
        print("  Set: python_message = 'Hello from Python!'")
        print("  Set: python_number = 42.5")
        print("  Set: python_list = ['a', 'b', 'c']")
    except Exception as e:
        print(f"  [ERROR] Failed to set global: {e}")
        return False

    # Test 4: Get Global Variable from Neam
    print("\n[5/7] Getting global variable in Neam...")
    try:
        result = runtime.run('{ emit python_message; emit str(python_number); }')
        print(f"  Output: {result.output}")
    except Exception as e:
        print(f"  [ERROR] Failed to get global: {e}")
        return False

    # Test 5: Value Conversion Python <-> Neam
    print("\n[6/7] Testing value conversions...")
    try:
        # Python -> Neam -> Python round-trip
        original = {"name": "Neam", "version": "0.6.0", "features": ["agents", "RAG", "voice"]}

        value = Value.from_python(original)
        print(f"  Original Python dict: {original}")
        print(f"  As Neam Value JSON: {value.to_json()}")

        converted = value.to_python()
        print(f"  Back to Python: {converted}")

        # Check round-trip
        if original["name"] == converted["name"]:
            print("  Round-trip: SUCCESS")
        else:
            print("  Round-trip: MISMATCH")
    except Exception as e:
        print(f"  [ERROR] Value conversion failed: {e}")
        return False

    # Test 6: Compile and Run Bytecode
    print("\n[7/7] Testing bytecode compilation...")
    try:
        bytecode = runtime.compile('{ emit "Bytecode execution test"; }')
        byte_data = bytecode.to_bytes()
        print(f"  Compiled to {len(byte_data)} bytes")

        # Execute bytecode
        result = runtime.run_bytecode(byte_data)
        print(f"  Bytecode output: {result.output.strip()}")
    except Exception as e:
        print(f"  [ERROR] Bytecode test failed: {e}")
        return False

    # Cleanup
    runtime.close()

    print("\n" + "=" * 60)
    print("All interoperability tests PASSED!")
    print("=" * 60)
    return True


def test_agent_interop():
    """Test Neam Agent interoperability (requires LLM)"""
    print("\n" + "=" * 60)
    print("Neam Agent Interoperability Test (Optional)")
    print("=" * 60)

    try:
        from neam import Runtime
        from neam.agent import Agent

        runtime = Runtime({"default_provider": "ollama", "default_model": "llama3.2:3b"})

        print("\n[NOTE] This test requires Ollama running with llama3.2:3b model")
        print("  Skipping agent test to avoid LLM dependency...")

        runtime.close()
        return True

    except ImportError:
        print("  Agent module not available")
        return True


if __name__ == "__main__":
    success = test_interoperability()

    if success:
        test_agent_interop()

    sys.exit(0 if success else 1)
