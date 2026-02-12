# Neam v0.7.1 — macOS (Apple Silicon) Release

## What's New in v0.7.1

**Object-Oriented Programming System** — Full OOP support for the Neam DSL:

- **struct + impl**: Define data types with methods, static methods, copy-with, structural equality
- **trait**: Interfaces with default method implementations
- **sealed + match**: Algebraic data types with exhaustive pattern matching and destructuring
- **extend**: Add methods to existing types after definition
- **Agentic patterns**: `pipeline`, `dispatch`, `parallel`, `loop` orchestration primitives
- **Property observers**: `willSet`, `didSet`, `guard` on mutable struct fields
- **Generic type params**: Type-erased generics (`struct Wrapper<T> { inner: T }`)

## Included Binaries

| Binary     | Description                              |
|------------|------------------------------------------|
| `neamc`    | Neam compiler — compiles `.neam` → `.neamx` bytecode |
| `neam`     | Neam runtime — executes `.neamx` bundles |
| `neam-cli` | Interactive REPL with autocomplete       |
| `neam-api` | HTTP API server for Neam agents          |
| `neam-lsp` | Language Server Protocol for IDE support |
| `neam-dap` | Debug Adapter Protocol for debugging     |
| `neam-pkg` | Package manager                          |
| `neam-gym` | Training/evaluation environment          |

## Installation

### Quick Install

```bash
chmod +x install.sh
./install.sh
```

Installs to `/usr/local/bin` by default (may require `sudo`).

### Custom Install Path

```bash
./install.sh /path/to/bin
```

### Manual Install

```bash
sudo cp neamc neam neam-cli neam-api neam-lsp neam-dap neam-pkg neam-gym /usr/local/bin/
```

## Quick Start

```bash
# Compile a Neam program
neamc hello.neam -o hello.neamx

# Run it
neam hello.neamx

# Interactive REPL
neam-cli
```

## System Requirements

- macOS 13+ (Ventura or later)
- Apple Silicon (M1/M2/M3/M4)
- libcurl (included with macOS)

## Build Info

- Architecture: arm64 (Apple Silicon)
- Build Type: Release (optimized)
- Compiler: Apple Clang
