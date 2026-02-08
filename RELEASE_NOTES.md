# Neam Release Notes — v0.6.2 through v0.6.7

## v0.6.7 — External Skill Adoption & Windows Cross-Compilation (February 2026)

### Highlights
Agents can now mix **local skills** with **external tools** from HTTP APIs, MCP servers, and Claude built-in tools — all flowing through the same guard/budget/dispatch infrastructure.

### New Language Features
- **`extern skill`** — Declare skills bound to external sources instead of local `impl()` functions
- **`mcp_server`** — Declare MCP server connections (stdio or SSE transport)
- **`adopt`** — Bulk-import tools from an MCP server (`adopt filesystem.*`, `adopt fs.{read_file, list} as fs`)

### Three External Binding Types
| Binding | Syntax | Use Case |
|---------|--------|----------|
| **HTTP API** | `binding: http { method, url, headers, response_path }` | REST APIs with URL templating and JSONPath extraction |
| **MCP Tool** | `binding: mcp { server, tool }` | Model Context Protocol tools via JSON-RPC |
| **Claude Built-in** | `binding: claude_builtin { type }` | `bash_20241022`, `text_editor_20241022` with local dispatch |

### Claude Built-in Local Dispatch
- `bash_20241022` executes commands via `popen()` — works with **any** LLM provider (OpenAI, Ollama, Bedrock)
- `text_editor_20241022` handles file read/write via `std::fstream`
- No dependency on Anthropic API for execution — portable across all providers

### Windows Cross-Compilation
- Full MinGW-w64 toolchain (`cmake/mingw-w64-x86_64.cmake`)
- Static linking of curl + LibreSSL for standalone Windows executables
- GCC 15 compatibility fixes (ERROR macro, `uint64_t`, shared_ptr template resolution)
- Produces `neamc.exe` and `neam.exe` (PE32+ x86-64)

### Bedrock SigV4 Fix
- Double URI-encode canonical path for correct AWS signature
- Use `AWS_REGION` env var directly instead of URL-based region extraction

### New Examples
- `v067_extern_skills.neam` — HTTP + MCP + Claude built-in demos
- `v067_brand_agent.neam` — Brand-guidelines agent using Claude bash skill
- `v067_pptx_agent.neam` — PowerPoint generation pipeline

---

## v0.6.6 — Native LLM Tool Calling (February 2026)

### Highlights
Neam skills are now **first-class LLM tools**. The LLM decides which skills to invoke via native function-calling protocols — no more text-based ReAct parsing.

### Native Tool Calling
- **Automatic schema generation** — Skill `params` auto-convert to JSON Schema for the LLM
- **Provider support** — Claude `tool_use`, OpenAI `function_calling`, Ollama OpenAI-compatible format
- **Multi-step orchestration** — LLM chains multiple tool calls autonomously (e.g., "Compare weather in Tokyo and London")
- **JSON-to-Value conversion** — Automatic parameter type mapping from LLM JSON to Neam values

### Guard & Budget Enforcement
- Every tool input/output passes through guard chains
- Budget tracking per tool call (api_calls, tokens, time limits)
- Capability checks enforced before skill execution

### Selective Skill Wiring
- Each agent declares which skills it can use: `skills: [calculate, weather, translate]`
- Agents without skills use plain chat (full backward compatibility)

### New Examples
- `v066_native_tool_calling.neam` — 10 scenarios: single tool, multi-step chains, cross-domain
- `v066_claude_skill_integration.neam` — 8 demos of Neam-to-LLM skill integration workflows

---

## v0.6.5 — Security Hardening & Reliability (February 2026)

### Highlights
A comprehensive hardening release addressing **all 36 gaps** from the v0.6.4 security assessment. No new features — only fixes, resilience, and test coverage.

### Critical Security Fixes (Phase 1)
- **TLS certificate verification** — CURLOPT_SSL_VERIFYPEER/VERIFYHOST on all LLM requests (CVSS 9.1 → 0)
- **SHA256 checksum verification** — EVP-based package integrity checks (replaced `return true` stub)
- **Command injection prevention** — `posix_spawn()` replaces `system()` for deploy scripts; `termios` replaces `stty`
- **API server authentication** — `NEAM_API_KEY` env var + `Authorization: Bearer` header required

### Reliability & Resilience (Phase 2)
- **HTTP connection pooling** — `curl_multi` handle pool (10-100x faster)
- **Exponential backoff retry** — 3 attempts with 1s/2s/4s for status 429, 500, 502, 503, 504
- **Call stack depth limit** — Default 1000 (configurable via `NEAM_MAX_CALL_DEPTH`)
- **GC reentrancy guard** — Prevents heap corruption during marking phase
- **Structured LLM logging** — Configurable via `NEAM_LOG_LEVEL` (DEBUG/INFO/WARN/ERROR)

### Build System & CI (Phase 3)
- CMake install targets for 8 executables, libraries, headers, stdlib
- GitHub Actions CI workflow (build, test, clang-format, CodeQL)
- Unified `VERSION` file — single source of truth
- `-Wall -Wextra -Wpedantic` compiler warnings enabled and resolved

### Compiler Integrity (Phase 4)
- Unimplemented features now emit clear errors instead of silent drops (`test`, `module`, `import`, `type alias`)
- Default cases added to all switch statements
- Type system honestly documented as planned, not implemented

### Test Coverage (Phase 5)
- **85 new test cases** (LLM adapters, HTTP client, package manager, integration)
- Coverage: **2.4% → 15.2%** (~650 → ~5,000 LoC tests)
- GoogleTest framework with custom matchers

### Code Cleanup (Phase 6)
- Hard-coded limits increased (list/map 255→65K, GC threshold configurable, ReAct steps configurable)
- Registry client rewritten with libcurl + nlohmann::json (replaced raw sockets + regex)
- Async runtime consolidated to single `runtime::Executor`

---

## v0.6.4 — Multi-Cloud Scaling, GPU/SIMD & FinOps (Mid-2025)

### Highlights
Enterprise-grade multi-cloud orchestration, GPU/SIMD acceleration, and FinOps cost management for production AI agent deployments.

### Multi-Cloud Orchestration
- **4 cloud providers** — AWS, GCP, Azure, Alibaba Cloud
- **Cost-aware routing** — Real-time spot price polling + latency evaluation
- **Routing constraints** — Max latency, max cost, preferred regions, data residency
- **Automatic failover** — After 3 consecutive errors (60s cooldown)
- **Deployment targets** — Docker, Kubernetes, Helm, Lambda, Cloud Run, Azure Functions, Terraform

### GPU/SIMD Acceleration
- **GPU backends** — CUDA, Metal, OpenCL, Vulkan, CPU fallback
- **SIMD ISAs** — AVX-512, AVX2, SSE4 (x86), NEON, SVE (ARM)
- **470+ accelerated operations** — Vector math, activation functions, quantization, similarity search
- **Auto-acceleration** — Transparent at compile time with CPU fallback

### Intelligent Auto-Scaling
- ML-based predictive scaling (5-min, 1-hour, 24-hour windows)
- Pattern detection (daily/weekly cycles, anomaly spikes)
- Warm pool for zero cold-start scale-out
- Spot ↔ on-demand ↔ serverless switching

### FinOps & Cost Management
- Real-time cost tracking per agent/task/user/project (10 categories)
- Per-agent budget controls with 80%/95% alerts
- FinOps dashboard (WebSocket, 14 widget types, JSON/PDF export)
- Continuous benchmarking with regression detection for CI/CD

### Multi-Language Benchmark Suite
- Bedrock adapter with AWS SigV4 signing
- Comparative benchmarks in Neam, Python, Go, Rust
- 82 test scenarios across 4 modalities

---

## v0.6.3 — OpenClaw Integration (Unreleased)

### Status
Feature branch only (`feature/v0.6.3-OpenClaw-integration`). Not merged to main. Development proceeded directly to v0.6.4.

---

## v0.6.2 — Foundation Release (Early 2025)

### Highlights
The initial public release establishing Neam as a compiled DSL for AI agents.

### Core Language
- Compiled bytecode VM (`neamc` compiler + `neam` runtime)
- Lexical scoping, first-class functions, closures
- String interpolation and pattern matching
- 25+ built-in math functions, JSON, time, file I/O, HTTP, crypto

### Agent System
- Agent declaration with provider/model configuration
- Multi-provider support: OpenAI, Ollama, Bedrock
- System prompt + temperature configuration
- `.ask()` method for agent queries

### Development Tools
- Interactive REPL (`neam-cli`) with editing, autocomplete, history
- Package manager (`neam-pkg`) with Supabase registry
- Project manifest (`neam.toml`) with dependencies, scripts, deploy targets

---

## Version Progression Summary

| Feature | v0.6.2 | v0.6.4 | v0.6.5 | v0.6.6 | v0.6.7 |
|---------|--------|--------|--------|--------|--------|
| Core Language & Agent System | ✓ | ✓ | ✓ | ✓ | ✓ |
| Multi-Cloud Orchestration | — | ✓ | ✓ | ✓ | ✓ |
| GPU/SIMD Acceleration | — | ✓ | ✓ | ✓ | ✓ |
| FinOps Cost Management | — | ✓ | ✓ | ✓ | ✓ |
| Security Hardening | — | — | ✓ | ✓ | ✓ |
| Native Tool Calling | — | — | — | ✓ | ✓ |
| External Skills (HTTP/MCP/Claude) | — | — | — | — | ✓ |
| Windows Cross-Compilation | — | — | — | — | ✓ |
| Test Coverage | 2.4% | 2.4% | 15.2% | 15.2% | 15.2% |

## Platforms

| Platform | Architecture | Status |
|----------|-------------|--------|
| macOS | ARM64 (Apple Silicon) | Release build |
| macOS | x86_64 | Supported |
| Linux | x86_64, ARM64 | Supported |
| Windows | x86_64 | Cross-compiled (MinGW-w64) |

## Breaking Changes

- **v0.6.4 → v0.6.5**: Unimplemented compiler features (`test`, `module`, `import`) now error instead of silently discarding
- **All other transitions**: Additive only — no breaking changes
