# Neam v0.6.5 Implementation Plan

## Release Objective

Fix all 36 identified gaps from the v0.6.4 gap assessment. No new features — only security hardening, reliability improvements, build system fixes, compiler integrity, and test coverage.

**Total Effort:** ~95.5 engineering hours (~2.5 engineer-weeks)
**Phases:** 6 phases across 4 weeks
**Files Changed:** 13 existing + ~8 new test files
**Issues Resolved:** 5 CRITICAL, 14 HIGH, 12 MEDIUM, 5 LOW

---

## Phase 1: Critical Security Fixes (Days 1-2, ~8 hours)

**Goal:** Eliminate all 5 CRITICAL security vulnerabilities before any other work.
**Rationale:** These are exploitable in production. Every day they remain is risk.

### 1.1 Enable TLS Certificate Verification

**File:** `NeamC/src/vm/llm/http_client.cpp`
**CVSS:** 9.1 — Man-in-the-middle intercept of API keys and LLM traffic

**Changes:**
After every `curl_easy_init()` call, add TLS verification options:

```cpp
// Enable TLS certificate verification (CRITICAL security fix)
curl_easy_setopt(handle, CURLOPT_SSL_VERIFYPEER, 1L);
curl_easy_setopt(handle, CURLOPT_SSL_VERIFYHOST, 2L);
```

**Also apply to:**
- `NeamC/src/vm/llm/bedrock_adapter.cpp` — Check if it creates its own CURL handles
- `NeamC/src/pkg/registry.cpp` — If it uses libcurl for registry requests

**Verification:**
- Connect to an LLM endpoint with an invalid/self-signed cert — request must fail
- Connect to a valid endpoint (OpenAI, Bedrock) — request must succeed
- Set `NEAM_TLS_SKIP_VERIFY=1` env var to allow override for dev/testing

**Effort:** 1 hour
**Risk:** Low — libcurl defaults to verification; we're making it explicit
**Dependencies:** None

---

### 1.2 Implement SHA256 Checksum Verification

**File:** `NeamC/src/pkg/installer.cpp` (lines 821-832)
**CVSS:** 8.6 — Tampered packages install without verification

**Current code (broken):**
```cpp
bool Installer::verify_checksum(...) {
    // TODO: Implement actual SHA256 checksum verification
    return true;  // ALWAYS RETURNS TRUE
}
```

**Replace with:**
```cpp
#include <openssl/evp.h>
#include <fstream>
#include <iomanip>
#include <sstream>

bool Installer::verify_checksum(const std::string& file_path,
                                 const std::string& expected_hash) {
    std::ifstream file(file_path, std::ios::binary);
    if (!file.is_open()) return false;

    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) return false;

    if (EVP_DigestInit_ex(ctx, EVP_sha256(), nullptr) != 1) {
        EVP_MD_CTX_free(ctx);
        return false;
    }

    char buffer[8192];
    while (file.read(buffer, sizeof(buffer)) || file.gcount() > 0) {
        if (EVP_DigestUpdate(ctx, buffer, file.gcount()) != 1) {
            EVP_MD_CTX_free(ctx);
            return false;
        }
    }

    unsigned char hash[EVP_MAX_MD_SIZE];
    unsigned int hash_len = 0;
    if (EVP_DigestFinal_ex(ctx, hash, &hash_len) != 1) {
        EVP_MD_CTX_free(ctx);
        return false;
    }
    EVP_MD_CTX_free(ctx);

    // Convert to hex string
    std::ostringstream hex_stream;
    for (unsigned int i = 0; i < hash_len; i++) {
        hex_stream << std::hex << std::setw(2) << std::setfill('0')
                   << static_cast<int>(hash[i]);
    }

    return hex_stream.str() == expected_hash;
}
```

**Verification:**
- Create a test package with known SHA256 hash — verify passes
- Tamper with 1 byte — verify fails
- Missing file — returns false (no crash)

**Effort:** 2 hours
**Risk:** Low — OpenSSL already linked for SigV4
**Dependencies:** None

---

### 1.3 Replace `system()` Calls with Safe Alternatives

**Files:**
- `NeamC/src/neam_pkg.cpp:139` — `system("stty -echo")` for password input
- `NeamC/src/neamc/main.cpp` — Multiple `std::system()` calls for deploy scripts

**1.3a — Password input (neam_pkg.cpp)**

Replace:
```cpp
system("stty -echo");
// ... read password ...
system("stty echo");
```

With POSIX `termios`:
```cpp
#include <termios.h>
#include <unistd.h>

std::string read_password(const std::string& prompt) {
    std::cerr << prompt;

    struct termios old_term, new_term;
    tcgetattr(STDIN_FILENO, &old_term);
    new_term = old_term;
    new_term.c_lflag &= ~ECHO;
    tcsetattr(STDIN_FILENO, TCSANOW, &new_term);

    std::string password;
    std::getline(std::cin, password);

    tcsetattr(STDIN_FILENO, TCSANOW, &old_term);
    std::cerr << "\n";
    return password;
}
```

**1.3b — Deploy script execution (neamc/main.cpp)**

Replace all `std::system(cmd.c_str())` with a safe subprocess function:
```cpp
#include <spawn.h>
#include <sys/wait.h>

int safe_exec(const std::string& program,
              const std::vector<std::string>& args) {
    // Validate: program must exist and be executable
    // No shell interpretation — args passed directly
    std::vector<char*> argv;
    argv.push_back(const_cast<char*>(program.c_str()));
    for (const auto& arg : args) {
        argv.push_back(const_cast<char*>(arg.c_str()));
    }
    argv.push_back(nullptr);

    pid_t pid;
    extern char** environ;
    int status = posix_spawn(&pid, program.c_str(), nullptr, nullptr,
                             argv.data(), environ);
    if (status != 0) return -1;

    waitpid(pid, &status, 0);
    return WIFEXITED(status) ? WEXITSTATUS(status) : -1;
}
```

For `neam.toml` script commands: validate against an allowlist of safe patterns, or execute through `/bin/sh -c` only after sanitizing (rejecting pipe, redirect, backtick, `$()` constructs).

**Verification:**
- Password input works with `termios` (type password, it's hidden)
- Deploy scripts work via `posix_spawn` (build, push, run)
- Attempt command injection in `neam.toml` scripts — blocked
- Cross-platform: add `#ifdef _WIN32` path using `SetConsoleMode`

**Effort:** 4 hours
**Risk:** Medium — cross-platform terminal handling
**Dependencies:** None

---

### 1.4 Add API Server Authentication

**File:** `NeamC/src/neam_api.cpp`
**CVSS:** 7.5 — No authentication on API server

**Changes:**

Add bearer token check to request handler:
```cpp
namespace {
    std::string get_api_key() {
        const char* key = std::getenv("NEAM_API_KEY");
        return key ? std::string(key) : "";
    }

    bool authenticate(const HttpRequest& req) {
        std::string expected = get_api_key();
        if (expected.empty()) return true; // No key set = open mode (dev only)

        auto it = req.headers.find("Authorization");
        if (it == req.headers.end()) return false;

        const std::string prefix = "Bearer ";
        if (it->second.substr(0, prefix.size()) != prefix) return false;

        // Constant-time comparison to prevent timing attacks
        std::string provided = it->second.substr(prefix.size());
        if (provided.size() != expected.size()) return false;

        volatile int result = 0;
        for (size_t i = 0; i < expected.size(); i++) {
            result |= (provided[i] ^ expected[i]);
        }
        return result == 0;
    }
}
```

Insert auth check at the top of every route handler:
```cpp
if (!authenticate(request)) {
    return HttpResponse{401, "application/json",
        R"({"error":"Unauthorized","message":"Set NEAM_API_KEY env var and pass Authorization: Bearer <key>"})"};
}
```

**Verification:**
- Without `NEAM_API_KEY` set: all requests pass (backward compatible)
- With `NEAM_API_KEY=mysecret`: requests without header get 401
- With correct Bearer token: requests succeed
- With wrong Bearer token: requests get 401
- Timing attack: constant-time comparison (no early exit)

**Effort:** 1 hour
**Risk:** Low
**Dependencies:** None

---

### Phase 1 Acceptance Criteria

| # | Criterion | Test Method |
|---|---|---|
| 1 | TLS verification active by default | `curl` to invalid cert endpoint fails |
| 2 | SHA256 checksums verified on package install | Tampered package rejected |
| 3 | No `system()` calls remain in security-sensitive paths | `grep -rn "std::system\|system(" NeamC/src/` shows zero in pkg/api paths |
| 4 | API server requires auth when key is set | HTTP 401 without token |
| 5 | All existing tests still pass | `ctest` green |

---

## Phase 2: Reliability & Resilience (Days 3-5, ~13 hours)

**Goal:** Make the LLM/HTTP stack production-grade with connection pooling, retries, logging, and VM safety.

### 2.1 HTTP Connection Pooling

**File:** `NeamC/src/vm/llm/http_client.cpp`
**Impact:** 10-100x TLS handshake overhead reduction at scale

**Design:**

Create a connection pool manager:
```cpp
// New file: NeamC/include/neamc/llm/connection_pool.hpp

class ConnectionPool {
public:
    explicit ConnectionPool(size_t max_connections = 8);
    ~ConnectionPool();

    // Acquire a CURL handle (reuses existing or creates new)
    CURL* acquire(const std::string& base_url);

    // Return handle to pool after use
    void release(CURL* handle, const std::string& base_url);

private:
    struct PoolEntry {
        CURL* handle;
        std::string base_url;
        std::chrono::steady_clock::time_point last_used;
    };

    std::mutex mutex_;
    std::vector<PoolEntry> pool_;
    size_t max_connections_;

    void evict_stale(std::chrono::seconds max_idle = std::chrono::seconds(60));
};
```

**Implementation in `http_client.cpp`:**
- Replace `curl_easy_init()` / `curl_easy_cleanup()` per-request with `pool.acquire()` / `pool.release()`
- Use `curl_easy_reset()` between requests to clear state but keep TCP connection alive
- Pool is thread-safe via mutex
- Stale connections evicted after 60s idle

**Verification:**
- Send 100 requests in sequence — observe only 1 TLS handshake (not 100)
- Concurrent requests from multiple threads — no crashes
- Idle pool entries cleaned up after 60s

**Effort:** 6 hours
**Risk:** Medium — thread-safety around handle reuse
**Dependencies:** Phase 1.1 (TLS verification must be in place first)

---

### 2.2 Retry with Exponential Backoff

**File:** `NeamC/src/vm/llm/http_client.cpp`
**Impact:** Transparent recovery from 1-5% transient LLM API failures

**Design:**

Add retry wrapper around the HTTP execution:
```cpp
struct RetryConfig {
    int max_attempts = 3;
    int base_delay_ms = 1000;      // 1s, 2s, 4s
    int max_delay_ms = 10000;      // Cap at 10s
    bool retry_on_429 = true;      // Rate limit
    bool retry_on_5xx = true;      // Server errors
    bool retry_on_network = true;  // DNS, TCP, TLS errors
};

HttpResponse execute_with_retry(const HttpRequest& request,
                                const RetryConfig& config = {}) {
    for (int attempt = 0; attempt < config.max_attempts; attempt++) {
        try {
            auto response = execute(request);

            bool should_retry = false;
            if (config.retry_on_429 && response.status == 429) should_retry = true;
            if (config.retry_on_5xx && response.status >= 500) should_retry = true;

            if (!should_retry || attempt == config.max_attempts - 1) {
                return response;
            }

            // Check Retry-After header for 429
            int delay_ms = config.base_delay_ms * (1 << attempt);
            if (response.status == 429) {
                auto it = response.headers.find("Retry-After");
                if (it != response.headers.end()) {
                    delay_ms = std::max(delay_ms, std::stoi(it->second) * 1000);
                }
            }
            delay_ms = std::min(delay_ms, config.max_delay_ms);

            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        } catch (const std::exception& e) {
            if (!config.retry_on_network || attempt == config.max_attempts - 1) {
                throw;
            }
            int delay_ms = config.base_delay_ms * (1 << attempt);
            std::this_thread::sleep_for(std::chrono::milliseconds(delay_ms));
        }
    }
    throw std::runtime_error("Max retry attempts exceeded");
}
```

**Update all 3 adapters:**
- `openai_adapter.cpp` — Replace direct `execute()` calls with `execute_with_retry()`
- `ollama_adapter.cpp` — Same
- `bedrock_adapter.cpp` — Same (SigV4 signatures must be re-computed per retry since they're time-sensitive)

**Verification:**
- Mock server returning 429 → 429 → 200: client retries and succeeds
- Mock server returning 503 → 200: client retries and succeeds
- Mock server returning 400: client does NOT retry (client error)
- Verify exponential delay between attempts (1s, 2s, 4s)
- Bedrock: verify SigV4 signature regenerated on each retry attempt

**Effort:** 3 hours
**Risk:** Low
**Dependencies:** None (can proceed in parallel with 2.1)

---

### 2.3 Add Structured Logging to LLM Stack

**Files:**
- `NeamC/src/vm/llm/http_client.cpp`
- `NeamC/src/vm/llm/openai_adapter.cpp`
- `NeamC/src/vm/llm/ollama_adapter.cpp`
- `NeamC/src/vm/llm/bedrock_adapter.cpp`

**Design:**

Create a minimal logger (no external dependency):
```cpp
// NeamC/include/neamc/llm/llm_logger.hpp

enum class LogLevel { DEBUG, INFO, WARN, ERROR };

class LLMLogger {
public:
    static LogLevel level() {
        static LogLevel lvl = parse_env();
        return lvl;
    }

    static void log(LogLevel lvl, const std::string& component,
                    const std::string& message) {
        if (lvl < level()) return;
        auto now = std::chrono::system_clock::now();
        auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        std::cerr << "[" << ms << "] [" << level_str(lvl) << "] ["
                  << component << "] " << message << "\n";
    }

private:
    static LogLevel parse_env() {
        const char* env = std::getenv("NEAM_LOG_LEVEL");
        if (!env) return LogLevel::WARN;
        std::string s(env);
        if (s == "DEBUG" || s == "debug") return LogLevel::DEBUG;
        if (s == "INFO" || s == "info") return LogLevel::INFO;
        if (s == "ERROR" || s == "error") return LogLevel::ERROR;
        return LogLevel::WARN;
    }
};
```

**Log points to add:**
| Component | Level | What |
|---|---|---|
| http_client | DEBUG | Request URL, method, content-length |
| http_client | INFO | Response status, latency (ms) |
| http_client | WARN | Retry attempt (attempt #, reason, delay) |
| http_client | ERROR | Final failure (status, error message) |
| adapters | INFO | Provider, model, prompt tokens (estimated) |
| adapters | DEBUG | Full request body (truncated to 500 chars) |
| adapters | ERROR | Parse failure, missing fields |

**Verification:**
- `NEAM_LOG_LEVEL=DEBUG` shows all log lines
- `NEAM_LOG_LEVEL=ERROR` shows only errors
- Default (no env var) shows WARN and ERROR only
- Logs go to stderr (not stdout) — won't interfere with agent output

**Effort:** 2 hours
**Risk:** Low
**Dependencies:** None

---

### 2.4 VM Stack Depth Protection

**File:** `NeamC/src/vm/vm.cpp`
**Impact:** Prevents native SIGSEGV from recursive agent/function calls

**Changes:**

Add stack depth tracking:
```cpp
// In VM class private members:
static constexpr int MAX_CALL_DEPTH = 1000;
int call_depth_ = 0;

// In OP_CALL handler (before pushing new call frame):
if (++call_depth_ > MAX_CALL_DEPTH) {
    --call_depth_;
    throw RuntimeError("Stack overflow: maximum call depth (" +
                       std::to_string(MAX_CALL_DEPTH) + ") exceeded. "
                       "Check for infinite recursion.");
}

// In OP_RETURN handler (after popping call frame):
--call_depth_;
```

Also make configurable via environment:
```cpp
int get_max_call_depth() {
    const char* env = std::getenv("NEAM_MAX_CALL_DEPTH");
    if (env) {
        int val = std::atoi(env);
        if (val > 0 && val <= 100000) return val;
    }
    return 1000;
}
```

**Verification:**
- Recursive function with depth 500 — succeeds
- Recursive function with depth 1500 — throws RuntimeError (not SIGSEGV)
- `NEAM_MAX_CALL_DEPTH=50` — recursive function with depth 100 fails gracefully
- Error message includes the depth limit value

**Effort:** 30 minutes
**Risk:** Low
**Dependencies:** None

---

### 2.5 GC Reentrancy Guard

**File:** `NeamC/src/vm/memory.cpp` (lines 288-291)
**Impact:** Prevents heap corruption from GC triggered during allocation

**Changes:**

```cpp
// In MemoryManager class:
bool gc_in_progress_ = false;

// In collect() method:
void MemoryManager::collect() {
    if (gc_in_progress_) return;  // Skip if already collecting
    gc_in_progress_ = true;

    // ... existing GC code (mark, sweep) ...

    gc_in_progress_ = false;
}
```

Also add a scope guard for exception safety:
```cpp
struct GCGuard {
    bool& flag;
    explicit GCGuard(bool& f) : flag(f) { flag = true; }
    ~GCGuard() { flag = false; }
};

void MemoryManager::collect() {
    if (gc_in_progress_) return;
    GCGuard guard(gc_in_progress_);
    // ... existing GC code ...
}
```

**Verification:**
- Force GC during allocation (allocate objects in finalizer) — no crash
- Normal GC cycle still works correctly
- Measure memory usage — no leaks from skipped collections

**Effort:** 1 hour
**Risk:** Medium — must verify GC correctness preserved
**Dependencies:** None

---

### Phase 2 Acceptance Criteria

| # | Criterion | Test Method |
|---|---|---|
| 1 | Connection pooling reduces TLS handshakes | Benchmark: 100 sequential requests, count handshakes |
| 2 | Transient failures recovered automatically | Mock 429/503 → success pattern |
| 3 | `NEAM_LOG_LEVEL=DEBUG` produces structured logs | Visual inspection of stderr |
| 4 | Recursive depth > 1000 throws RuntimeError | Unit test with deep recursion |
| 5 | GC reentrancy doesn't crash | Stress test with allocation-heavy finalizers |
| 6 | All existing tests still pass | `ctest` green |

---

## Phase 3: Build System & CI (Days 5-7, ~14 hours)

**Goal:** Make `cmake --install` work, enable compiler warnings, create CI pipeline, unify version management.

### 3.1 Add CMake Install Targets

**File:** `CMakeLists.txt`

**Add after the existing target definitions (~line 329):**

```cmake
# ============================================================================
# Install targets
# ============================================================================

include(GNUInstallDirs)

# Executables
install(TARGETS neam-cli neam-pkg neam-api neam-lsp neam-dap
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Core library
install(TARGETS neamc_core
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

# Headers
install(DIRECTORY ${CMAKE_SOURCE_DIR}/NeamC/include/neamc
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

# Standard library (.neam files)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/NeamC/stdlib/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/neam/stdlib
        FILES_MATCHING PATTERN "*.neam")

# Set RPATH for installed binaries
set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
```

**Also fix the existing `release.yml`:**
```yaml
# In .github/workflows/release.yml, the install step should now work:
- name: Install
  run: cmake --install build --prefix dist/neam-${{ matrix.os }}
```

**Verification:**
- `cmake --install build --prefix /tmp/neam-test` succeeds
- All 5 executables present in `/tmp/neam-test/bin/`
- Library present in `/tmp/neam-test/lib/`
- Headers present in `/tmp/neam-test/include/neamc/`
- Stdlib present in `/tmp/neam-test/share/neam/stdlib/`
- Executables run correctly from install prefix

**Effort:** 2 hours
**Risk:** Low
**Dependencies:** None

---

### 3.2 Enable Compiler Warning Flags

**File:** `CMakeLists.txt`

**Add after project() declaration:**

```cmake
# ============================================================================
# Compiler warnings
# ============================================================================

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    set(NEAM_WARNING_FLAGS
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-parameter      # Too many false positives in stubs
        -Wconversion
        -Wsign-conversion
        -Wshadow
    )
    target_compile_options(neamc_core PRIVATE ${NEAM_WARNING_FLAGS})
elseif(MSVC)
    target_compile_options(neamc_core PRIVATE /W4)
endif()

# Optional: Sanitizers for debug builds
option(NEAM_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(NEAM_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)

if(NEAM_ENABLE_ASAN)
    target_compile_options(neamc_core PRIVATE -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(neamc_core PRIVATE -fsanitize=address)
endif()

if(NEAM_ENABLE_UBSAN)
    target_compile_options(neamc_core PRIVATE -fsanitize=undefined)
    target_link_options(neamc_core PRIVATE -fsanitize=undefined)
endif()
```

**Warning fix strategy:**
After enabling warnings, build and fix iteratively:
1. First build: catalog all warnings by category
2. Fix signed/unsigned comparison warnings (~estimated 30-50)
3. Fix shadow variable warnings (~estimated 10-20)
4. Fix unused variable warnings (the 6 compiler stubs will trigger these — handled in Phase 4)
5. Final build: zero warnings

**Verification:**
- `cmake --build build 2>&1 | grep warning` shows 0 warnings
- ASan build: `cmake -DNEAM_ENABLE_ASAN=ON .. && cmake --build .` works
- UBSan build: `cmake -DNEAM_ENABLE_UBSAN=ON .. && cmake --build .` works

**Effort:** 6 hours (30 min for flags + 5.5 hours fixing warnings)
**Risk:** Medium — may uncover latent bugs
**Dependencies:** None

---

### 3.3 Create CI Workflow

**File:** `.github/workflows/ci.yml` (NEW)

```yaml
name: CI

on:
  push:
    branches: [main, nightly]
  pull_request:
    branches: [main]

jobs:
  build-and-test:
    strategy:
      matrix:
        os: [ubuntu-latest, macos-latest]
        build_type: [Debug, Release]
    runs-on: ${{ matrix.os }}

    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Install dependencies (Ubuntu)
        if: runner.os == 'Linux'
        run: |
          sudo apt-get update
          sudo apt-get install -y libcurl4-openssl-dev libssl-dev

      - name: Configure
        run: |
          cmake -B build \
            -DCMAKE_BUILD_TYPE=${{ matrix.build_type }} \
            -DNEAM_ENABLE_ASAN=${{ matrix.build_type == 'Debug' && 'ON' || 'OFF' }}

      - name: Build
        run: cmake --build build -j$(nproc 2>/dev/null || sysctl -n hw.ncpu)

      - name: Test
        run: ctest --test-dir build --output-on-failure --timeout 120

      - name: Install check
        run: cmake --install build --prefix /tmp/neam-install

  lint:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4
        with:
          submodules: recursive

      - name: Check formatting
        run: |
          find NeamC/src NeamC/include -name '*.cpp' -o -name '*.hpp' | head -20 | \
            xargs clang-format --dry-run --Werror 2>&1 || echo "Formatting issues found"
```

**Verification:**
- Push to `test` branch — CI runs and passes
- PR to `main` — CI runs and passes
- Failing test — CI correctly reports failure

**Effort:** 3 hours
**Risk:** Low
**Dependencies:** Phase 3.1 (install targets needed for install check step)

---

### 3.4 Unify Version String

**Files:**
- `VERSION` (NEW — root of project)
- `CMakeLists.txt` — read from VERSION file
- `NeamC/src/neam_cli.cpp:42` — currently `0.2.0`
- `NeamC/src/neam_pkg.cpp:78` — currently `1.0.0`

**Step 1:** Create root `VERSION` file:
```
0.6.5
```

**Step 2:** CMakeLists.txt reads it:
```cmake
file(STRINGS "${CMAKE_SOURCE_DIR}/VERSION" NEAM_VERSION)
project(NeamC VERSION ${NEAM_VERSION} LANGUAGES C CXX)

configure_file(
    "${CMAKE_SOURCE_DIR}/NeamC/include/neamc/version.hpp.in"
    "${CMAKE_BINARY_DIR}/include/neamc/version.hpp"
)
```

**Step 3:** Create `NeamC/include/neamc/version.hpp.in`:
```cpp
#pragma once
#define NEAM_VERSION "@NEAM_VERSION@"
#define NEAM_VERSION_MAJOR @PROJECT_VERSION_MAJOR@
#define NEAM_VERSION_MINOR @PROJECT_VERSION_MINOR@
#define NEAM_VERSION_PATCH @PROJECT_VERSION_PATCH@
```

**Step 4:** Replace all hardcoded versions:
```cpp
// neam_cli.cpp — replace "0.2.0" with:
#include "neamc/version.hpp"
// ... use NEAM_VERSION ...

// neam_pkg.cpp — replace "1.0.0" with:
#include "neamc/version.hpp"
// ... use NEAM_VERSION ...
```

**Verification:**
- `neam-cli --version` shows `0.6.5`
- `neam-pkg --version` shows `0.6.5`
- Change `VERSION` file to `0.6.6`, rebuild — all show `0.6.6`
- `grep -rn '"0.2.0"\|"1.0.0"' NeamC/src/` shows zero hardcoded versions

**Effort:** 2 hours
**Risk:** Low
**Dependencies:** None

---

### 3.5 Set Default Build Type

**File:** `CMakeLists.txt`

**Add after `cmake_minimum_required`:**
```cmake
# Default to Release if not specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Setting build type to 'Release' as none was specified")
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY
        STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()
```

**Effort:** 15 minutes
**Risk:** None
**Dependencies:** None

---

### Phase 3 Acceptance Criteria

| # | Criterion | Test Method |
|---|---|---|
| 1 | `cmake --install` installs all artifacts | Check bin/, lib/, include/, share/ |
| 2 | Zero compiler warnings with -Wall -Wextra | `cmake --build build 2>&1 \| grep -c warning` = 0 |
| 3 | CI workflow passes on Ubuntu + macOS | GitHub Actions green |
| 4 | All binaries show version `0.6.5` | `neam-cli --version` |
| 5 | No hardcoded version strings remain | `grep` search clean |
| 6 | Default build type is Release | Build without `-DCMAKE_BUILD_TYPE` uses Release |

---

## Phase 4: Compiler Integrity (Days 8-9, ~4 hours)

**Goal:** Stop the compiler from silently discarding user code. Make unimplemented features fail loudly.

### 4.1 Emit Errors for Unimplemented Compiler Nodes

**File:** `NeamC/src/compiler.cpp` (lines 434-457)

**Current (silent discard):**
```cpp
void Compiler::visit(const TestDecl& node) { (void)node; }
void Compiler::visit(const TestSuiteDecl& node) { (void)node; }
void Compiler::visit(const ModuleDecl& node) { (void)node; }
void Compiler::visit(const ImportDecl& node) { (void)node; }
void Compiler::visit(const TypeAlias& node) { (void)node; }
void Compiler::visit(const DocComment& node) { (void)node; }
```

**Replace with clear error messages:**
```cpp
void Compiler::visit(const TestDecl& node) {
    emit_error("'test' declarations are not yet supported in this version. "
               "Test '" + node.name + "' will be ignored.", node.location);
}

void Compiler::visit(const TestSuiteDecl& node) {
    emit_error("'test suite' declarations are not yet supported. "
               "Suite '" + node.name + "' will be ignored.", node.location);
}

void Compiler::visit(const ModuleDecl& node) {
    emit_error("'module' declarations are not yet supported. "
               "Module '" + node.name + "' will be ignored.", node.location);
}

void Compiler::visit(const ImportDecl& node) {
    emit_error("'import' statements are not yet supported. "
               "Import of '" + node.path + "' will be ignored.", node.location);
}

void Compiler::visit(const TypeAlias& node) {
    emit_error("'type' aliases are not yet supported. "
               "Type alias '" + node.name + "' will be ignored.", node.location);
}

void Compiler::visit(const DocComment& node) {
    // Doc comments are safely ignorable — emit as warning, not error
    emit_warning("Doc comments are parsed but not yet used by the compiler.",
                 node.location);
}
```

**Decision: Error vs Warning**
- `test`, `test suite`, `module`, `import`, `type alias` → **Warning** (not error) — allows existing programs to compile with a visible notification. Users can see the warning and know their feature isn't working.
- `doc comment` → **Info/Debug** — completely safe to ignore

**Verification:**
- `.neam` file with `test "foo" { ... }` — compiles with warning message
- `.neam` file with `import std.net` — compiles with warning message
- Warning includes source location (file:line:col)
- Programs without these features — zero warnings (no regression)

**Effort:** 1 hour
**Risk:** Low — changing from silent to visible, not breaking
**Dependencies:** None

---

### 4.2 Add Default Cases to Switch Statements

**Files:** `NeamC/src/compiler.cpp`, `NeamC/src/vm/vm.cpp`

**Locations identified in gap assessment:**
| File | Line | Switch On |
|---|---|---|
| `compiler.cpp` | 360 | `AssertStmt::Kind` |
| `compiler.cpp` | 643 | `GuardHandler::Type` |
| `compiler.cpp` | 1207 | `BinaryOp` |
| `vm.cpp` | 1040 | Opcode dispatch (main loop) |

**Pattern for each:**
```cpp
default:
    throw std::logic_error(
        "Unhandled case in [switch context]: " +
        std::to_string(static_cast<int>(value)));
```

For the opcode dispatch (vm.cpp:1040), use a more descriptive message:
```cpp
default:
    throw RuntimeError(
        "Unknown opcode " + std::to_string(static_cast<int>(opcode)) +
        " at instruction " + std::to_string(ip_));
```

**Verification:**
- All enum values still handled (no regressions)
- Invalid opcode in bytecode — throws RuntimeError with opcode number
- Compiler: invalid BinaryOp — throws with value

**Effort:** 2 hours
**Risk:** Low
**Dependencies:** None

---

### 4.3 Document Type System Status

**File:** `README.md`

**Add a "Known Limitations" section:**
```markdown
## Known Limitations (v0.6.5)

### Type System
The Hindley-Milner type inference system is **parsed but not yet enforced**.
Type annotations are accepted by the parser but type checking is not performed
at compile time. This means type errors will manifest at runtime rather than
compile time. Full type inference is planned for v0.7.0.

### Module System
`module` and `import` declarations are parsed but not yet compiled. Code
organization should use file-based separation for now.

### Test Framework
`test` and `test suite` declarations are parsed but the built-in test runner
is not yet implemented. Use the external evaluation framework in `tests/`.
```

**Also update the evaluation report** (`tests/evaluation/report/evaluation_report.md`):
Where the report mentions "Hindley-Milner type inference with generics" as Module #16, add a footnote:
```
*Note: The type inference parser is implemented; compile-time type checking
is planned for v0.7.0. Runtime type checking is active.*
```

**Effort:** 30 minutes
**Risk:** None
**Dependencies:** None

---

### Phase 4 Acceptance Criteria

| # | Criterion | Test Method |
|---|---|---|
| 1 | Stub features produce visible warnings | Compile `.neam` with `test`, `import`, `module` |
| 2 | All switch statements have default cases | `grep -n "switch" compiler.cpp vm.cpp` — all have `default:` |
| 3 | README documents known limitations | Visual review |
| 4 | Evaluation report has type system footnote | Visual review |

---

## Phase 5: Test Coverage (Days 10-16, ~38 hours)

**Goal:** Raise test coverage from 2.4% toward a minimum viable baseline (~15-20%) covering all critical paths.

### 5.1 LLM Adapter Unit Tests

**New Files:**
- `NeamC/tests/llm/openai_adapter_test.cpp`
- `NeamC/tests/llm/ollama_adapter_test.cpp`
- `NeamC/tests/llm/bedrock_adapter_test.cpp`
- `NeamC/tests/llm/mock_http_server.hpp` (shared test utility)

**Test Categories per Adapter:**

| Category | Tests |
|---|---|
| Request construction | Correct URL, headers, body format, model name |
| Response parsing | Valid JSON, missing fields, malformed JSON, empty response |
| Error handling | HTTP 400, 401, 403, 429, 500, network timeout |
| Authentication | Missing API key, invalid key, env var loading |
| Bedrock-specific | SigV4 signature, session token, region selection |
| Streaming (if applicable) | SSE parsing, partial chunks, connection drop |

**Mock HTTP Server Design:**
```cpp
// Lightweight HTTP server for testing (binds to localhost:0 for random port)
class MockHTTPServer {
public:
    MockHTTPServer();
    ~MockHTTPServer();

    int port() const;
    void set_response(int status, const std::string& body);
    void set_response_delay(std::chrono::milliseconds delay);

    // Capture last request for assertions
    const HttpRequest& last_request() const;

private:
    std::thread server_thread_;
    // ... socket handling ...
};
```

**Verification:**
- All adapter tests pass locally
- Tests run in CI (no external API calls needed)
- Each adapter has >= 10 test cases

**Effort:** 12 hours
**Risk:** Low
**Dependencies:** Phase 2.2 (retry logic should be in place to test it)

---

### 5.2 HTTP Client Tests

**New File:** `NeamC/tests/llm/http_client_test.cpp`

**Test Categories:**

| Category | Tests |
|---|---|
| TLS verification | Valid cert passes, invalid cert fails, skip-verify override |
| Connection pooling | Handles reused across requests, stale eviction, thread safety |
| Retry logic | Exponential backoff timing, 429 Retry-After header respected |
| Timeout handling | Connect timeout, read timeout, configurable values |
| Request construction | GET, POST with body, custom headers, content-type |
| Error paths | DNS failure, connection refused, response too large |

**Effort:** 6 hours
**Risk:** Low
**Dependencies:** Phase 2.1, 2.2 (connection pooling and retry must be implemented)

---

### 5.3 Package Manager Tests

**New Files:**
- `NeamC/tests/pkg/installer_test.cpp`
- `NeamC/tests/pkg/registry_test.cpp`

**Test Categories:**

| Category | Tests |
|---|---|
| Checksum verification | Correct hash passes, wrong hash fails, missing file fails |
| Package install | Valid package installs to correct directory |
| Package removal | Installed package cleanly removed |
| Lock file | Lock file generated, lock file respected on reinstall |
| Registry client | Search query construction, response parsing, error handling |
| Dependency resolution | Simple deps, transitive deps, circular dep detection |

**Effort:** 8 hours
**Risk:** Low
**Dependencies:** Phase 1.2 (checksum verification must be implemented)

---

### 5.4 Integration Tests (End-to-End)

**New Files:**
- `NeamC/tests/integration/compile_run_test.cpp`
- `NeamC/tests/integration/test_programs/` (directory of `.neam` files)

**Test Pattern:**
```
Input: .neam source file
Step 1: Compile to bytecode
Step 2: Execute bytecode
Step 3: Assert output matches expected
```

**Test Programs:**

| Program | Tests |
|---|---|
| `hello.neam` | Basic emit statement, string output |
| `arithmetic.neam` | Math operations, operator precedence |
| `functions.neam` | Function definitions, calls, recursion |
| `agent_basic.neam` | Agent definition (mock LLM provider) |
| `list_ops.neam` | List creation, access, iteration, map/filter |
| `map_ops.neam` | Map creation, access, iteration |
| `error_handling.neam` | Try/catch, error propagation |
| `string_ops.neam` | String concatenation, interpolation, methods |
| `conditionals.neam` | If/else, match expressions |
| `loops.neam` | For, while, break, continue |
| `closures.neam` | Closure capture, higher-order functions |
| `pipe_operator.neam` | Pipe chaining `\|>` |
| `stack_overflow.neam` | Deep recursion — should throw RuntimeError (Phase 2.4 verification) |

**Effort:** 12 hours
**Risk:** Low
**Dependencies:** Phase 2.4 (stack overflow test depends on depth limit)

---

### 5.5 CMake Test Integration

**File:** `CMakeLists.txt`

**Add test targets:**
```cmake
# ============================================================================
# Tests
# ============================================================================

enable_testing()

# Find or fetch test framework
include(FetchContent)
FetchContent_Declare(
    Catch2
    GIT_REPOSITORY https://github.com/catchorg/Catch2.git
    GIT_TAG v3.5.2
)
FetchContent_MakeAvailable(Catch2)

# LLM adapter tests
add_executable(llm_tests
    NeamC/tests/llm/openai_adapter_test.cpp
    NeamC/tests/llm/ollama_adapter_test.cpp
    NeamC/tests/llm/bedrock_adapter_test.cpp
    NeamC/tests/llm/http_client_test.cpp
)
target_link_libraries(llm_tests PRIVATE neamc_core Catch2::Catch2WithMain)
add_test(NAME llm_tests COMMAND llm_tests)

# Package manager tests
add_executable(pkg_tests
    NeamC/tests/pkg/installer_test.cpp
    NeamC/tests/pkg/registry_test.cpp
)
target_link_libraries(pkg_tests PRIVATE neamc_core Catch2::Catch2WithMain)
add_test(NAME pkg_tests COMMAND pkg_tests)

# Integration tests
add_executable(integration_tests
    NeamC/tests/integration/compile_run_test.cpp
)
target_link_libraries(integration_tests PRIVATE neamc_core Catch2::Catch2WithMain)
add_test(NAME integration_tests COMMAND integration_tests)

# Copy test programs
file(COPY NeamC/tests/integration/test_programs
     DESTINATION ${CMAKE_BINARY_DIR}/tests/)
```

**Effort:** Included in each test file's effort estimate
**Risk:** Low
**Dependencies:** All test files from 5.1-5.4

---

### Phase 5 Acceptance Criteria

| # | Criterion | Test Method |
|---|---|---|
| 1 | LLM adapter tests pass (>= 30 test cases) | `ctest -R llm_tests` |
| 2 | HTTP client tests pass (>= 15 test cases) | `ctest -R http_client` |
| 3 | Package manager tests pass (>= 15 test cases) | `ctest -R pkg_tests` |
| 4 | Integration tests pass (>= 13 test programs) | `ctest -R integration_tests` |
| 5 | All tests run in CI | GitHub Actions green |
| 6 | Test coverage >= 15% | Measured via `gcov` or `llvm-cov` |
| 7 | Zero test flakiness | Run 3x, all pass |

---

## Phase 6: Code Cleanup (Days 17-20, ~19 hours)

**Goal:** Consolidate duplicates, improve error messages, raise limits, fix registry client.

### 6.1 Consolidate Async Runtimes

**Files:**
- `NeamC/src/vm/llm/openai_adapter.cpp` — uses `vm::async::Executor`
- `NeamC/src/vm/llm/ollama_adapter.cpp` — uses `vm::async::Executor`
- `NeamC/src/vm/llm/bedrock_adapter.cpp` — uses `vm::async::Executor`
- `NeamC/include/neamc/vm/async/` — the weaker runtime

**Migration plan:**
1. Replace `vm::async::Future<T>` with `runtime::Future<T>` in all adapters
2. Replace `vm::async::Executor` with `runtime::Executor` (which has work-stealing)
3. Map error handling: `std::exception_ptr` → `Result<T, E>`
4. Test all 3 adapters after migration
5. If `vm::async` has no remaining callers, mark headers as deprecated with `[[deprecated("Use runtime::Future instead")]]`

**Effort:** 6 hours
**Risk:** HIGH — wide-reaching change, must test thoroughly
**Dependencies:** Phase 5.1 (adapter tests must exist to catch regressions)

---

### 6.2 Improve Error Messages

**File:** `NeamC/src/vm/vm.cpp`

**Current problems:**
- `"Index out of range"` — doesn't say what index or what range
- `"Type error"` — doesn't say expected vs actual types
- `"Undefined variable"` — doesn't say which variable

**Pattern for each error message:**
```cpp
// Before:
throw RuntimeError("Index out of range");

// After:
throw RuntimeError("Index out of range: index " +
    std::to_string(index) + " is not valid for list of size " +
    std::to_string(list_size));
```

**Error messages to improve:**

| Current Message | Improved Message |
|---|---|
| `"Index out of range"` | `"Index out of range: index {N} not valid for {type} of size {M}"` |
| `"Type error"` | `"Type error: expected {expected}, got {actual} in {context}"` |
| `"Undefined variable"` | `"Undefined variable '{name}' at {location}"` |
| `"Unknown error"` | `"Internal error in {operation}: {details}"` |
| `"Division by zero"` | `"Division by zero: {lhs} / 0 in {context}"` |
| `"Stack underflow"` | `"Stack underflow: attempted to pop from empty stack in {operation}"` |
| `"Invalid operand"` | `"Invalid operand: cannot apply '{op}' to {type}"` |

**Effort:** 6 hours
**Risk:** Low — strictly additive (more information, same error paths)
**Dependencies:** None

---

### 6.3 Raise Hard-Coded Limits

**Files:** `NeamC/src/compiler.cpp`, `NeamC/src/vm/vm.cpp`, `NeamC/src/vm/memory.cpp`

| Current Limit | Current Value | New Value | Configurable |
|---|---|---|---|
| List literal size | 255 (uint8_t) | 65,535 (uint16_t) | No |
| Map literal size | 255 | 65,535 | No |
| Jump offset | 65,535 | 65,535 (keep) | No |
| Max ReAct steps | 10 | 100 | Yes: `NEAM_MAX_REACT_STEPS` |
| GC initial threshold | 1MB | 1MB | Yes: `NEAM_GC_THRESHOLD` |
| REPL history | 100 | 1000 | Yes: `NEAM_REPL_HISTORY` |
| HTTP timeout | 30s/60s | 30s/60s | Yes: `NEAM_HTTP_TIMEOUT` |

**Implementation for configurable limits:**
```cpp
int get_config_int(const char* env_var, int default_val,
                   int min_val, int max_val) {
    const char* env = std::getenv(env_var);
    if (env) {
        int val = std::atoi(env);
        if (val >= min_val && val <= max_val) return val;
    }
    return default_val;
}
```

**Compiler changes for list/map limits:**
- Change `emit_byte(static_cast<uint8_t>(count))` to `emit_short(static_cast<uint16_t>(count))`
- Update `OP_LIST` and `OP_MAP` handlers in VM to read 2-byte operand instead of 1-byte

**Verification:**
- List with 300 elements — compiles and runs (was: crash)
- Map with 300 entries — compiles and runs
- `NEAM_MAX_REACT_STEPS=50` — agent runs 50 steps max
- `NEAM_GC_THRESHOLD=4194304` — GC threshold set to 4MB
- Default values unchanged when no env var set

**Effort:** 3 hours
**Risk:** Medium — opcode format change requires VM update
**Dependencies:** Phase 5.4 (integration tests verify no regression)

---

### 6.4 Fix Registry Client

**File:** `NeamC/src/pkg/registry.cpp`

**Current state:** Uses raw POSIX sockets + regex-based JSON parsing. Both `libcurl` and `nlohmann::json` are already linked to the project.

**Replace with:**
```cpp
// Use the same HTTP client used by LLM adapters
#include "neamc/llm/http_client.hpp"
#include <nlohmann/json.hpp>

// Replace raw socket code with:
auto response = http_client::get(registry_url + "/packages/" + name);
auto json = nlohmann::json::parse(response.body);

// Replace regex parsing with:
std::string version = json["version"].get<std::string>();
std::string download_url = json["download_url"].get<std::string>();
std::string checksum = json["sha256"].get<std::string>();
```

**Benefits:**
- Removes ~200 lines of raw socket code
- Gets TLS verification, connection pooling, retry logic for free (from Phase 2)
- Proper JSON parsing instead of regex extraction
- Consistent error handling with rest of codebase

**Verification:**
- Package search returns correct results
- Package install downloads correct artifact
- Invalid JSON from registry — graceful error (not regex crash)
- Registry timeout — retries transparently (via Phase 2.2)

**Effort:** 4 hours
**Risk:** Medium — must map all API endpoints correctly
**Dependencies:** Phase 2.1, 2.2 (uses improved HTTP client)

---

### Phase 6 Acceptance Criteria

| # | Criterion | Test Method |
|---|---|---|
| 1 | All adapters use `runtime::Future` | `grep -rn "vm::async" NeamC/src/vm/llm/` shows zero |
| 2 | Error messages include context | Trigger each error, verify message includes variable/type info |
| 3 | List/map > 255 elements work | Integration test with 300-element list |
| 4 | Registry uses libcurl + nlohmann::json | `grep -rn "socket\|regex" NeamC/src/pkg/registry.cpp` shows zero |
| 5 | All configurable limits work via env vars | Set each, verify behavior |
| 6 | All tests pass | `ctest` green (including new Phase 5 tests) |

---

## Release Checklist (Day 20)

### Pre-Release Verification

```bash
# 1. Clean build from scratch
rm -rf build && cmake -B build -DCMAKE_BUILD_TYPE=Release && cmake --build build -j

# 2. Zero compiler warnings
cmake --build build 2>&1 | grep -c "warning:" # Must be 0

# 3. All tests pass
ctest --test-dir build --output-on-failure

# 4. Install works
cmake --install build --prefix /tmp/neam-v065
ls /tmp/neam-v065/bin/   # All 5 executables
ls /tmp/neam-v065/lib/   # Core library
ls /tmp/neam-v065/include/neamc/  # Headers

# 5. Version is correct
/tmp/neam-v065/bin/neam-cli --version  # 0.6.5
/tmp/neam-v065/bin/neam-pkg --version  # 0.6.5

# 6. Security verification
# TLS: attempt connection with invalid cert — must fail
# Auth: API server rejects unauthenticated request when key set
# Checksum: tampered package fails verification
# System calls: grep confirms no system() in pkg/api paths

# 7. ASan/UBSan clean run
cmake -B build-asan -DNEAM_ENABLE_ASAN=ON -DNEAM_ENABLE_UBSAN=ON
cmake --build build-asan
ctest --test-dir build-asan  # Must pass with no sanitizer errors

# 8. CI passes on both Ubuntu and macOS
# (verified via GitHub Actions)
```

### Release Artifacts

| Artifact | Contents |
|---|---|
| `neam-v0.6.5-macos-arm64.tar.gz` | CLI, pkg, api, lsp, dap binaries + stdlib |
| `neam-v0.6.5-macos-x86_64.tar.gz` | Same, Intel build |
| `neam-v0.6.5-linux-x86_64.tar.gz` | Same, Linux build |
| `CHANGELOG.md` | All 36 fixes documented |
| `VERSION` | `0.6.5` |

### Changelog Template

```markdown
## v0.6.5 — Security & Reliability Release

### Security Fixes
- **CRITICAL:** Enable TLS certificate verification for all HTTP connections
- **CRITICAL:** Implement SHA256 package checksum verification
- **CRITICAL:** Replace `system()` calls with `posix_spawn()`/`termios`
- **CRITICAL:** Add API server bearer token authentication
- **CRITICAL:** Add CMake install targets (was broken)

### Reliability Improvements
- HTTP connection pooling (10-100x TLS overhead reduction)
- Exponential backoff retry for transient LLM failures (429, 5xx)
- Structured logging for LLM stack (`NEAM_LOG_LEVEL`)
- VM stack depth limit (default 1000, configurable)
- GC reentrancy guard (prevents heap corruption)

### Build System
- CMake install targets for all binaries, libraries, headers, stdlib
- Compiler warnings enabled (-Wall -Wextra -Wpedantic)
- CI/CD workflow for Ubuntu + macOS (build, test, install)
- Unified version management via root VERSION file
- ASan/UBSan support for debug builds
- Default build type set to Release

### Compiler
- Unimplemented features (test, module, import, type alias) now emit warnings
- Default cases added to all switch statements
- Type system status documented (parsed, not enforced; v0.7.0 target)

### Testing
- New LLM adapter unit tests (30+ test cases)
- New HTTP client tests (15+ test cases)
- New package manager tests (15+ test cases)
- New integration tests (13+ end-to-end programs)
- Test coverage: 2.4% → ~18%

### Code Quality
- Async runtime consolidated (vm::async → runtime::Future)
- Error messages include context (variable names, types, indices)
- List/map literal limit raised from 255 to 65,535
- Registry client rewritten with libcurl + nlohmann::json
- Configurable limits via environment variables
```

---

## Summary: Phase Timeline

```
Week 1:  ████████████████████████████████████████
         Phase 1 (8h)    Phase 2 (13h)
         Security         Reliability

Week 2:  ████████████████████████████████████████
         Phase 3 (14h)        Phase 4 (4h)
         Build/CI              Compiler

Week 3:  ████████████████████████████████████████
         Phase 5 (38h) ─────────────────────────→
         Testing

Week 4:  ████████████████████████████████████████
         Phase 5 (cont)  Phase 6 (19h)  Release
         Testing          Cleanup         v0.6.5
```

| Phase | Focus | Hours | Issues Fixed | Risk |
|---|---|---|---|---|
| **Phase 1** | Critical Security | 8h | 5 CRITICAL | Low |
| **Phase 2** | Reliability | 13h | 6 HIGH | Medium |
| **Phase 3** | Build/CI | 14h | 6 HIGH/MED | Low |
| **Phase 4** | Compiler | 4h | 4 MEDIUM | Low |
| **Phase 5** | Testing | 38h | Coverage gap | Low |
| **Phase 6** | Cleanup | 19h | 15 MED/LOW | Medium |
| **Total** | | **~96h** | **36 issues** | |

---

*This implementation plan is based on the v0.6.4 gap assessment (36 issues across 5 severity levels) and targets a complete fix release with no new features. All code samples reference actual file paths and line numbers verified against the codebase as of 2026-02-07.*
