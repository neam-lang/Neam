# Neam Programming Language

**Version:** v0.13 (Draft) — End-to-End Requirements Specification (Jan 05, 2026)

> Latest updates (v0.13, Jan 05, 2026): native terminal multimodal I/O (text,
> images, streaming audio), a Media I/O Kernel with audio device capabilities,
> realtime audio/image output support in the runtime and NeamCode, A2A + EDA
> alignment, and tightened security guardrails around device effects and terminal
> rendering.

Neam is an agent-native programming language and runtime designed to make agentic
applications simple, safe, and production-ready. It combines Rust/Cargo-like
ergonomics with Bun-like simplicity and bakes in agent-focused primitives such as
multi-agent orchestration, capability-scoped effects, structured context lanes,
and governed tool execution (TerminalTool, PythonTool, WebSearchTool).

This repository captures a GitHub-based installer concept for the Neam toolchain
alongside a concise, requirements-aligned overview of the language, runtime, and
ecosystem. Use this repo as the seed for a Neam bootstrapper that installs the
compiler/runtime artifacts published in the upstream
[`Praveengovianalytics/NeamC`](https://github.com/Praveengovianalytics/NeamC)
repository via GitHub Releases.

## Highlights (aligned to the v0.13 specification)

- **Language + Compiler (NeamC):** statically typed, effect-aware compiler that
  performs capability checking and emits reproducible Neam Bundles with schema
  snapshots, policies, and provenance metadata.
- **Runtime + NeamFlow:** C/C++ runtime with deterministic orchestration,
  multi-agent messaging (SYNAPSE), event-driven DAGs, supervision trees, and
  replay-friendly receipts for every tool call and model decision.
- **Context + Memory Kernels:** structured context lanes (policy, role, goal,
  plan, tool, memory, evidence, user, output contract) with budgets and
  injection defenses; human-inspired memory tiers (working, episodic, semantic,
  procedural, social) governed by provenance and policies.
- **Tooling + Ecosystem:** built-in TerminalTool, PythonTool, WebSearchTool with
  sandbox profiles and receipts; MCP host kernel for tools/resources/prompts;
  package ecosystem (NeamPackage) and audited skill marketplace (NeamSkills).
- **Media I/O Kernel + Terminal Rendering:** portable audio input/output,
  inline terminal image rendering (Kitty/iTerm2/Sixel where available), and
  artifact-based media handling with backpressure and cancellation. Device
  effects (AudioInput/AudioOutput/Camera/TerminalRender/MediaExport) are
  explicit capabilities with approval/retention policies enforced by
  NeamC/Neam.
- **NeamCode + Provider Realtime:** terminal agent experience with modality
  routing (text-only, STT→LLM→TTS fallback, or provider-native realtime audio)
  and CLI flags to force/deny audio or images. A2A client/server support is
  aligned with the v0.13 AgentCard + version negotiation requirements.
- **Event- and Interop-Ready:** first-class CloudEvents/AsyncAPI contracts,
  Kafka/NATS adapters, Beam integration option, and Agent2Agent (A2A) protocol
  for cross-vendor collaboration.
- **Observability + Safety:** OpenTelemetry by default, Langfuse/MLflow
  integrations, approval gates for high-risk effects, deny-by-default
  capabilities, and mesh-ready governance with certification manifests.

## GitHub-based installer (blueprint)

The installer is designed to be shipped via GitHub Releases with:

- Deterministic tarball artifacts per platform/arch (Linux/macOS/Windows),
  accompanied by SHA-256 checksums and optional signature files.
- A small, auditable shell installer (`scripts/neam-install.sh`) that performs
  platform detection, checksum verification, optional GPG validation, and
  installation into a versioned directory (default: `/usr/local/neam/<version>`)
  with deterministic symlinks (`neam`, `neamc`).
- Safe defaults: deny ambient network during install steps, avoid curl | sh,
  prefer argv-style command invocation, and require explicit `--execute` to make
  changes (default mode is a dry-run plan).
- Media-aware artifacts: runtime bundles include the Media I/O Kernel, device
  capability manifests, and terminal renderer assets; NeamC enforces that audio
  and camera effects declare policies in the Construct page before builds will
  pass. The installer validates that media capability assets exist (override
  with `--allow-missing-media` only when testing partial bundles).

See [INSTALL.md](./INSTALL.md) for the full installer design, threat model, and
operational guidance.

## Repository layout

- `README.md` — Overview and highlights of Neam v0.13 plus installer summary.
- `INSTALL.md` — GitHub-based installer design, validation flow, and usage.
- `scripts/neam-install.sh` — Portable installer script (plan-first, opt-in
  execute) intended to be distributed via GitHub Releases alongside checksums
  and signatures; defaults target the `Praveengovianalytics/NeamC` release
  artifacts.

## Quick start (planner mode)

```bash
# Preview what the installer would do for v0.13.0 on your platform
./scripts/neam-install.sh --version v0.13.0
```

To actually perform the installation (once release artifacts are published),
run with `--execute` and adjust `--install-dir` as needed:

```bash
sudo ./scripts/neam-install.sh --version v0.13.0 --execute
```

## Detailed setup and usage

### Prerequisites

- `curl` and `tar` installed (for downloads and extraction).
- A checksum tool (`sha256sum` or `shasum`).
- Optional: `gpg` and a trusted keyring if you plan to verify `SHA256SUMS.sig`.
- Sufficient privileges for the chosen install directory (`/usr/local/neam` by
  default; use a user-writable path to avoid `sudo`).

### Install from the NeamC release (default settings)

```bash
# Plan-only dry run: shows computed URLs and targets for v0.13.0
./scripts/neam-install.sh --version v0.13.0

# Perform the install to /usr/local/neam/v0.13.0 and set up bin symlinks
sudo ./scripts/neam-install.sh --version v0.13.0 --execute

# Add the bin directory to your shell PATH (if not already present)
export PATH=\"/usr/local/neam/bin:$PATH\"
```

### Install to a custom location without sudo

```bash
mkdir -p \"$HOME/.local/neam\"
./scripts/neam-install.sh --version v0.13.0 --install-dir \"$HOME/.local/neam\" --execute
export PATH=\"$HOME/.local/neam/bin:$PATH\"
```

### Using alternative release hosts or artifact names

If the upstream release owner/repo or tarball prefix differ from the defaults
(`Praveengovianalytics/NeamC` and `neam-*`), override them explicitly:

```bash
./scripts/neam-install.sh \
  --version v0.13.0 \
  --owner your-org --repo your-repo \
  --artifact-prefix customprefix \
  --install-dir "$HOME/.local/neam" \
  --execute
```

## Sample Neam workflows (conceptual)

Once binaries are installed, Neam aims to deliver a Cargo-style agentic workflow.
The commands below illustrate how a typical Neam developer could scaffold and
run agentic programs (names and flags reflect the v0.13 specification draft):

```bash
# Create a new agentic project using built-in patterns and templates
neam init my-agentic-app --template react-loop
cd my-agentic-app

# Inspect and edit the Construct Page (single-page blueprint)
${EDITOR:-vi} Construct.neam

# Build with NeamC (type + capability checks; produces a Neam Bundle)
neam build

# Run the default Construct locally (respecting security profile)
neam run --profile dev

# Tail traces and receipts for observability/debugging
neam trace --follow

# Evaluate against an offline regression suite (MLflow/Langfuse ready)
neam eval --suite smoke

# Publish the bundle to a registry and optional mesh registry
neam publish --registry https://registry.example.com

# Deploy with a Docker/K8s profile
neam deploy --profile prod
```

Additional domain workflows:

- **Event-driven**: `neam event connect --adapter kafka --list-topics` followed
  by `neam event run --topic transactions --handler fraud_detector`.
- **A2A interop**: `neam a2a serve --bundle target/neam/bundle` to expose an
  AgentCard, or `neam a2a discover https://peer.example.com` to delegate tasks.
- **Ingestion for RAG/GraphRAG**: `neam ingest --profile vec-dev` to run
  VectorIngest, or `neam ingest --profile graph-dev` for GraphIngest.

## Status

This is a working draft focused on installer and documentation scaffolding.
Compiler/runtime binaries and release assets are not yet published; the
installer operates in plan mode by default to avoid accidental partial installs.

---

# Neam sample programs

Quick examples you can use to exercise the current `neamc` compiler + VM pipeline.

## Knowledge bases (RAG)

Neam supports first-class knowledge bases via the `knowledge` declaration. Knowledge bases ingest
sources at runtime and expose relevant context to connected agents through `connected_knowledge`.

```neam
knowledge NeamDocs {
  vector_store: "usearch",
  embedding_model: "nomic-embed-text",
  chunk_size: 128,
  chunk_overlap: 32,
  sources: [
    { type: "file", path: "./readme.md" }
  ]
}

agent SupportBot {
  provider: "ollama",
  model: "qwen2.5:14b",
  system: "You answer using the provided knowledge context.",
  connected_knowledge: [NeamDocs]
}

{
  emit "Starting Knowledge Query...";
  let answer = SupportBot.ask("How do I build NeamC?");
  emit answer;
}
```

### Knowledge configuration fields

* `vector_store`: The embedded vector backend (currently `usearch`).
* `embedding_model`: The embedding model identifier (used for future provider wiring).
* `chunk_size`: Sliding window token count per chunk.
* `chunk_overlap`: Number of overlapping tokens between chunks.
* `sources`: Array of `{ type, path }` source descriptors.

### Supported source types

* `file`: Ingest local files. Paths can include `*` wildcards.
* `web`: Fetch and ingest HTML content from a URL.

### Sample programs

* `examples/knowledge_simple.neam`: Basic RAG query pipeline using a local file source.
* `examples/rag_researcher.neam`: Research-focused agent connected to multiple docs.
* `tests/knowledge_basic.neam`: Minimal knowledge initialization and query for smoke tests.

## Simple arithmetic

```neam
{ 1 + 2; }
```

Compile and run:

```bash
neamc math.neam -o math.neamb
neam math.neamb
```

## Nested blocks and duplication

```neam
{
  3 * (4 + 5);
  {
    -1 + 2;
  }
}
```

This demonstrates block statements and unary negation.

## Chained expressions

```neam
{
  10 / 2 + 7;
  (8 - 3) * 4;
}
```

Multiple expressions in a block are compiled sequentially; each expression result is popped to keep the stack clean.

## Negative numbers

```neam
{
  -42;
  -(1 + 2 * 3);
}
```

Unary negation lowers to `OP_NEGATE` before arithmetic combines the values.

## Agentic AI patterns (conceptual)

The current parser focuses on arithmetic expressions, but the runtime model already includes `AgentRef` values. Below are forward-looking examples to illustrate intended usage once agent declarations and events are wired through the compiler:

```neam
agent Planner {
  // Future: plan tasks and emit structured intents
}

agent Worker {
  // Future: execute intents and emit receipts
}

{
  Planner.plan("summarize report");
  Worker.execute(Planner.last_plan);
}
```

```neam
{
  // Conceptual event emission pipeline
  let decision = Planner.decide("route inquiry");
  emit decision;
  emit "hand-off to worker";
}
```

These snippets are illustrative; parser and codegen support for agent declarations, method calls, and event emission will arrive in later phases.

### Multi-agent orchestration sketch

```neam
agent Router { }
agent Summarizer { }
agent Reviewer { }

{
  // Router inspects the request and chooses a path
  let route = Router.decide("summarize vs. translate");
  emit route;

  // Summarizer executes then emits a receipt
  let summary = Summarizer.summarize("input doc");
  emit summary;

  // Reviewer validates downstream
  let verdict = Reviewer.review(summary);
  emit verdict;
}
```

### Supervisor with retries

```neam
agent Supervisor { }
agent Worker { }

{
  let attempt = 0;
  let success = false;

  while (!success && attempt < 3) {
    attempt = attempt + 1;
    let result = Worker.execute("task payload");
    success = Supervisor.validate(result);
    emit "attempt " + attempt;
    emit result;
  }

  if (!success) {
    emit "fallback escalation";
  }
}
```

### Event-driven tool invocation

```neam
agent Planner { }
agent Toolbelt { }

{
  let plan = Planner.plan("extract key facts");
  emit plan;

  // Hypothetical tool call sequence
  let data = Toolbelt.call("search", "topic query");
  let notes = Toolbelt.call("summarize", data);
  emit notes;
}
```

## 🌟 Phase 6 RAG Examples

Neam's native knowledge system enables powerful agentic workflows. Here are complete examples:

### 1. Basic RAG (`knowledge_simple.neam`)
The minimal "Hello World" for RAG.
```neam
knowledge MyWiki {
  vector_store: "usearch",
  embedding_model: "nomic-embed-text",
  chunk_size: 64,
  chunk_overlap: 16,
  sources: [
    { type: "file", path: "./readme.md" }
  ]
}

agent Helper {
  provider: "ollama",
  model: "qwen2.5:14b",
  system: "You answer using the provided knowledge context.",
  connected_knowledge: [MyWiki]
}

{
  emit "Starting Knowledge Query...";
  let answer = Helper.ask("How do I build NeamC?");
  emit answer;
}
```

### 2. Deep Researcher (`rag_researcher.neam`)
Connects an agent to multiple sources (files + web) for synthesis.
```neam
knowledge ResearchDocs {
  vector_store: "usearch",
  embedding_model: "nomic-embed-text",
  sources: [
    { type: "file", path: "./README.md" },
    { type: "file", path: "./readme.md" }
  ]
}

agent Researcher {
  provider: "ollama",
  model: "qwen2.5:14b",
  system: "You are a deep-dive technical researcher.",
  connected_knowledge: [ResearchDocs]
}

{
  let response = Researcher.ask("Summarize Neam positioning.");
  emit response;
}
```

### 3. Medical Advisor (`medical_advisor.neam`)
Demonstrates **Safety Guardrails** using system prompts and temperature control.
```neam
knowledge MedicalGuidelines {
  vector_store: "native",
  embedding_model: "biomed-embed-v1",
  sources: [
    { type: "file", path: "./data/clinical_guidelines.pdf" },
    { type: "web", path: "https://cdc.gov/protocols" }
  ]
}

agent DrNeam {
  provider: "openai",
  model: "gpt-4-turbo",
  temperature: 0.1, // Deterministic
  system: "ONLY answer based on Knowledge Base. If unsure, say 'I do not know'.",
  connected_knowledge: [MedicalGuidelines]
}

{
  let q1 = "What is the recommended dosage?";
  emit "A: " + DrNeam.ask(q1);
}
```

### 4. Legal Assistant (`legal_assistant.neam`)
Demonstrates **Control Flow** based on agent responses.
```neam
knowledge LegalCorpus {
  vector_store: "native",
  sources: [ { type: "file", path: "./data/contracts/*.txt" } ]
}

agent ParalegalBot {
  provider: "ollama",
  model: "llama3:8b",
  system: "Cite section numbers. If unsure, state 'Insufficient Data'.",
  connected_knowledge: [LegalCorpus]
}

{
  let answer = ParalegalBot.ask("Termination notice period?");
  if (answer == "Insufficient Data") {
     emit "Action: Flag for manual review.";
  }
}
```
