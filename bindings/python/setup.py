#!/usr/bin/env python3
"""
Neam Python Bindings Setup

This script builds and installs the Neam Python package.
The package uses ctypes to interface with the libneam shared library.
"""

import os
import sys
import platform
from pathlib import Path
from setuptools import setup, find_packages

# Determine library extension based on platform
def get_lib_extension():
    system = platform.system()
    if system == "Darwin":
        return ".dylib"
    elif system == "Windows":
        return ".dll"
    else:
        return ".so"

def find_libneam():
    """Find the libneam shared library."""
    lib_name = "libneam" + get_lib_extension()

    # Search paths in order of preference
    search_paths = [
        # Local build directory
        Path(__file__).parent.parent.parent / "build" / lib_name,
        # Installed system locations
        Path("/usr/local/lib") / lib_name,
        Path("/usr/lib") / lib_name,
        # Homebrew on macOS
        Path("/opt/homebrew/lib") / lib_name,
        # Windows common locations
        Path("C:/Program Files/Neam/lib") / lib_name,
    ]

    for path in search_paths:
        if path.exists():
            return str(path)

    return None

# Try to find the library
lib_path = find_libneam()
if lib_path:
    print(f"Found libneam at: {lib_path}")
else:
    print("Warning: libneam not found. Build it first with 'cmake --build build'")

# Read long description from README if it exists
long_description = ""
readme_path = Path(__file__).parent / "README.md"
if readme_path.exists():
    long_description = readme_path.read_text(encoding="utf-8")

setup(
    name="neam",
    version="0.6.0",
    description="Python bindings for the Neam agentic programming language",
    long_description=long_description,
    long_description_content_type="text/markdown",
    author="Neam Team",
    author_email="team@neam-lang.org",
    url="https://github.com/neam-lang/neam",
    license="Apache-2.0",
    packages=find_packages(),
    python_requires=">=3.9",
    classifiers=[
        "Development Status :: 4 - Beta",
        "Intended Audience :: Developers",
        "License :: OSI Approved :: Apache Software License",
        "Operating System :: OS Independent",
        "Programming Language :: Python :: 3",
        "Programming Language :: Python :: 3.9",
        "Programming Language :: Python :: 3.10",
        "Programming Language :: Python :: 3.11",
        "Programming Language :: Python :: 3.12",
        "Topic :: Software Development :: Interpreters",
        "Topic :: Scientific/Engineering :: Artificial Intelligence",
    ],
    extras_require={
        "dev": [
            "pytest>=7.0",
            "pytest-cov>=4.0",
            "black>=23.0",
            "mypy>=1.0",
        ],
    },
    package_data={
        "neam": ["py.typed", "*.pyi"],
    },
    include_package_data=True,
)
