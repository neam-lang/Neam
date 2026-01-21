# Neam Build Guide

Complete build instructions for the Neam Agentic AI Programming Language on macOS, Linux, and Windows.

## Table of Contents

- [Prerequisites](#prerequisites)
- [macOS Build](#macos-build)
- [Linux Build](#linux-build)
- [Windows Build](#windows-build)
- [Build Outputs](#build-outputs)
- [Running Neam](#running-neam)
- [Troubleshooting](#troubleshooting)

---

## Prerequisites

### All Platforms

| Requirement | Version | Notes |
|------------|---------|-------|
| CMake | 3.16+ | Build system generator |
| C++ Compiler | C++17 support | GCC 8+, Clang 7+, or MSVC 2019+ |
| Git | Any recent | For cloning (optional) |
| Internet | Required | CMake fetches dependencies automatically |

### Auto-Downloaded Dependencies

The following are automatically downloaded by CMake during configuration:

| Library | Version | Purpose |
|---------|---------|---------|
| tree-sitter | v0.22.6 | Parser generator |
| nlohmann/json | v3.11.3 | JSON handling |
| json-schema-validator | v2.1.0 | JSON schema validation |
| miniz | v3.0.2 | ZIP compression |
| usearch | v2.6.0 | Vector search (RAG) |
| lexbor | v2.2.0 | HTML parsing |
| googletest | v1.14.0 | Testing framework |
| benchmark | v1.8.3 | Performance benchmarks |

### System Dependencies (Manual Install Required)

| Library | Purpose |
|---------|---------|
| CURL | HTTP client for web sources and LLM APIs |
| OpenSSL | Cryptographic functions |

---

## macOS Build

### Tested On
- macOS Sequoia 15.x (Darwin 25.2.0)
- Apple Silicon (M1/M2/M3) and Intel

### Step 1: Install Xcode Command Line Tools

```bash
xcode-select --install
```

### Step 2: Install Homebrew (if not installed)

```bash
/bin/bash -c "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/HEAD/install.sh)"
```

### Step 3: Install Dependencies

```bash
brew install cmake curl openssl
```

### Step 4: Clone Repository (if needed)

```bash
git clone <repository-url> Neam
cd Neam
```

### Step 5: Build

```bash
# Create build directory
mkdir -p build && cd build

# Configure (Release build recommended)
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build all targets (uses all CPU cores)
cmake --build . --parallel $(sysctl -n hw.ncpu)
```

### Step 6: Verify Build

```bash
# Check executables exist
ls -la neamc neam neam-cli neam-lsp neam-dap neam-gym

# Test compiler version
./neamc --help
```

### macOS Troubleshooting

**OpenSSL not found:**
```bash
# If Homebrew OpenSSL isn't detected:
cmake .. -DOPENSSL_ROOT_DIR=$(brew --prefix openssl)
```

**Architecture mismatch (Rosetta):**
```bash
# Force native architecture
cmake .. -DCMAKE_OSX_ARCHITECTURES=arm64  # For Apple Silicon
cmake .. -DCMAKE_OSX_ARCHITECTURES=x86_64 # For Intel
```

---

## Linux Build

### Tested Distributions
- Ubuntu 20.04 / 22.04 / 24.04
- Debian 11 / 12
- Fedora 38+
- Arch Linux

### Ubuntu / Debian

```bash
# Update package list
sudo apt update

# Install build tools
sudo apt install -y build-essential cmake git

# Install dependencies
sudo apt install -y libcurl4-openssl-dev libssl-dev
```

### Fedora / RHEL / CentOS

```bash
# Install build tools
sudo dnf groupinstall -y "Development Tools"
sudo dnf install -y cmake git

# Install dependencies
sudo dnf install -y libcurl-devel openssl-devel
```

### Arch Linux

```bash
# Install build tools and dependencies
sudo pacman -S base-devel cmake git curl openssl
```

### Build Steps (All Linux)

```bash
# Clone repository (if needed)
git clone <repository-url> Neam
cd Neam

# Create build directory
mkdir -p build && cd build

# Configure
cmake .. -DCMAKE_BUILD_TYPE=Release

# Build (uses all CPU cores)
cmake --build . --parallel $(nproc)
```

### Verify Build

```bash
# Check executables
ls -la neamc neam neam-cli neam-lsp neam-dap neam-gym

# Test
./neamc --help
```

### Linux Troubleshooting

**GCC version too old:**
```bash
# Ubuntu/Debian - Install newer GCC
sudo apt install -y gcc-11 g++-11
cmake .. -DCMAKE_C_COMPILER=gcc-11 -DCMAKE_CXX_COMPILER=g++-11
```

**Missing libcurl:**
```bash
# Ubuntu/Debian
sudo apt install -y libcurl4-openssl-dev

# Or with GnuTLS instead of OpenSSL
sudo apt install -y libcurl4-gnutls-dev
```

---

## Windows Build

### Option A: Visual Studio (Recommended)

#### Prerequisites

1. **Visual Studio 2019 or 2022** with:
   - "Desktop development with C++" workload
   - CMake tools for Windows

2. **vcpkg** (Package manager):
   ```powershell
   git clone https://github.com/Microsoft/vcpkg.git C:\vcpkg
   cd C:\vcpkg
   .\bootstrap-vcpkg.bat
   .\vcpkg integrate install
   ```

3. **Install dependencies via vcpkg:**
   ```powershell
   .\vcpkg install curl:x64-windows openssl:x64-windows
   ```

#### Build Steps

```powershell
# Clone repository
git clone <repository-url> Neam
cd Neam

# Create build directory
mkdir build
cd build

# Configure with vcpkg toolchain
cmake .. -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build . --config Release --parallel
```

#### Executables Location

```
build\Release\neamc.exe
build\Release\neam.exe
build\Release\neam-cli.exe
build\Release\neam-lsp.exe
build\Release\neam-dap.exe
build\Release\neam-gym.exe
```

### Option B: MSYS2 / MinGW-w64

#### Install MSYS2

Download from: https://www.msys2.org/

#### Install Dependencies (MSYS2 UCRT64 terminal)

```bash
# Update MSYS2
pacman -Syu

# Install build tools
pacman -S mingw-w64-ucrt-x86_64-gcc mingw-w64-ucrt-x86_64-cmake mingw-w64-ucrt-x86_64-make git

# Install dependencies
pacman -S mingw-w64-ucrt-x86_64-curl mingw-w64-ucrt-x86_64-openssl
```

#### Build Steps (MSYS2 UCRT64)

```bash
# Clone and enter directory
git clone <repository-url> Neam
cd Neam

# Create build directory
mkdir -p build && cd build

# Configure
cmake .. -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release

# Build
cmake --build . --parallel $(nproc)
```

### Option C: WSL2 (Windows Subsystem for Linux)

Follow the [Linux Build](#linux-build) instructions inside your WSL2 distribution.

```powershell
# Install WSL2 with Ubuntu
wsl --install -d Ubuntu
```

Then open Ubuntu terminal and follow Linux instructions.

---

## Build Outputs

After a successful build, the following executables are created in the `build/` directory:

| Executable | Description |
|------------|-------------|
| `neamc` | Neam compiler - compiles `.neam` to `.neamb` bytecode |
| `neam` | Neam VM - executes `.neamb` bytecode files |
| `neam-cli` | Interactive REPL and CLI tool |
| `neam-lsp` | Language Server Protocol server for IDE integration |
| `neam-dap` | Debug Adapter Protocol server for debugging |
| `neam-gym` | Training/benchmarking environment |
| `libneam.dylib/so/dll` | Shared library for embedding |

---

## Running Neam

### Basic Usage

```bash
# Compile a Neam program
./build/neamc program.neam -o program.neamb

# Run the bytecode
./build/neam program.neamb

# One-liner (compile and run)
./build/neamc program.neam -o /tmp/out.neamb && ./build/neam /tmp/out.neamb
```

### Example: Hello World with Agent

Create `hello_agent.neam`:
```neam
agent Greeter {
  provider: "ollama"
  model: "qwen3:1.7b"
  system: "You are a friendly assistant. Keep responses brief."
}

{
  emit "Hello from Neam!";
  let response = Greeter.ask("Say hello in a creative way");
  emit response;
}
```

Run:
```bash
./build/neamc hello_agent.neam -o /tmp/hello.neamb && ./build/neam /tmp/hello.neamb
```

### LLM Provider Setup

Neam requires an LLM backend. Currently supported:

#### Ollama (Recommended for local development)

```bash
# Install Ollama (macOS)
brew install ollama

# Install Ollama (Linux)
curl -fsSL https://ollama.com/install.sh | sh

# Start Ollama server
ollama serve

# Pull a model (in another terminal)
ollama pull qwen3:1.7b
ollama pull nomic-embed-text  # For RAG embeddings
```

#### OpenAI API

Set environment variable:
```bash
export OPENAI_API_KEY="your-api-key"
```

Use in Neam:
```neam
agent MyAgent {
  provider: "openai"
  model: "gpt-4"
  system: "You are a helpful assistant."
}
```

---

## Running Tests

```bash
cd build

# Run all tests
ctest --output-on-failure

# Run specific test
./vm_test
./compiler_test
```

---

## Build Options

| CMake Option | Default | Description |
|--------------|---------|-------------|
| `CMAKE_BUILD_TYPE` | Debug | Build type: Debug, Release, RelWithDebInfo |
| `BUILD_TESTING` | ON | Build test executables |

Example:
```bash
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_TESTING=OFF
```

---

## Troubleshooting

### Common Issues

#### 1. CMake version too old

```bash
# macOS
brew upgrade cmake

# Ubuntu/Debian
sudo apt install -y cmake  # Or use pip: pip install cmake

# Or download from https://cmake.org/download/
```

#### 2. FetchContent download failures

If CMake fails to download dependencies:
```bash
# Clear CMake cache and retry
rm -rf build/_deps build/CMakeCache.txt
cmake ..
```

#### 3. Compiler errors about C++17 features

Ensure your compiler supports C++17:
```bash
# Check GCC version (need 8+)
g++ --version

# Check Clang version (need 7+)
clang++ --version
```

#### 4. CURL not found

```bash
# macOS
brew install curl

# Ubuntu/Debian
sudo apt install -y libcurl4-openssl-dev

# Fedora
sudo dnf install -y libcurl-devel
```

#### 5. OpenSSL not found

```bash
# macOS (specify path)
cmake .. -DOPENSSL_ROOT_DIR=$(brew --prefix openssl)

# Ubuntu/Debian
sudo apt install -y libssl-dev

# Fedora
sudo dnf install -y openssl-devel
```

#### 6. Out of memory during build

Reduce parallel jobs:
```bash
cmake --build . --parallel 2  # Use 2 cores instead of all
```

#### 7. Permission denied

```bash
# Don't use sudo for build
# If needed, fix permissions:
sudo chown -R $(whoami) build/
```

---

## Directory Structure

```
Neam/
├── CMakeLists.txt          # Main build configuration
├── NeamC/
│   ├── include/            # Header files
│   │   └── neamc/
│   │       ├── ast.hpp     # Abstract Syntax Tree
│   │       ├── parser.hpp  # Parser definitions
│   │       ├── compiler.hpp
│   │       └── vm/         # Virtual Machine headers
│   └── src/                # Source files
│       ├── neamc/          # Compiler main
│       ├── vm/             # VM implementation
│       ├── lsp/            # Language Server
│       └── dap/            # Debug Adapter
├── examples/               # Example Neam programs
├── tests/                  # Test suites
├── stdlib/                 # Standard library
└── build/                  # Build output (created by cmake)
```

---

## Quick Reference

```bash
# Full build from scratch (macOS/Linux)
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel

# Clean rebuild
rm -rf build && mkdir build && cd build
cmake .. && cmake --build .

# Run example
./neamc ../examples/rag_basic_strategies.neam -o /tmp/test.neamb
./neam /tmp/test.neamb
```

---

## Support

- Issues: Report bugs and feature requests on the project repository
- Documentation: See `readme.md` for language documentation
- Examples: Check the `examples/` directory for working code samples

---

*Last verified: January 2026 on macOS Sequoia 15.x (Darwin 25.2.0)*
