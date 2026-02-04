# Neam Project Packaging Guide

Complete guide to Neam project structure, TOML manifests, and package management.

## Table of Contents

- [Project Structure](#project-structure)
- [neam.toml Manifest](#neamtoml-manifest)
- [Dependencies](#dependencies)
- [Module System](#module-system)
- [Build Profiles](#build-profiles)
- [Deployment](#deployment)
- [Workspaces](#workspaces)

---

## Project Structure

### Standard Layout

```
my-project/
├── neam.toml           # Project manifest (required)
├── neam.lock           # Lock file (auto-generated)
├── README.md           # Project documentation
├── src/
│   ├── main.neam       # Main entry point
│   ├── lib.neam        # Library entry (for libraries)
│   └── modules/        # Additional modules
│       ├── utils.neam
│       └── agents.neam
├── tests/
│   ├── test_main.neam
│   └── test_agents.neam
├── examples/
│   └── basic.neam
└── build/              # Build output directory
    └── main.neamb
```

### Entry Points

| Type | Default Entry | Description |
|------|---------------|-------------|
| Binary | `src/main.neam` | Executable program |
| Library | `src/lib.neam` | Reusable library |
| Custom | Defined in manifest | Multiple entry points |

---

## neam.toml Manifest

### Complete Reference

```toml
# ============================================
# Project Metadata (Required)
# ============================================
neam_version = "1.0"

[project]
name = "my-project"           # Required: Project name
version = "0.1.0"             # Required: Semantic version
type = "binary"               # "binary" or "library"
description = "Description"   # Optional
authors = ["Name <email>"]    # Optional
license = "MIT"               # Optional

[project.entry_points]
main = "src/main.neam"        # Default entry point
api = "src/api.neam"          # Additional entry points

# ============================================
# Dependencies
# ============================================
[dependencies]
# Version constraint
utils = "1.0.0"
utils = "^1.0.0"              # Compatible with 1.x.x
utils = "~1.0.0"              # Compatible with 1.0.x

# Git repository
ai-tools = { git = "https://github.com/org/repo" }
ai-tools = { git = "https://github.com/org/repo", branch = "main" }
ai-tools = { git = "https://github.com/org/repo", tag = "v1.0.0" }
ai-tools = { git = "https://github.com/org/repo", rev = "abc123" }

# With features
ml-lib = { version = "2.0.0", features = ["gpu", "advanced"] }

# Path dependency (local)
local-lib = { path = "../my-lib" }

[dev-dependencies]
test-utils = "0.1.0"

# ============================================
# Scripts
# ============================================
[scripts]
build = "neamc src/main.neam -o build/main.neamb"
run = "neam build/main.neamb"
test = "neam-cli test tests/**/*.neam"
api = "neam-api --port 8080"
clean = "rm -rf build/"

# ============================================
# Features (Conditional Compilation)
# ============================================
[features]
default = ["basic"]           # Enabled by default
basic = []                    # Empty feature
advanced = ["ml", "rag"]      # Feature with dependencies
full = ["basic", "advanced"]  # Combines features

# ============================================
# Agent Configuration
# ============================================
[agent]
provider = "openai"           # Default LLM provider
model = "gpt-4o-mini"         # Default model
capabilities = ["text", "code"]

[agent.limits]
max-tokens-per-request = 4096
max-concurrent-tools = 5
timeout-seconds = 300
max-retries = 3

[agent.prompts]
system = "Default system prompt"
error-recovery = "Error handling prompt"

# ============================================
# Knowledge Base Defaults
# ============================================
[knowledge]
default-vector-store = "usearch"
default-embedding-model = "nomic-embed-text"
chunk-size = 200
chunk-overlap = 50

# ============================================
# Test Configuration
# ============================================
[test]
timeout = 60                  # Test timeout in seconds
parallel = true               # Run tests in parallel
coverage = true               # Enable coverage
coverage-threshold = 80       # Minimum coverage %
include = ["tests/**/*.neam"]
exclude = ["tests/fixtures/**"]

# ============================================
# Deployment
# ============================================
[deploy]
default-target = "docker"

[deploy.docker]
image = "my-project"
registry = "ghcr.io/myorg"
tag-format = "v{version}"

[deploy.kubernetes]
namespace = "default"
replicas = 3
cpu = "500m"
memory = "1Gi"
port = 8080

[deploy.serverless]
provider = "aws-lambda"
memory = 1024
timeout = 300

# ============================================
# Build Profiles
# ============================================
[profile.dev]
optimization = "none"
debug = true
source-maps = true

[profile.release]
optimization = "full"
debug = false
source-maps = false
strip = true

[profile.bench]
optimization = "full"
debug = true
```

---

## Dependencies

### Dependency Sources

| Source | Syntax | Description |
|--------|--------|-------------|
| Registry | `"1.0.0"` | Central package registry |
| Git | `{ git = "url" }` | Git repository |
| Path | `{ path = "../lib" }` | Local path |
| URL | `{ url = "https://..." }` | Direct download |

### Version Constraints

| Constraint | Example | Matches |
|------------|---------|---------|
| Exact | `"1.0.0"` | Only 1.0.0 |
| Caret | `"^1.0.0"` | 1.0.0 to <2.0.0 |
| Tilde | `"~1.0.0"` | 1.0.0 to <1.1.0 |
| Wildcard | `"1.*"` | Any 1.x.x |
| Range | `">=1.0.0, <2.0.0"` | Custom range |

### Lock File (neam.lock)

Auto-generated file that locks dependency versions:

```toml
# neam.lock - Do not edit manually

[[package]]
name = "utils"
version = "1.0.0"
source = "registry"
checksum = "sha256:..."

[[package]]
name = "ai-tools"
git = "https://github.com/org/repo"
revision = "abc123def456"
```

---

## Module System

### Module Resolution

Modules are resolved from `src/` directory:

| Import | File Location |
|--------|---------------|
| `import utils` | `src/utils.neam` |
| `import utils::helpers` | `src/utils/helpers.neam` |
| `import deep::nested::mod` | `src/deep/nested/mod.neam` |

### Visibility Levels

```neam
// Public - accessible everywhere
pub agent MyAgent { ... }
pub fun my_function() { ... }

// Private - only in this module (default)
agent PrivateAgent { ... }
fun private_function() { ... }

// Crate - accessible within the package
pub(crate) fun internal_helper() { ... }
```

### Visibility Rules

| Level | Accessible From |
|-------|-----------------|
| `pub` | Anywhere |
| `pub(crate)` | Same package/crate |
| `pub(super)` | Parent module |
| (none) | Same module only |

---

## Build Profiles

### Available Profiles

| Profile | Use Case | Command |
|---------|----------|---------|
| `dev` | Development | `neamc --profile dev` |
| `release` | Production | `neamc --profile release` |
| `bench` | Benchmarking | `neamc --profile bench` |

### Profile Settings

```toml
[profile.dev]
optimization = "none"    # none, basic, full
debug = true             # Include debug info
source-maps = true       # Generate source maps

[profile.release]
optimization = "full"
debug = false
source-maps = false
strip = true             # Strip symbols
```

---

## Deployment

### Docker

```toml
[deploy.docker]
image = "my-app"
registry = "ghcr.io/myorg"
tag-format = "v{version}"
```

Generated Dockerfile:
```dockerfile
FROM ubuntu:22.04
COPY neam-api /usr/local/bin/
COPY build/ /app/
WORKDIR /app
CMD ["neam-api", "--port", "8080"]
```

### Kubernetes

```toml
[deploy.kubernetes]
namespace = "production"
replicas = 3
cpu = "500m"
memory = "1Gi"
port = 8080
```

### Serverless

```toml
[deploy.serverless]
provider = "aws-lambda"   # aws-lambda, gcp-functions, azure-functions
memory = 1024
timeout = 300
```

---

## Workspaces

### Workspace Structure

```
my-workspace/
├── neam.toml             # Workspace manifest
├── packages/
│   ├── core/
│   │   ├── neam.toml
│   │   └── src/
│   ├── cli/
│   │   ├── neam.toml
│   │   └── src/
│   └── api/
│       ├── neam.toml
│       └── src/
```

### Workspace Manifest

```toml
[workspace]
members = [
  "packages/core",
  "packages/cli",
  "packages/api"
]

# Shared dependencies
[workspace.dependencies]
utils = "1.0.0"

# Shared settings inherited by members
[workspace.package]
version = "0.1.0"
authors = ["Team <team@example.com>"]
```

### Member Inheritance

Members can inherit from workspace:

```toml
# packages/core/neam.toml
[project]
name = "core"
version.workspace = true    # Inherit from workspace
authors.workspace = true

[dependencies]
utils.workspace = true      # Use workspace version
```

---

## CLI Commands

### Project Management

```bash
# Initialize new project
neam-cli init my-project
neam-cli init my-project --template agent-rag

# Add dependency
neam-cli add utils
neam-cli add ai-tools --git https://github.com/org/repo

# Remove dependency
neam-cli remove utils

# Update dependencies
neam-cli update
neam-cli update utils
```

### Building

```bash
# Build project
neam-cli build
neam-cli build --release
neam-cli build --profile bench

# Clean build artifacts
neam-cli clean
```

### Running

```bash
# Run default entry point
neam-cli run

# Run specific entry point
neam-cli run --entry api

# Run with arguments
neam-cli run -- --port 8080
```

### Testing

```bash
# Run all tests
neam-cli test

# Run specific test
neam-cli test tests/test_agents.neam

# Run with coverage
neam-cli test --coverage
```

### Scripts

```bash
# Run defined script
neam-cli run-script build
neam-cli run-script test

# List available scripts
neam-cli scripts
```

---

## Templates

### Available Templates

| Template | Description |
|----------|-------------|
| `agent-basic` | Single agent project |
| `agent-multi` | Multi-agent patterns |
| `agent-rag` | RAG-enabled agent |
| `library` | Reusable library |
| `workspace` | Multi-package workspace |

### Using Templates

```bash
neam-cli new my-project --template agent-rag
```

---

## Example Project

See `examples/sample_project/` for a complete example with:
- Full `neam.toml` manifest
- Multiple entry points
- Test suite
- Documentation

```bash
cd examples/sample_project
neamc src/main.neam -o build/main.neamb
neam build/main.neamb
```

---

## Best Practices

1. **Always use semantic versioning** for your project
2. **Lock dependencies** by committing `neam.lock`
3. **Use features** for optional functionality
4. **Organize modules** logically in `src/`
5. **Write tests** in `tests/` directory
6. **Document** your public API
7. **Use profiles** appropriately for dev/release builds

---

*See also: `BUILD_README.md` for build instructions*
