# Neam — Agentic AI Programming Language

**v0.6.6** | Compiled DSL for building AI agent systems with native LLM tool calling, RAG, multi-agent orchestration, and multi-cloud deployment.

```
  Neam source (.neam)
       |
       v
  Compiler (neamc)  -->  Bytecode (.neamb)
       |
       v
  VM Runtime (neam)  -->  LLM Providers (OpenAI, Bedrock, Ollama)
       |                        |
       v                        v
  Native Skills  <------  Tool Calls (auto-dispatch)
```

## Table of Contents

- [Quick Start](#quick-start)
- [Building Neam](#building-neam)
- [Native Tool Calling (v0.6.6)](#native-tool-calling-v066)
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

# Or use the CLI (compile + run in one step)
./neam-cli program.neam
```

## Building Neam

See [BUILD_README.md](BUILD_README.md) for detailed build instructions for macOS, Linux, and Windows.

---

## Native Tool Calling (v0.6.6)

Neam v0.6.6 introduces **native skill-to-tool integration** — Neam skills declared in your program are automatically converted to LLM-native tool definitions and dispatched via the provider's tool calling protocol (OpenAI function calling, Claude tool_use, or Ollama tools).

### How It Works

```
  User Query
       |
       v
  Neam VM collects agent.skills --> builds JSON Schema tool definitions
       |
       v
  LLM API call with tools[] array (provider-native format)
       |
       v
  LLM returns tool_use / function_call response
       |
       v
  Neam VM dispatches to skill impl() with converted arguments
       |
       v
  Skill returns result --> sent as tool_result back to LLM
       |
       v
  LLM processes result --> more tool calls OR final text answer
```

### Defining Skills (Auto-Mapped to LLM Tools)

```neam
// This skill becomes an LLM tool automatically:
//   { "name": "lookup_customers",
//     "description": "Look up customers by city name...",
//     "input_schema": { "properties": { "city": {"type":"string"} } } }

skill lookup_customers {
  description: "Look up customers by city name. Returns customer records."
  params: {
    city: string
  }
  impl(city) {
    if (city == "Tokyo") {
      return "Found 2 customers: #101 Tanaka Yuki (47 orders), #205 Sato Hana (23 orders)";
    }
    return "No customers found in " + city;
  }
}

skill calculate {
  description: "Perform a math calculation on a list of numbers."
  params: {
    operation: string,
    numbers: string
  }
  impl(operation, numbers) {
    if (operation == "sum") {
      if (numbers == "47,23") { return "70"; }
    }
    return "Computed " + operation + " on " + numbers;
  }
}
```

### Wiring Skills to Agents

```neam
agent Analyst {
  provider: "openai"
  model: "gpt-4o"
  endpoint: "https://api.openai.com/v1/chat/completions"
  api_key_env: "OPENAI_API_KEY"
  system: "You are a data analyst. Use your tools for all data operations."
  skills: [lookup_customers, calculate]
  guards: [SecurityChain]
  budget: AnalyticsBudget
}

{
  // The LLM decides which skills to call:
  let result = Analyst.ask("How many customers in Tokyo? Sum their orders.");
  emit result;
}
```

### What Happens at Runtime

1. VM reads `agent.skills` and calls `build_skill_schema()` for each skill
2. JSON Schema tool definitions are sent in the LLM API request
3. The LLM decides which tools to call (or responds directly if no tool needed)
4. VM receives tool call responses, maps JSON arguments to Neam Values
5. Skill `impl()` functions execute with full guard/budget enforcement
6. Results flow back to the LLM as tool results
7. Loop continues until the LLM sends a final text response

### Supported Providers

| Provider | Protocol | Config |
|----------|----------|--------|
| **OpenAI** | Function Calling (`tool_calls`) | `OPENAI_API_KEY` |
| **AWS Bedrock** | Claude `tool_use` content blocks | `AWS_ACCESS_KEY_ID`, `AWS_SECRET_ACCESS_KEY` |
| **Ollama** | OpenAI-compatible tools | Local (no key needed) |

### Guards and Budgets on Tool Calls

Guards and budgets are enforced at **every step** in the tool calling loop:

```neam
guard InputValidator {
  description: "Validate all tool inputs"
  on_tool_input(input) {
    // Check, modify, or block the input
    return input;
  }
  on_tool_output(output) {
    return output;
  }
}

guardchain SecurityChain = [InputValidator];

budget AnalyticsBudget {
  api_calls: 30
  tokens: 200000
}
```

### Running the Demo

```bash
# Set your provider key
export OPENAI_API_KEY="your-key"

# Run with INFO-level logging (shows [TOOL] markers for each skill call)
NEAM_LOG_LEVEL=INFO ./neam-cli examples/v066_claude_skill_integration.neam

# Run with DEBUG-level logging (shows full JSON payloads)
NEAM_LOG_LEVEL=DEBUG NEAM_TRACE=1 ./neam-cli examples/v066_claude_skill_integration.neam
```

### Demo Output (Actual)

```
+--------------------------------------------------------------------+
|                                                                    |
|   Neam v0.6.6 -- Native Skill-to-Tool Integration Demo            |
|                                                                    |
|   Skills declared in Neam are auto-converted to LLM tools.        |
|   The LLM decides which tools to call. Neam executes them.        |
|                                                                    |
+--------------------------------------------------------------------+

  DEMO 1: Single Tool Call
  Expect: LLM calls lookup_customers(city=Tokyo)
  ........................................................

  Q: How many customers are in Tokyo?

    [TOOL] lookup_customers(city=Tokyo)

  A: There are 2 customers in Tokyo.

--------------------------------------------------------------------

  DEMO 2: Multi-Step Tool Chain
  Expect: lookup_customers -> calculate -> make_report
  ........................................................

  Q: Look up Tokyo customers, sum their orders, make a report.

    [TOOL] lookup_customers(city=Tokyo)
    [TOOL] calculate(op=sum, numbers=47,23)
    [TOOL] make_report(title=Tokyo Summary)

  A: === REPORT: Tokyo Summary === Total orders: 70 === End of Report ===

--------------------------------------------------------------------

  DEMO 8: Full Workflow (4-Step Orchestration)
  Expect: lookup_customers -> calculate -> make_report -> send_alert
  ........................................................

    [TOOL] lookup_customers(city=Tokyo)
    [TOOL] calculate(op=average, numbers=47,23)
    [TOOL] make_report(title=Tokyo Analysis)
    [TOOL] send_alert(channel=email)

  A: The workflow is complete. Report generated, email alert sent.
```

---

## Knowledge Bases (RAG)

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
| Multi-Agent Collaboration | Researcher -> Writer -> Editor |
| Sequential Pipeline | Data transformation chain |
| Supervisor/Worker | Task validation pattern |
| Router/Dispatcher | Query classification and routing |
| Debate/Adversarial | Pro vs Con with Judge |

### Special Patterns

| Pattern | Description |
|---------|-------------|
| DeepSearch | Plan -> Research -> Synthesize -> Reflect |
| Chain-of-Thought | Explicit step-by-step reasoning |
| ReAct | Thought -> Action -> Observation loop |
| Self-Reflection | Create -> Critique -> Refine |
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

### Example: Supervisor/Worker with Retries

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

### Example: Skill-Equipped Agents (v0.6.6)

```neam
skill search_docs {
  description: "Search documentation by topic"
  params: { topic: string }
  impl(topic) {
    return "Docs for: " + topic;
  }
}

skill write_summary {
  description: "Write a summary from source material"
  params: { source: string }
  impl(source) {
    return "Summary of: " + source;
  }
}

agent ResearchAgent {
  provider: "openai"
  model: "gpt-4o"
  endpoint: "https://api.openai.com/v1/chat/completions"
  api_key_env: "OPENAI_API_KEY"
  system: "Research agent with document search and summarization tools."
  skills: [search_docs, write_summary]
}

{
  // The LLM autonomously decides to search, then summarize
  let result = ResearchAgent.ask("Find docs about RAG and summarize them.");
  emit result;
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

### v0.6.6 — Skill & Tool Integration

| File | Description |
|------|-------------|
| `examples/v066_claude_skill_integration.neam` | **Full demo: 8 scenarios with 5 skills, guards, budgets** |
| `examples/v066_native_tool_calling.neam` | Comprehensive tool calling across all providers |
| `examples/v066_tool_calling_bedrock.neam` | AWS Bedrock-specific tool calling demo |
| `examples/v066_tool_calling_ollama.neam` | Local-only Ollama tool calling (no cloud keys) |

### RAG & Agents

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

---

## Language Basics

### Expressions

```neam
{ 1 + 2; }
```

```neam
{
  3 * (4 + 5);
  10 / 2 + 7;
  (8 - 3) * 4;
}
```

### Variables and Control Flow

```neam
{
  let x = 10;
  let y = 20;

  if (x > 5) {
    emit "x is large";
  }

  let i = 0;
  while (i < 3) {
    emit "iteration " + i;
    i = i + 1;
  }
}
```

### Agents and Skills

```neam
skill greet {
  description: "Greet a user by name"
  params: { name: string }
  impl(name) {
    return "Hello, " + name + "!";
  }
}

agent Assistant {
  provider: "openai"
  model: "gpt-4o-mini"
  endpoint: "https://api.openai.com/v1/chat/completions"
  api_key_env: "OPENAI_API_KEY"
  system: "You are a friendly assistant."
  skills: [greet]
}

{
  let response = Assistant.ask("Say hello to Alice");
  emit response;
}
```

---

## Known Limitations (v0.6.6)

### Type System
The Hindley-Milner type inference system is **parsed but not yet enforced**.
Type annotations are accepted by the parser but type checking is not performed
at compile time. Full type inference is planned for v0.7.0.

### Module System
`module` and `import` declarations are parsed but not yet compiled. Code
organization should use file-based separation for now.

### Test Framework
`test` and `test suite` declarations are parsed but the built-in test runner
is not yet implemented. Use the external evaluation framework in `tests/`.

### String Escapes
The Neam lexer reads strings as raw text between quotes. Standard escape
sequences (`\n`, `\"`, `\\`) are not processed. Use string concatenation
or pipe-separated formats for multi-line content.
