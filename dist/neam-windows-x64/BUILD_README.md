# Neam v0.7.1 — Windows (x86_64) Release

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

| Binary       | Description                              |
|--------------|------------------------------------------|
| `neamc.exe`  | Neam compiler — compiles `.neam` → `.neamx` bytecode |
| `neam.exe`   | Neam runtime — executes `.neamx` bundles |
| `neam-api.exe` | HTTP API server for Neam agents        |
| `neam-lsp.exe` | Language Server Protocol for IDE support |
| `neam-pkg.exe` | Package manager                        |
| `neam-gym.exe` | Training/evaluation environment        |

**Note:** `neam-cli` (REPL) and `neam-dap` (debugger) require platform-specific terminal/socket
APIs and are available in the macOS build. Windows versions will be available in a future release.

## Installation

### Quick Install (as Administrator)

```cmd
install.bat
```

Installs to `C:\Program Files\Neam` by default.

### Custom Path

```cmd
install.bat C:\neam
```

### Add to PATH

After installation, add the install directory to your PATH:

```cmd
setx PATH "%PATH%;C:\Program Files\Neam"
```

## Quick Start

```cmd
REM Compile a Neam program
neamc hello.neam -o hello.neamx

REM Run it
neam hello.neamx
```

## System Requirements

- Windows 10 or later (x86_64)
- No additional runtime dependencies (statically linked)
