# Neam - Agentic AI Programming Language

Neam is a domain-specific language for building AI agent systems with first-class support for LLM providers, RAG (Retrieval-Augmented Generation), and multi-agent orchestration.

## Table of Contents

- [Quick Start](#quick-start)
- [Building Neam](#building-neam)
- [Knowledge Bases (RAG)](#knowledge-bases-rag)
- [RAG Retrieval Strategies](#rag-retrieval-strategies)
- [Agentic Patterns](#agentic-patterns)
- [API Server](#api-server)
- [Package Manager](#package-manager-neam-pkg)
- [Examples](#examples)

---

## Quick Start

```bash
# Build
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel

# Run a Neam program
./neamc program.neam -o program.neamb
./neam program.neamb
```

## Building Neam

See [BUILD_README.md](BUILD_README.md) for detailed build instructions for macOS, Linux, and Windows.

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

---

## RAG Retrieval Strategies

Neam supports 7 retrieval strategies for knowledge bases:

| Strategy | Description | Use Case |
|----------|-------------|----------|
| `basic` | Standard vector similarity | Simple Q&A |
| `mmr` | Maximal Marginal Relevance | Diverse results |
| `hybrid` | Keyword + vector search | Precise matching |
| `hyde` | Hypothetical Document Embeddings | Abstract queries |
| `self_rag` | Self-reflective with relevance check | High accuracy |
| `crag` | Corrective RAG with query decomposition | Complex questions |
| `agentic` | Iterative refinement with reflection | Research tasks |

### Example: Using Different Strategies

```neam
knowledge BasicKB {
  vector_store: "usearch"
  embedding_model: "nomic-embed-text"
  chunk_size: 200
  chunk_overlap: 50
  sources: [ { "type": "file", "path": "./docs.md" } ]
  retrieval_strategy: "basic"
  top_k: 3
}

knowledge HyDEKB {
  vector_store: "usearch"
  embedding_model: "nomic-embed-text"
  chunk_size: 200
  chunk_overlap: 50
  sources: [ { "type": "file", "path": "./docs.md" } ]
  retrieval_strategy: "hyde"
  top_k: 3
  num_hypothetical: 1
}

knowledge SelfRAGKB {
  vector_store: "usearch"
  embedding_model: "nomic-embed-text"
  chunk_size: 200
  chunk_overlap: 50
  sources: [ { "type": "file", "path": "./docs.md" } ]
  retrieval_strategy: "self_rag"
  top_k: 4
  enable_relevance_check: true
}
```

### Strategy Options

| Option | Strategies | Description |
|--------|------------|-------------|
| `top_k` | All | Number of documents to retrieve |
| `mmr_lambda` | mmr | Balance relevance (1.0) vs diversity (0.0) |
| `num_hypothetical` | hyde | Hypothetical docs to generate |
| `enable_relevance_check` | self_rag | Check document relevance |
| `enable_query_decomposition` | crag | Break complex queries |
| `max_iterations` | agentic | Refinement iterations |
| `enable_reflection` | agentic | Enable self-reflection |

See `examples/Neam_test_examples.md` for comprehensive examples.

---

## Agentic Patterns

Neam supports various multi-agent orchestration patterns:

### Basic Patterns

| Pattern | Description |
|---------|-------------|
| Single Agent | Basic Q&A with one agent |
| Multi-Agent Collaboration | Researcher → Writer → Editor |
| Sequential Pipeline | Data transformation chain |
| Supervisor/Worker | Task validation pattern |
| Router/Dispatcher | Query classification and routing |
| Debate/Adversarial | Pro vs Con with Judge |

### Special Patterns

| Pattern | Description |
|---------|-------------|
| DeepSearch | Plan → Research → Synthesize → Reflect |
| Chain-of-Thought | Explicit step-by-step reasoning |
| ReAct | Thought → Action → Observation loop |
| Self-Reflection | Create → Critique → Refine |
| Planning | Goal decomposition and monitoring |
| Socratic | Teaching through questions |
| Red/Blue Team | Security attack + defense analysis |
| Memory | Contextual fact extraction and retrieval |

### Example: Multi-Agent Collaboration

```neam
agent Researcher {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You are a researcher. Provide factual information."
}

agent Writer {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You are a writer. Create polished prose from notes."
}

agent Editor {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You are an editor. Improve text for clarity."
}

{
  let research = Researcher.ask("Key facts about AI");
  let draft = Writer.ask("Write about: " + research);
  let final_text = Editor.ask("Edit: " + draft);
  emit final_text;
}
```

### Example: Supervisor/Worker

```neam
agent Supervisor {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "Evaluate work. Reply APPROVED or NEEDS_REVISION."
}

agent Worker {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "Complete assigned tasks thoroughly."
}

{
  let task = "List 3 benefits of exercise";
  let result = Worker.ask(task);
  let validation = Supervisor.ask("Evaluate: " + result);
  emit validation;
}
```

See `examples/Agentic_Patterns_readme.md` for all patterns with examples.

---

## API Server

Neam agents can be exposed as REST API endpoints using the native `neam-api` server built in C++.

### Building the API Server

```bash
# From the build directory
cmake --build . --target neam-api --parallel

# Verify build
./neam-api --help
```

### Quick Start

```bash
# Set API key
export OPENAI_API_KEY="your-key"

# Start the native server
./neam-api --port 8080

# Query an agent
curl -X POST http://localhost:8080/api/v1/agent/ask \
  -H "Content-Type: application/json" \
  -d '{"agent_id": "assistant", "query": "Hello!"}'
```

### Command Line Options

| Option | Description | Default |
|--------|-------------|---------|
| `--host HOST` | Host to bind to | `0.0.0.0` |
| `--port PORT` | Port to listen on | `8080` |
| `--help` | Show help message | - |

### API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/v1/health` | GET | Health check |
| `/api/v1/agents` | GET | List available agents |
| `/api/v1/agent/ask` | POST | Query an agent |

### Available Agents

| Agent ID | Description | RAG |
|----------|-------------|-----|
| `assistant` | General purpose helpful assistant | No |
| `coder` | Expert programmer, code solutions | No |
| `analyst` | Data analysis and insights | No |
| `writer` | Creative writing | No |
| `researcher` | Research with knowledge base | Yes |

### Example: Health Check

```bash
curl http://localhost:8080/api/v1/health
```

Response:
```json
{"status": "healthy", "version": "1.0.0", "server": "neam-api"}
```

### Example: List Agents

```bash
curl http://localhost:8080/api/v1/agents
```

### Example: Query Agent

```bash
curl -X POST http://localhost:8080/api/v1/agent/ask \
  -H "Content-Type: application/json" \
  -d '{"agent_id": "coder", "query": "Write a Python quicksort"}'
```

Response:
```json
{
  "agent_id": "coder",
  "query": "Write a Python quicksort",
  "response": "def quicksort(arr):\n    if len(arr) <= 1:\n        return arr\n    ..."
}
```

### Architecture

```
┌─────────────┐     ┌──────────────────┐     ┌─────────────┐
│   Client    │────▶│   neam-api       │────▶│   Neam VM   │
│  (curl/app) │     │  (C++ HTTP)      │     │  (native)   │
└─────────────┘     └──────────────────┘     └─────────────┘
                            │
                            ▼
                    ┌──────────────────┐
                    │  LLM Provider    │
                    │  (OpenAI/Ollama) │
                    └──────────────────┘
```

See `examples/api_server/README.md` for full documentation including Docker deployment and production setup.

---

## Package Manager (neam-pkg)

Neam includes a full-featured package manager for managing dependencies, similar to pip (Python), cargo (Rust), or npm (Node.js).

### Building the Package Manager

```bash
# From the build directory
cmake --build . --target neam-pkg --parallel

# Verify build
./neam-pkg --help
```

### Quick Start

```bash
# Initialize a new project
neam-pkg init my-project

# Install a package
neam-pkg install agent-utils

# Install as dev dependency
neam-pkg install --dev test-framework

# List installed packages
neam-pkg list

# Update all packages
neam-pkg update
```

### Commands

| Command | Description |
|---------|-------------|
| `neam-pkg init [name]` | Initialize a new Neam project |
| `neam-pkg install` | Install dependencies from neam.toml |
| `neam-pkg install <pkg>` | Install a specific package |
| `neam-pkg install <pkg>@<ver>` | Install a specific version |
| `neam-pkg install --dev <pkg>` | Install as dev dependency |
| `neam-pkg update` | Update all packages |
| `neam-pkg update <pkg>` | Update a specific package |
| `neam-pkg remove <pkg>` | Remove a package |
| `neam-pkg list` | List installed packages |
| `neam-pkg outdated` | Show outdated packages |
| `neam-pkg search <query>` | Search for packages |
| `neam-pkg info <pkg>` | Show package information |
| `neam-pkg login` | Login to registry |
| `neam-pkg publish` | Publish package to registry |
| `neam-pkg cache clean` | Clean the package cache |
| `neam-pkg cache info` | Show cache information |

### Project Structure

```
my-project/
├── neam.toml           # Project manifest (required)
├── neam.lock           # Lock file (auto-generated)
├── src/
│   └── main.neam       # Main entry point
├── tests/
│   └── ...
└── .neam/
    └── packages/       # Installed packages
```

### neam.toml Manifest

```toml
neam_version = "1.0"

[project]
name = "my-project"
version = "0.1.0"
description = "My Neam project"
type = "binary"
authors = ["Developer <dev@example.com>"]
license = "MIT"

[project.entry_points]
main = "src/main.neam"

[dependencies]
utils = "^1.0.0"
ai-tools = { git = "https://github.com/org/repo" }

[dev-dependencies]
test-framework = "0.1.0"

[agent]
provider = "openai"
model = "gpt-4o-mini"
```

### Version Constraints

| Constraint | Example | Matches |
|------------|---------|---------|
| Exact | `"1.0.0"` | Only 1.0.0 |
| Caret | `"^1.0.0"` | 1.0.0 to <2.0.0 |
| Tilde | `"~1.0.0"` | 1.0.0 to <1.1.0 |
| Greater | `">=1.0.0"` | 1.0.0 and above |
| Range | `">=1.0.0, <2.0.0"` | Custom range |

See `docs/PACKAGE_ECOSYSTEM.md` for full documentation and `examples/PACKAGING_GUIDE.md` for packaging best practices.

---

## Examples

### Example Files

| File | Description |
|------|-------------|
| `examples/rag_basic_strategies.neam` | Basic, MMR, Hybrid RAG |
| `examples/rag_advanced_strategies.neam` | HyDE, Self-RAG, CRAG, Agentic |
| `examples/rag_all_strategies.neam` | All 7 strategies comparison |
| `examples/agentic_patterns_openai.neam` | 6 orchestration patterns |
| `examples/agentic_rag_patterns.neam` | RAG-enhanced patterns |
| `examples/special_agents_openai.neam` | 8 special agent patterns |

### Documentation

| File | Description |
|------|-------------|
| `BUILD_README.md` | Build instructions (Mac/Linux/Windows) |
| `docs/LEARN_NEAM_EXAMPLES.md` | **Part 1: Fundamentals, Agents, Basic RAG, Multi-Agent Patterns** |
| `docs/LEARN_NEAM_EXAMPLES_PART2.md` | **Part 2: Special Agents, RAG Strategies, Packaging, Web APIs** |
| `examples/Neam_test_examples.md` | RAG strategies guide |
| `examples/Agentic_Patterns_readme.md` | Agentic patterns guide |
| `examples/api_server/README.md` | API server documentation |
| `examples/PACKAGING_GUIDE.md` | Project packaging guide |
| `docs/PACKAGE_ECOSYSTEM.md` | Package ecosystem design |
| `docs/REGISTRY_IMPLEMENTATION.md` | Package registry implementation spec |
| `docs/LANDING_PAGE_SPEC.md` | Neam.dev website specification |

---

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
