# Neam Release Notes

> Latest releases first. v1.5.0 NeamEvolve (April 2026) ships on top of the
> v1.4.5 NeamHarness substrate; both releases are documented in detail below.
> Earlier (v0.6.x) entries follow.

---

## v1.5.0 — NeamEvolve: Self-Evolving Agents (April 2026)

**Codename:** NeamEvolve — *The Shapeshifter*
**Tagline:** *v1.0 secured the agent. v1.1 gave it knowledge. v1.2 made it production-ready. v1.3 lets it research. v1.4 lets it remember. v1.4.5 lets it orchestrate. v1.5 lets it evolve — on top of the orchestration substrate, not from scratch.*

### What ships

| Surface | Count |
|---|---|
| New keywords | 4 (`evolve agent`, `belief`, `skill_library`, `curriculum`) |
| New bytecode sub-types | 4 (31, 32, 33, 34) — **zero new opcodes** |
| New native functions | **24** registered |
| New compile-time validation rules | 14 reachable (E-001..E-016) |
| New JSONL trace event kinds | 8 (with SHA-256 hash chain — NFR-SEC-6) |
| Net new C++ | ~1255 LOC implementation + ~470 LOC tests + ~120 LOC stdlib |
| Test coverage | **40/40 v1.5 + 56/56 v1.4.5** = 96/96 unit tests passing |

### The headline construct

```neam
// Alignment anchor — immutable purpose
program EvolveGoals {
    mission: "Help research effective ML architectures",
    constraints: ["Never circumvent governance"],
    autonomy: "human_in_loop"
}

// Hard-severity assertion kernel — the constraint layer that bounds belief
assertion_registry CoreConstraints {
    no_secret_leak: { kind: "regex",   pattern: "api_key", severity: "hard" },
    cost_cap:       { kind: "runtime", metric: "cost", op: "<=", value: 100, severity: "hard" }
}

// Mutable strategy — the only genuinely new mutable cell in the language
belief CoreStrategy {
    initial: "Approach systematically. Verify each step.",
    constraints: CoreConstraints,
    revision_trigger: "every_N_runs",
    trigger_n: 5,
    rollback: true,
    max_revisions_per_session: 10
}

// Runtime-acquired skills — sandboxed, signed, capability-monotonic
skill_library Skills {
    verify: { method: "self_test", sandbox: true },
    deprecate: { after_failures: 5 },
    allow_runtime_acquisition: true
}

// Versioned cross-session state — git-backed for belief rollback
handoff Experiences {
    path: "./runs/darwin/experiences.md",
    schema: "markdown",
    required_sections: ["reflections", "rules"],
    versioning: "git",
    schema_version: "1.0.0"
}

// At least one role:"evaluator" forge agent required (E-013)
forge agent Critic {
    provider: "openai", model: "gpt-5", role: "evaluator",
    loop: { max_iterations: 1 max_cost: 1.0 max_tokens: 10000 },
    verify: my_check, budget: B
}

// The evolve agent — a specialised harness with mutable belief
evolve agent Darwin {
    provider: "openai", model: "gpt-5", budget: B,
    sub_agents: { critic: Critic },
    handoff: Experiences,
    assertions: CoreConstraints,
    belief: CoreStrategy,
    skills: Skills,
    safety: { program: EvolveGoals }
}

// Lifecycle (delegates to v1.4.5 harness_runtime under the hood)
evolve_agent_start("Darwin");
let answer = evolve_agent_run("Darwin", "AIME 2025 problem I-7");
let r = belief_revise("CoreStrategy", "Approach systematically with verification.");
let s = skill_acquire("Skills", "extract_int", "fun extract_int(text) { return text; }");
evolve_agent_complete("Darwin");
```

### v1.5 native functions (24)

| Group | Natives |
|---|---|
| Lifecycle (6) | `evolve_agent_{start, run, complete, abort, status, trace_path}` |
| Belief (6) | `belief_{text, revise, rollback, history, diff, hash}` |
| Skill library (6) | `skill_{acquire, get, list, test, deprecate, invoke}` |
| Curriculum P1 (3) | `curriculum_{next, advance, difficulty}` |
| Design op P2 (4) | `design_{propose, compile_in_sandbox, score, promote}` — gated on `safety.human_gate` |
| Audit (1) | `evolution_audit_query` (read JSONL trace by date range) |

### Hash-chained audit (NFR-SEC-6)

Every v1.5 event carries `prev_hash` + `this_hash` SHA-256 fields, making the JSONL trace tamper-evident:

```jsonl
{"kind":"harness.start", ...}                                         ← v1.4.5 (no chain)
{"kind":"belief.revision","prev_hash":"0000...","this_hash":"25b8...",...}  ← chain seed
{"kind":"skill.acquired","prev_hash":"25b8...","this_hash":"8801...",...}    ← linked
{"kind":"skill.acquired","prev_hash":"8801...","this_hash":"1a7e...",...}
{"kind":"harness.complete", ...}                                      ← v1.4.5 (no chain)
```

A tampered line breaks the chain on the next event — verifiers reject the trace.

### Alignment anchor — purpose immutability + constraint enforcement

The cardinal safety pattern (FR-AAN-1..5):

- The `program` declaration is the **immutable purpose** (`safety.program: EvolveGoals` — required, E-016).
- The `assertion_registry` referenced by `belief.constraints` is the **enforced constraint layer** with at least one hard-severity rule (E-002, E-004).
- Every `belief_revise` call evaluates hard regex assertions BEFORE committing (refuses with `BL-CONSTRAINTS` on violation).
- Cumulative drift bounded at 0.7 (Levenshtein over initial vs current text); exceeding triggers auto-`harness_abort` with reason `BELIEF_DRIFT_EXCEEDED` (NFR-EVO-1).
- Skills acquired at runtime go through capability-monotonicity static analysis (FR-AAN-3, E-011) — they cannot exceed the parent agent's `tool_registry.scoping`.

### Backward compatibility (NFR-COMPAT-*)

- Every valid v1.4.5 program compiles unchanged with v1.5 `neamc` (NFR-COMPAT-1) — verified by `v145_harness_test` 56/56 still passing.
- Zero new opcodes; all v1.5 declarations reuse `OP_DEFINE_DIO_DECLARATION` with new sub-types 31–34 (NFR-COMPAT-2).
- `evolve_agent_runtime.cpp` is a **95-line shim** that delegates orchestration to `harness_runtime.cpp` (NFR-COMPAT-3). No duplicated control flow.
- The JSONL trace plane is forward-compatible by design — v1.4.5 consumers ignore unknown event kinds (FR-EVT-4).

### Implementation phase summary

| Phase | Scope | Files |
|---|---|---|
| A | AST + parser + lexer (sub-types 31–34) | `ast.hpp`, `parser.cpp`, `compiler.cpp` |
| B | 14 compile-time validators (E-001..E-016) | `compiler.cpp` |
| C | VM dispatch + `BeliefRecord` / `SkillLibraryRecord` / `CurriculumRecord` | `vm.cpp`, `harness_types.{hpp,cpp}` |
| D | Belief runtime (revise/rollback/history) | `belief_runtime.{hpp,cpp}` *(new)* |
| E | EvolveAgent runtime (thin shim) | `evolve_agent_runtime.{hpp,cpp}` *(new)* |
| F | Hash chain + 8 new event kinds | `harness_runtime.cpp` |
| G | Skill library + capability monotonicity analyser + sandbox | `skill_library_runtime.{hpp,cpp}` *(new)* |
| H | Curriculum runtime (P1) | `curriculum_runtime.{hpp,cpp}` *(new)* |
| I | Design operation (P2) — sandboxed `neamc` spawn | `design_runtime.{hpp,cpp}` *(new)* |
| J | Native registration (24) + stdlib (4) + tests (40) | `native.cpp`, `stdlib/evolve/*.neam`, `tests/unit/v15_evolve_test.cpp` |

### Pragmatic limitations (documented honest gaps)

1. **Sandbox process spawn for skills**: The static analysis + capability monotonicity (the load-bearing safety checks) are fully enforced. The actual subprocess spawn for skill self-test is dry-run-stubbed under `NEAM_HARNESS_DRY_RUN=1`. Production seccomp/sandbox-exec wiring lands in a follow-up.
2. **Belief drift uses Levenshtein fallback** instead of cosine similarity over `nomic-embed-text` embeddings (documented in spec §9.3). A one-line warning is emitted on first use.
3. **Curriculum auto mode** generates templated task descriptions; LLM-driven proposal lands when the proposer-sub-agent invocation surface ships.
4. **Design op P2** uses a stub for `design_propose` (deterministic candidate template); full LLM-driven design generation is a follow-up.

### Documentation

- [`docs/v1.5_Neam_Self_Evolving_Agent_Brainstorming_Research.md`](docs/v1.5_Neam_Self_Evolving_Agent_Brainstorming_Research.md) — research grounding (16 arXiv papers + 2 surveys, rebased on v1.4.5.1)
- [`docs/v1.5_Neam_Self_Evolving_Agent_Requirement_Specification.md`](docs/v1.5_Neam_Self_Evolving_Agent_Requirement_Specification.md) — formal RFC-2119 requirements (72 FR + 34 NFR + 18 E-* validators)
- [`docs/v1.5_Neam_Self_Evolving_Agent_Implementation_Specification.md`](docs/v1.5_Neam_Self_Evolving_Agent_Implementation_Specification.md) — C++/VM insertion-line plan with phase decomposition

---

## v1.4.5.1 — NeamHarness patch: Phase-3-full lifecycle + streaming LLM (April 2026)

### Highlights

- **Phase 3 *full* harness lifecycle**: `harness_start` / `harness_run` / `harness_complete` / `harness_abort` / `harness_trace_path` natives. A single `harness_run(name, goal)` call iterates the declared `sub_agents` map (in declaration order, preserved via `nlohmann::ordered_json`), threads each output as the next slot's prior context, runs hard-severity regex assertions, retries once on violation, and writes a JSONL trace.
- **`llm_ask_stream`** — SSE-streamed LLM bridge. Same calling convention as `llm_ask` but uses the provider's streaming chat under the hood.
- **HTTP recv-idle timeout 120 s → 300 s** in `http_client.hpp` — gpt-5-class reasoning models can silence-think for 2+ minutes between visible tokens; the recv-idle window resets on each token chunk.

### Recommended transport (from AIME 2025 scoreboard)

| Use `llm_ask` | Use `llm_ask_stream` |
|---|---|
| Short prompts (< 10 s) | Long reasoning traces (gpt-5 deep mode) |
| Buffer-and-go is fine | Caller wants first-token latency |
| AIME 2025 best: **80%** on AIME I (Run 6, non-streaming + 600 s + retry) | gpt-4o-mini, claude-haiku, etc. |

### Trace events (v1.4.5)

```jsonl
{"kind":"harness.start",        "harness":..., "hash":..., "run_id":..., "provider":..., "model":..., "ts":...}
{"kind":"sub_agent.begin",      "slot":..., "agent":..., "ts":...}
{"kind":"sub_agent.complete",   "slot":..., "agent":..., "output_chars":..., "ts":...}
{"kind":"sub_agent.llm_error",  "slot":..., "attempt":..., "error":..., "ts":...}
{"kind":"assertion.violation",  "slot":..., "attempt":..., "assertion":..., "ts":...}
{"kind":"harness.complete",     "harness":..., "ts":...}
{"kind":"harness.abort",        "harness":..., "reason":..., "ts":...}
```

### Test footprint
56/56 unit tests in `tests/unit/v145_harness_test.cpp` (Phase 0–7 + Phase 3-full lifecycle).

---

## v1.4.5.0 — NeamHarness: Compile-time-validated agent orchestration (April 2026)

**Codename:** NeamHarness — *The Conductor*

### The five new declarations

| Sub-type | Keyword | Purpose |
|---|---|---|
| 25 | `harness` | Compile-time-validated multi-step agent orchestration |
| 26 | `handoff` | Typed cross-session state with schema validation + git versioning |
| 27 | `tool_registry` | Per-role tool allow-lists with `briefs` (planner-only context-economics hints) |
| 28 | `assertion_registry` | CAAF-inspired Unified Assertion Interface (regex/runtime/capability/domain kinds, hard/soft severity) |
| 29 | `harness_benchmark` | Typed benchmark declaration covering the 8-dimension framework + CTCR |

### v1.4.5 surface (29 natives)

- **Lifecycle (4)**: `harness_hash`, `harness_status`, `harness_env`, `handoff_schema_version`
- **Lifecycle FULL (5, v1.4.5.1)**: `harness_start`, `harness_run`, `harness_complete`, `harness_abort`, `harness_trace_path`
- **Handoff I/O (5)**: `handoff_write`, `handoff_read`, `handoff_exists`, `handoff_size`, `handoff_validate`
- **Tool registry (4)**: `tool_registry_check`, `_scope_of`, `_brief`, `_format_briefs`
- **Assertion kernel (5)**: `assertion_check_regex`, `_check_runtime`, `_hard_count`, `_kinds`, `_by_name`
- **Forge introspection (3)**: `forge_role_of`, `forge_function_of`, `forge_ops_of`
- **LLM bridge (3)**: `llm_ask`, `llm_ask_stream` (v1.4.5.1), and the harness-wrapping path

### Worked example (NeamEvolve substrate too)

```neam
budget B { cost: 10.0, tokens: 5000 }
agent Planner   { provider: "openai", model: "gpt-4o-mini", system: "plan",     budget: B }
agent Generator { provider: "openai", model: "gpt-4o-mini", system: "generate", budget: B }
agent Evaluator { provider: "openai", model: "gpt-4o-mini", system: "evaluate", budget: B }

assertion_registry AR {
    no_secrets: { kind: "regex", pattern: "api_key", severity: "hard" }
}

harness Pipeline {
    provider: "openai", model: "gpt-4o-mini", budget: B,
    assertions: AR,
    sub_agents: { plan: Planner, gen: Generator, eval: Evaluator }
}

harness_start("Pipeline");
let answer = harness_run("Pipeline", "build a sorted list of 3 squares");
print(answer);
harness_complete("Pipeline");
print(harness_trace_path("Pipeline"));   // → "runs/<NEAM_RUN_ID>/Pipeline.trace.jsonl"
```

### Compile-time validators

- **H-001** — harness with no `sub_agents` rejected.
- **H-015** — handoff missing `schema_version` rejected (silent breaking changes prevented).
- **P-FR-001** — forge agent `role:` value not in `{planner, generator, evaluator}` rejected.

### CTCR (Compile-Time Catch Rate)

Neam-unique benchmark dimension: fraction of broken programs `neamc` rejects pre-emit. v1.4.5.1 scores **1.000 (60/60)** on the seeded corpus in `Neam_Harness_Benchmarking/ctcr/`.

### Documentation

- [`docs/v1.4.5_Neam_Harness_Usage_Guidelines.md`](docs/v1.4.5_Neam_Harness_Usage_Guidelines.md) — 2378-line end-to-end usage guide (30 sections + 6 production examples)
- [`docs/v1.4.5_Neam_Harness_Architecture.md`](docs/v1.4.5_Neam_Harness_Architecture.md) — 1103-line architecture doc (ecosystem view + detailed component diagram + worked walkthrough)
- [`docs/v1.4.5_Neam_Agentic_Harness_Requirement_Specification.md`](docs/v1.4.5_Neam_Agentic_Harness_Requirement_Specification.md) — formal requirements
- [`docs/v1.4.5_Neam_Agentic_Harness_Implementation_Specification.md`](docs/v1.4.5_Neam_Agentic_Harness_Implementation_Specification.md) — insertion-line C++ plan

---

## Installation

### macOS (arm64) — local install

```bash
# Download the pre-built tarball
tar -xzf neam-macos-arm64-v1.5.0.tar.gz
cd neam-macos-arm64

# Install to ~/.neam/bin (no sudo)
mkdir -p ~/.neam/bin && ./install.sh ~/.neam/bin

# Add to PATH
echo 'export PATH="$HOME/.neam/bin:$PATH"' >> ~/.zshrc
source ~/.zshrc

# Verify
neam-cli --version    # → neam-cli 1.5.0
```

### macOS — `curl | bash` from GitHub releases

```bash
curl -fsSL https://raw.githubusercontent.com/neam-lang/Neam/main/install.sh | bash
```

### Building from source (macOS)

```bash
git clone https://github.com/neam-lang/Neam.git
cd Neam
./mac_build_script.sh -v 1.5.0          # native arch (arm64 or x86_64)
./mac_build_script.sh -v 1.5.0 -a universal   # universal binary
./mac_build_script.sh -v 1.5.0 -t       # build + run tests
```

### Windows (x64)

The Windows binary is built via GitHub Actions on `windows-latest` (MSVC 2022 + vcpkg). Releases are uploaded automatically when a `v*` tag is pushed.

**Trigger a Windows release:**

```bash
# From a maintainer's machine:
git tag v1.5.0
git push origin v1.5.0
# .github/workflows/release.yml runs:
#   - build-linux, build-macos-arm64, build-macos-amd64, build-windows
#   - All four artifacts uploaded to the GitHub Release page
```

**Manual Windows build (on a Windows machine):**

```powershell
git clone https://github.com/neam-lang/Neam.git
cd Neam
vcpkg install curl[ssl]:x64-windows
cmake -B build -G "Visual Studio 17 2022" -A x64 `
  -DCMAKE_BUILD_TYPE=Release `
  -DCMAKE_TOOLCHAIN_FILE="$env:VCPKG_INSTALLATION_ROOT\scripts\buildsystems\vcpkg.cmake"
cmake --build build --config Release --parallel
cmake --install build --config Release
```

The v1.5 source tree is **Windows-portable** — all v1.5 runtime files use `_WIN32`-guarded `gmtime_s` / `_popen` / `_pclose` for cross-compat.

### Linux

```bash
sudo apt install -y build-essential cmake git libcurl4-openssl-dev libssl-dev
git clone https://github.com/neam-lang/Neam.git && cd Neam
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel $(nproc)
sudo cp neamc neam neam-cli /usr/local/bin/
```

### Quick start

```bash
# Compile + run a v1.5 evolve agent under dry-run (no API keys needed)
mkdir -p /tmp/runs
NEAM_HARNESS_DRY_RUN=1 NEAM_RUN_ID=demo \
  NEAM_HARNESS_TRACE_PATH=/tmp/runs/demo.trace.jsonl \
  neam-cli your_evolve_agent.neam

# Inspect the JSONL trace (each line is one event)
cat /tmp/runs/demo.trace.jsonl | jq -c '{kind, prev_hash: .prev_hash[:8], this_hash: .this_hash[:8]}'
```

---

## Earlier release history

(See git history `git log --oneline` for full versions; key milestones below.)

- **v1.4.0** NeamWiki — wiki agent + knowledge graph (24 typed agent types)
- **v1.3.0** NeamLab — research agent + hypothesis tracking + experiment loops
- **v1.2.0** NeamProd — plugins, sessions, eval framework, A2A
- **v1.1.0** NeamOS — knowledge cards, governance rules, blueprints
- **v1.0.0** NeamOne — OWASP ASI security, multi-cloud deployment
- **v0.9.x** 14 data-intelligence agents (DIO orchestrator)
- **v0.8.0** NeamClaw — persistent agents, forge loops, channels

---

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
