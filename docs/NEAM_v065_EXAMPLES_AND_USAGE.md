# Neam v0.6.5 — Complete Examples & Usage Guide

> Neam is a compiled DSL for building AI agent systems with first-class support for LLM providers, RAG, multi-agent orchestration, multi-cloud deployment, GPU/SIMD acceleration, and FinOps.

---

## Table of Contents

- [1. Core Language](#1-core-language)
- [2. Agents](#2-agents)
- [3. Knowledge Bases (RAG)](#3-knowledge-bases-rag)
- [4. Skills](#4-skills)
- [5. Tools, Guards & Capabilities](#5-tools-guards--capabilities)
- [6. Budget & Resource Management](#6-budget--resource-management)
- [7. Environment Configuration](#7-environment-configuration)
- [8. Memory, World Model & Planning](#8-memory-world-model--planning)
- [9. Subagents & Connectors](#9-subagents--connectors)
- [10. Built-in Functions](#10-built-in-functions)
- [11. Testing Framework](#11-testing-framework)
- [12. Checkpoint & Rewind](#12-checkpoint--rewind-time-travel)
- [13. Module System](#13-module-system)
- [14. Complete Example Programs](#14-complete-example-programs)
- [15. Environment Variables](#15-environment-variables)
- [16. Multi-Cloud Deployment](#16-multi-cloud-deployment)
- [17. Multi-Cloud Orchestration](#17-multi-cloud-orchestration)
- [18. GPU & SIMD Acceleration](#18-gpu--simd-acceleration)
- [19. Intelligent Auto-Scaling](#19-intelligent-auto-scaling)
- [20. FinOps (Financial Operations)](#20-finops-financial-operations)
- [21. Cloud Testing Framework](#21-cloud-testing-framework)
- [22. Complete Cloud Deployment Examples](#22-complete-cloud-deployment-examples)
- [23. Deployment Targets Summary](#23-deployment-targets-summary)
- [24. Cloud Provider Support Matrix](#24-cloud-provider-support-matrix)

---

## 1. Core Language

### Data Types & Variables

```neam
// Numbers
let x = 42;
let pi = 3.14;

// Strings
let name = "Neam";

// Booleans
let active = true;

// Nil
let empty = nil;

// Lists
let items = [1, 2, 3, "four"];
let nested = [[1, 2], [3, 4]];

// Maps
let person = { "name": "Alice", "age": 30 };

// Constants (immutable)
const MAX_SIZE = 100;
const API_URL = "https://api.example.com";
```

### Control Flow

```neam
// Conditionals
if (x > 0) {
  print("positive");
} else {
  print("non-positive");
}

// While loops
let i = 0;
while (i < 10) {
  print(i);
  i = i + 1;
}

// For-in loops
for (item in list) {
  print(item);
}
```

### Functions

```neam
fun greet(name) {
  return "Hello, " + name;
}

fun fibonacci(n) {
  if (n <= 1) { return n; }
  return fibonacci(n - 1) + fibonacci(n - 2);
}

let result = greet("World");
```

### Operators

| Category | Operators |
|---|---|
| Arithmetic | `+`, `-`, `*`, `/` |
| Comparison | `==`, `!=`, `>`, `<`, `>=`, `<=` |
| Logical | `!` (NOT) |
| String | `+` (concatenation) |

---

## 2. Agents

### Basic Agent

```neam
agent Assistant {
  provider: "openai",
  model: "gpt-4o-mini",
  system: "You are a helpful assistant. Answer concisely."
}

let answer = Assistant.ask("What is the capital of France?");
emit answer;
```

### Agent with Full Configuration

```neam
agent Analyst {
  provider: "openai",
  model: "gpt-4o",
  system: "You are a data analyst. Be precise and factual.",
  temperature: 0.3,
  api_key_env: "OPENAI_API_KEY",
  skills: [Calculator, WebSearch],
  connected_knowledge: [CompanyDocs],
  guardchains: [SafetyChain],
  budget: AnalysisBudget,
  env: Production,
  memory: SessionMemory
}
```

### Multi-Agent Pipeline

```neam
agent Researcher {
  provider: "openai",
  model: "gpt-4o",
  system: "Research topics thoroughly. Cite sources."
}

agent Writer {
  provider: "openai",
  model: "gpt-4o-mini",
  system: "Write clear, engaging articles from research notes."
}

agent Editor {
  provider: "openai",
  model: "gpt-4o-mini",
  system: "Edit for grammar, clarity, and accuracy."
}

let research = Researcher.ask("Latest developments in quantum computing");
let draft = Writer.ask("Write a 500-word article based on: " + research);
let final = Editor.ask("Edit this article: " + draft);
emit final;
```

### Local LLM Agent (Ollama)

```neam
agent LocalBot {
  provider: "ollama",
  model: "qwen2.5:14b",
  system: "You are a coding assistant.",
  endpoint: "http://localhost:11434"
}

let code = LocalBot.ask("Write a Python function to sort a list");
emit code;
```

### AWS Bedrock Agent

```neam
agent CloudAgent {
  provider: "bedrock",
  model: "anthropic.claude-3-5-sonnet-20241022-v2:0",
  system: "Answer concisely in one sentence."
}

let answer = CloudAgent.ask("Explain machine learning");
emit answer;
```

### Supported LLM Providers

| Provider | Value | Endpoint | Auth |
|---|---|---|---|
| Ollama | `"ollama"` | `http://localhost:11434` | None (local) |
| OpenAI | `"openai"` | `https://api.openai.com/v1` | `OPENAI_API_KEY` |
| AWS Bedrock | `"bedrock"` | AWS regional endpoint | AWS credentials |

---

## 3. Knowledge Bases (RAG)

### Basic RAG

```neam
knowledge ProductDocs {
  vector_store: "usearch",
  embedding_model: "nomic-embed-text",
  chunk_size: 200,
  chunk_overlap: 50,
  sources: [
    { type: "file", path: "./docs/readme.md" },
    { type: "file", path: "./docs/api.md" }
  ]
}

agent Support {
  provider: "openai",
  model: "gpt-4o-mini",
  system: "Answer questions using the product documentation.",
  connected_knowledge: [ProductDocs]
}

let answer = Support.ask("How do I install the CLI?");
```

### 7 Retrieval Strategies

#### 1. BASIC — Standard Vector Similarity

```neam
knowledge BasicKB {
  vector_store: "usearch",
  embedding_model: "nomic-embed-text",
  chunk_size: 200,
  chunk_overlap: 50,
  sources: [{ type: "file", path: "./data.md" }],
  retrieval_strategy: "basic",
  top_k: 3
}
```

#### 2. MMR — Maximal Marginal Relevance (Diversity-Aware)

```neam
knowledge DiverseKB {
  vector_store: "usearch",
  embedding_model: "nomic-embed-text",
  chunk_size: 200,
  chunk_overlap: 50,
  sources: [{ type: "file", path: "./data.md" }],
  retrieval_strategy: "mmr",
  top_k: 5,
  mmr_lambda: 0.7   // 1.0 = pure relevance, 0.0 = pure diversity
}
```

#### 3. HYBRID — Keyword + Vector Combined Search

```neam
knowledge HybridKB {
  vector_store: "usearch",
  embedding_model: "nomic-embed-text",
  chunk_size: 200,
  chunk_overlap: 50,
  sources: [{ type: "file", path: "./data.md" }],
  retrieval_strategy: "hybrid",
  top_k: 3
}
```

#### 4. HyDE — Hypothetical Document Embeddings

```neam
knowledge HyDEKB {
  vector_store: "usearch",
  embedding_model: "nomic-embed-text",
  chunk_size: 200,
  chunk_overlap: 50,
  sources: [{ type: "file", path: "./data.md" }],
  retrieval_strategy: "hyde",
  top_k: 3,
  num_hypothetical: 1
}
```

#### 5. Self-RAG — Self-Reflective Retrieval

```neam
knowledge SelfRAGKB {
  vector_store: "usearch",
  embedding_model: "nomic-embed-text",
  chunk_size: 200,
  chunk_overlap: 50,
  sources: [{ type: "file", path: "./data.md" }],
  retrieval_strategy: "self_rag",
  top_k: 4,
  enable_relevance_check: true,
  enable_support_check: true
}
```

#### 6. CRAG — Corrective RAG with Query Decomposition

```neam
knowledge CRAGKB {
  vector_store: "usearch",
  embedding_model: "nomic-embed-text",
  chunk_size: 200,
  chunk_overlap: 50,
  sources: [{ type: "file", path: "./data.md" }],
  retrieval_strategy: "crag",
  top_k: 3,
  enable_query_decomposition: true,
  enable_web_fallback: false,
  max_corrections: 2
}
```

#### 7. Agentic RAG — Tool-Based Retrieval with Reflection

```neam
knowledge AgenticKB {
  vector_store: "usearch",
  embedding_model: "nomic-embed-text",
  chunk_size: 200,
  chunk_overlap: 50,
  sources: [{ type: "file", path: "./data.md" }],
  retrieval_strategy: "agentic",
  top_k: 3,
  max_iterations: 5,
  enable_reflection: true
}
```

---

## 4. Skills

```neam
skill Calculator {
  description: "Performs arithmetic calculations",
  params: [
    { name: "expression", schema: { "type": "string", "description": "Math expression" } }
  ],
  impl: fun(expression) {
    return eval_math(expression);
  }
}

skill WebLookup {
  description: "Fetches data from a URL",
  params: [
    { name: "url", schema: { "type": "string", "description": "URL to fetch" } }
  ],
  impl: fun(url) {
    let response = http_get(url);
    return response;
  }
}

agent SmartBot {
  provider: "openai",
  model: "gpt-4o-mini",
  system: "Use your skills to help users.",
  skills: [Calculator, WebLookup]
}
```

---

## 5. Tools, Guards & Capabilities

### Capabilities — Permission Boundaries

```neam
capability FileAccess {
  pattern: "file:*"
}

capability DatabaseAccess {
  pattern: "db:*"
}
```

### Guards — Input/Output Validation

```neam
guard InputValidator {
  description: "Validates input data",
  handlers: [
    on_tool_input(input) -> Bool {
      if (input.length > 10000) { return false; }
      return true;
    },
    on_tool_output(output) -> Bool {
      return output != nil;
    }
  ]
}
```

### Guard Chains — Ordered Validation Pipeline

```neam
guardchain SecurityChain {
  guards: [InputValidator, OutputSanitizer, RateLimiter]
}
```

### Tools — Capability-Gated Executable Actions

```neam
tool ReadFile {
  description: "Reads a file from disk",
  capabilities: [FileAccess],
  params: [
    { name: "path", type: String, default: "./data.txt" }
  ],
  returns: String,
  guards: [InputValidator],
  impl: fun(path) {
    return file_read_string(path);
  }
}
```

---

## 6. Budget & Resource Management

```neam
budget QuickTask {
  time: 5000,      // milliseconds
  cost: 0.10,      // dollars
  tokens: 5000
}

budget HeavyAnalysis {
  time: 60000,
  cost: 2.00,
  tokens: 100000
}

agent CheapBot {
  provider: "openai",
  model: "gpt-4o-mini",
  system: "Be brief and efficient.",
  budget: QuickTask
}
```

---

## 7. Environment Configuration

```neam
env Production {
  API_URL: "https://api.prod.com",
  DEBUG: "false",
  API_KEY: env("PROD_API_KEY")
}

env Development {
  API_URL: "http://localhost:8080",
  DEBUG: "true",
  API_KEY: env("DEV_API_KEY")
}

agent ProdAgent {
  provider: "openai",
  model: "gpt-4o",
  system: "Production agent",
  env: Production
}
```

---

## 8. Memory, World Model & Planning

### Memory Systems

```neam
memory ConversationMemory {
  backend: "redis",
  retention: "session",       // "session", "persistent", "temporary"
  max_events: 10000,
  snapshot_interval: 100
}
```

### World Model

```neam
world_model TaskWorld {
  tier: 1,
  state_schema: "task_state_v1",
  update_frequency: 1000       // milliseconds
}
```

### Planning System

```neam
plan HierarchicalPlanner {
  pattern: "hierarchical",
  max_depth: 5,
  backtrack: true,
  pruning: "alpha_beta"
}
```

### Combined Agent

```neam
agent StrategicAgent {
  provider: "openai",
  model: "gpt-4o",
  system: "Think strategically and plan ahead.",
  memory: ConversationMemory,
  world_model: TaskWorld,
  plan: HierarchicalPlanner
}
```

---

## 9. Subagents & Connectors

### Subagents — Delegated Workers

```neam
subagent Worker {
  base_agent: "MainAgent",
  budget_share: 0.5,            // 50% of parent budget
  capability_inherit: true,
  isolation: false
}
```

### Connectors — External Service Integration

```neam
connector SlackAPI {
  protocol: "http",
  endpoint: "https://slack.com/api",
  contract: "slack-v1",
  auth: env("SLACK_TOKEN")
}

agent SlackBot {
  provider: "openai",
  model: "gpt-4o-mini",
  system: "Slack assistant",
  connector: SlackAPI
}
```

---

## 10. Built-in Functions

### Math (25+ functions)

```neam
math_abs(-5);                // 5
math_floor(3.7);             // 3
math_ceil(3.2);              // 4
math_round(3.5);             // 4
math_min(5, 10);             // 5
math_max(5, 10);             // 10
math_clamp(15, 0, 10);       // 10
math_pow(2, 8);              // 256
math_sqrt(16);               // 4
math_cbrt(27);               // 3
math_log(x);                 // Natural log
math_log10(x);               // Base-10 log
math_exp(x);                 // e^x
math_sin(angle);             // Trig functions
math_cos(angle);
math_tan(angle);
math_asin(val);
math_acos(val);
math_atan(val);
math_atan2(y, x);
math_random();               // 0.0 to 1.0
math_random_int(1, 100);     // 1 to 100
```

### JSON

```neam
let obj = json_parse('{"name": "Alice", "age": 30}');
let text = json_stringify(obj);
```

### Time

```neam
let now = clock();                           // Seconds (float)
let ms = time_now();                         // Milliseconds
let us = time_now_micros();                  // Microseconds
time_sleep(1000);                            // Sleep 1000ms
let fmt = time_format(ms, "%Y-%m-%d");       // Format timestamp
let ts = time_parse("2024-01-15", "%Y-%m-%d"); // Parse date string
```

### File I/O

```neam
let content = file_read_string("./data.txt");
file_write_string("./output.txt", "Hello");
let bytes = file_read_bytes("./image.png");
file_write_bytes("./copy.png", bytes);
let exists = file_exists("./file.txt");
file_remove("./temp.txt");
file_copy("./src.txt", "./dst.txt");
file_rename("./old.txt", "./new.txt");
```

### HTTP

```neam
let resp = http_get("https://api.example.com/data");

let headers = { "Content-Type": "application/json" };
let body = '{"key": "value"}';
let resp = http_request("POST", "https://api.example.com", body, headers);
```

### Crypto

```neam
let hash = crypto_hash("sha256", "data");
let hmac = crypto_hmac("sha256", "secret", "message");
let uuid = crypto_uuid_v4();
let encoded = crypto_base64_encode("hello");
let decoded = crypto_base64_decode("aGVsbG8=");
let hex = crypto_hex_encode([255, 0, 128]);
let rand_bytes = crypto_random_bytes(32);
```

### Futures / Async

```neam
let resolved = future_resolve(42);
let rejected = future_reject("error");
let all = future_all([f1, f2, f3]);
let first = future_race([f1, f2]);
let delayed = future_delay(1000);    // Resolves after 1s
```

### I/O & Utilities

```neam
print("Hello", "World", 123);     // Print to stdout
let input_val = input();           // Read from stdin
emit "result";                     // Emit value (program output)
let t = typeof(value);             // Type introspection
```

### Error Handling

```neam
panic("Fatal error occurred");
let result = context(try_operation(), "Failed to load config");
let result = with_context(try_operation(), "file", "./config.json");
```

---

## 11. Testing Framework

```neam
test "addition works" {
  assert_eq(add(2, 3), 5);
}

test "string concatenation" {
  let result = "Hello" + " " + "World";
  assert_eq(result, "Hello World");
}

test "async operation" {
  attributes: [timeout(5000), async]
  let result = slow_function();
  assert_ok(result);
}
```

### Assertion Functions

| Function | Description |
|---|---|
| `assert_eq(a, b)` | Assert equality |
| `assert_ne(a, b)` | Assert inequality |
| `assert_true(cond)` | Assert truthy |
| `assert_false(cond)` | Assert falsy |
| `assert_some(opt)` | Assert Option has value |
| `assert_none(opt)` | Assert Option is None |
| `assert_ok(result)` | Assert Result is Ok |
| `assert_err(result)` | Assert Result is Err |
| `assert_throws(fn)` | Assert throws exception |

---

## 12. Checkpoint & Rewind (Time Travel)

```neam
checkpoint "safe_point";

let result = risky_operation();
if (!result) {
  rewind "safe_point";   // Roll back to checkpoint
}
```

---

## 13. Module System

```neam
module my.app.utils;

import std.list;
import std.map;
import my.app.config as cfg;
import std.list { map, filter, reduce };  // Selective import
import std.math.*;                         // Wildcard import

pub fun public_helper() { }   // Exported
fun internal_helper() { }     // Private (default)
crate fun crate_visible() { } // Crate-level visibility
```

---

## 14. Complete Example Programs

### Hello World

```neam
print("Hello, Neam!");
```

### Simple Q&A Agent

```neam
agent QA {
  provider: "openai",
  model: "gpt-4o-mini",
  system: "Answer questions concisely in one sentence."
}

let answer = QA.ask("What is the speed of light?");
emit answer;
```

### RAG Document Assistant

```neam
knowledge Docs {
  vector_store: "usearch",
  embedding_model: "nomic-embed-text",
  chunk_size: 200,
  chunk_overlap: 50,
  sources: [
    { type: "file", path: "./docs/guide.md" },
    { type: "file", path: "./docs/faq.md" }
  ],
  retrieval_strategy: "hybrid",
  top_k: 5
}

agent DocAssistant {
  provider: "openai",
  model: "gpt-4o",
  system: "Answer questions based on the documentation. Cite sources.",
  connected_knowledge: [Docs]
}

let question = input();
let answer = DocAssistant.ask(question);
emit answer;
```

### Multi-Agent Research System

```neam
budget ResearchBudget {
  time: 120000,
  cost: 5.00,
  tokens: 200000
}

agent Planner {
  provider: "openai",
  model: "gpt-4o",
  system: "Break research topics into sub-questions.",
  budget: ResearchBudget
}

agent Researcher {
  provider: "openai",
  model: "gpt-4o",
  system: "Research each question deeply. Provide citations."
}

agent Synthesizer {
  provider: "openai",
  model: "gpt-4o",
  system: "Synthesize research into a coherent report."
}

let topic = "Impact of AI on healthcare in 2025";
let plan = Planner.ask("Create 5 research questions about: " + topic);
let findings = Researcher.ask("Research these questions:\n" + plan);
let report = Synthesizer.ask("Write a report from:\n" + findings);
emit report;
```

### Data Processing Pipeline

```neam
let data = file_read_string("./input.csv");
let lines = data.split("\n");

let results = [];
let i = 1;
while (i < lines.length) {
  let fields = lines[i].split(",");
  let processed = {
    "name": fields[0],
    "score": math_round(fields[1] * 100) / 100
  };
  results = results + [processed];
  i = i + 1;
}

let output = json_stringify(results);
file_write_string("./output.json", output);
print("Processed " + results.length + " records");
```

---

## 15. Environment Variables

| Variable | Default | Range | Description |
|---|---|---|---|
| `NEAM_MAX_CALL_DEPTH` | 1000 | 1 – 100,000 | Max recursion depth |
| `NEAM_MAX_REACT_STEPS` | 100 | 1 – 10,000 | Max ReAct loop iterations |
| `NEAM_REPL_HISTORY` | 1000 | 10 – 100,000 | REPL history size |
| `OPENAI_API_KEY` | — | — | OpenAI API key |
| `AWS_ACCESS_KEY_ID` | — | — | AWS credentials for Bedrock |
| `AWS_SECRET_ACCESS_KEY` | — | — | AWS credentials for Bedrock |
| `AWS_REGION` | us-east-1 | — | AWS region for Bedrock |

---

## 16. Multi-Cloud Deployment

Neam compiles agents to native binaries and generates deployment artifacts for 6 targets. All deployment generators are built into the `neam` CLI.

### Kubernetes

```bash
neam deploy --target kubernetes \
  --replicas 3 \
  --min-replicas 1 \
  --max-replicas 20 \
  --cpu 500m \
  --memory 1Gi \
  --namespace production
```

Generates:

| File | Purpose |
|---|---|
| `deployment.yaml` | Deployment with replicas, CPU/memory limits, health probes |
| `service.yaml` | ClusterIP service |
| `configmap.yaml` | Environment configuration |
| `ingress.yaml` | Nginx ingress HTTP routing |

### GCP Cloud Run (Serverless Containers)

```bash
neam deploy --target gcp-cloudrun \
  --region us-central1 \
  --concurrency 80 \
  --cpu-boost           # Faster cold starts
```

### AWS Lambda (Serverless Functions)

```bash
neam deploy --target aws-lambda \
  --memory 1024 \
  --timeout 30 \
  --arch arm64
```

### Docker

```bash
neam deploy --target docker
# Generates:
#   build/deploy/docker/Dockerfile
#   build/deploy/docker/docker-compose.yml

docker build -t my-agent -f build/deploy/docker/Dockerfile .
docker run -e OPENAI_API_KEY=$OPENAI_API_KEY my-agent
```

### Helm Charts

```bash
neam deploy --target helm \
  --chart-name my-agent \
  --chart-version 1.0.0

# Generates full chart at build/deploy/helm/my-agent/
helm install my-agent build/deploy/helm/my-agent/
```

### Terraform (Infrastructure as Code)

```bash
neam deploy --target terraform
# Generates provider-agnostic IaC at build/deploy/terraform/
terraform init && terraform apply
```

### Generated Artifacts

All deployment artifacts are written to `build/deploy/`:

```
build/deploy/
  kubernetes/
    deployment.yaml
    service.yaml
    configmap.yaml
    ingress.yaml
  cloudrun/
    service.yaml
  aws/
    template.yaml          # SAM template
  helm/
    myapp/
      Chart.yaml
      values.yaml
      templates/
  docker/
    Dockerfile
    docker-compose.yml
  terraform/
    main.tf
```

### Stdlib Deployment Modules

The standard library provides Neam-native deployment definitions:

```
stdlib/project/deploy/
  targets/
    local.neam
    docker.neam
    kubernetes.neam
    terraform.neam
    serverless/
      aws_lambda.neam
      gcp_cloudrun.neam
      azure_functions.neam
  generators/
    dockerfile.neam
    k8s_manifests.neam
    helm_chart.neam
    serverless.neam
  cli/commands/
    deploy.neam
```

---

## 17. Multi-Cloud Orchestration

### Cost-Aware Router

The `CostAwareRouter` selects the cheapest cloud provider in real time by monitoring spot prices, latency, and data residency rules across AWS, GCP, Azure, and Alibaba Cloud.

```neam
// Neam automatically routes to the cheapest cloud at runtime
agent CostOptimizedAgent {
  provider: "openai",
  model: "gpt-4o-mini",
  system: "Cost-optimized agent with multi-cloud routing."
}

// Runtime behavior:
// 1. Polls spot prices across AWS, GCP, Azure, Alibaba
// 2. Evaluates latency from current region
// 3. Enforces data residency constraints
// 4. Routes to cheapest option
// 5. Automatic failover on provider errors
```

### Routing Constraints

| Constraint | Description |
|---|---|
| `max_latency` | Maximum acceptable latency (ms) |
| `max_cost` | Maximum cost per request ($) |
| `preferred_regions` | Preferred deployment regions |
| `preferred_providers` | Preferred cloud providers |
| `data_residency` | Required data residency (e.g., `"EU"`, `"US"`) |

### Cross-Cloud Failover

```neam
// Automatic failover policy:
//   - After 3 consecutive errors on a provider → failover
//   - 60-second cooldown before retrying failed provider
//   - Routes to next cheapest alternative
//   - Falls back to on-demand if all spot unavailable
```

### Alibaba Cloud Integration

Native Alibaba Cloud support for APAC deployments:

```neam
env APACConfig {
  PREFERRED_CLOUD: "alibaba",
  PREFERRED_REGION: "ap-southeast-1"
}

agent APACAgent {
  provider: "openai",
  model: "gpt-4o-mini",
  system: "APAC-optimized agent.",
  env: APACConfig
}
```

**Alibaba services supported:**

| Service | Description |
|---|---|
| Function Compute | Serverless functions |
| ECS | Elastic Compute Service (VMs) |
| ACK | Container Service for Kubernetes |
| GPU (GN6i/GN6v/GN7i) | T4 / V100 / A10 GPU instances |

**Alibaba regions:** `cn-hangzhou`, `cn-shanghai`, `cn-beijing`, `ap-southeast-1`, `eu-central-1`

---

## 18. GPU & SIMD Acceleration

### GPU Acceleration

GPU is automatically used for compute-heavy operations when available.

**Supported backends:**

| Backend | Platform |
|---|---|
| CUDA | NVIDIA GPUs |
| Metal | Apple Silicon / macOS |
| OpenCL | Cross-platform |
| Vulkan | Cross-platform |
| CPU | Fallback (always available) |

**Auto-accelerated operations:**

- Embedding similarity search (cosine distance)
- Top-K document retrieval (batch sorted)
- Batch normalization
- Large vector operations

**Default settings:** batch size 32, memory pool 512MB

```neam
// No code changes needed — GPU acceleration is automatic
knowledge LargeCorpus {
  vector_store: "usearch",
  embedding_model: "nomic-embed-text",
  chunk_size: 200,
  chunk_overlap: 50,
  sources: [
    { type: "file", path: "./corpus/volume1.md" },
    { type: "file", path: "./corpus/volume2.md" },
    { type: "file", path: "./corpus/volume3.md" }
  ],
  retrieval_strategy: "hybrid",
  top_k: 20
  // GPU accelerates cosine similarity + top-K retrieval
}
```

**GPU instance types by cloud:**

| Cloud | Instance | GPU | VRAM |
|---|---|---|---|
| AWS | g4dn.xlarge | T4 | 16 GB |
| AWS | g5.xlarge | A10G | 24 GB |
| AWS | p4d.24xlarge | A100 | 40 GB |
| GCP | n1-standard-4 + T4 | T4 | 16 GB |
| GCP | a2-highgpu-1g | A100 | 40 GB |
| Azure | NC4as_T4_v3 | T4 | 16 GB |
| Azure | NC24ads_A100_v4 | A100 | 80 GB |
| Alibaba | ecs.gn6i-c4g1 | T4 | 16 GB |
| Alibaba | ecs.gn6v-c8g1 | V100 | 16 GB |
| Alibaba | ecs.gn7i-c8g1 | A10 | 24 GB |

```bash
# Deploy with GPU
neam deploy --target kubernetes --gpu nvidia-t4 --gpu-memory 16Gi --replicas 2
```

### SIMD Acceleration

Auto-detected at compile time. No code changes needed.

**Supported instruction sets:**

| ISA | Width | Platform |
|---|---|---|
| AVX-512 | 512-bit | x86 (Intel/AMD) |
| AVX2 | 256-bit | x86 (Intel/AMD) |
| SSE4 | 128-bit | x86 (Intel/AMD) |
| NEON | 128-bit | ARM (Apple Silicon, AWS Graviton) |
| SVE | Scalable | ARM (server-class) |

**470+ accelerated operations including:**

| Category | Operations |
|---|---|
| Vector | Dot product, cosine similarity, L2 distance, normalize |
| Matrix | Matrix-vector multiply, element-wise ops |
| Activation | ReLU, GELU, Softmax, Sigmoid, Tanh |
| Quantization | float32 <-> int8, int8 dot product |
| String | Substring search, character count, case conversion |
| JSON | Structure validation, key search, array counting |
| Text | Whitespace tokenization, word/line counting |
| Memory | Fast memcpy, memset, prefetch |
| Batch | Top-K similar, batch normalization |

---

## 19. Intelligent Auto-Scaling

### Predictive Scaling

ML-based time-series forecasting scales before traffic spikes arrive.

**Metrics monitored:**

| Metric | Description |
|---|---|
| Request rate | Requests per second |
| Queue depth | Pending request count |
| Latency p50/p95/p99 | Response time percentiles |
| CPU utilization | Processor usage % |
| Memory utilization | RAM usage % |
| GPU utilization | GPU usage % |
| Cost rate | Dollars per hour |

**Forecast windows:** 5 min, 1 hour, 24 hours

**Pattern detection:** daily cycles, weekly cycles, anomaly spikes

### Scaling Actions

| Action | Trigger |
|---|---|
| Scale up replicas | Load forecast exceeds capacity |
| Scale down replicas | Sustained low utilization |
| Switch to spot | Spot price < 70% of on-demand |
| Switch to on-demand | Spot interruption notice |
| Switch to FaaS | Bursty, unpredictable traffic |
| Switch to containers | Sustained, steady load |

### Warm Pool

Pre-warmed instances eliminate cold start latency:

- Instances kept ready in standby pool
- Pre-loaded model weights and embeddings
- Pre-established database connections
- Auto-replenished as pool is consumed during scale-out

### Spot Instance Optimization

```neam
// Automatic spot vs on-demand decision:
//   1. Check spot prices across all 4 clouds
//   2. If spot < 70% of on-demand -> use spot
//   3. Monitor for spot interruption notices
//   4. Automatic failover to on-demand on interruption
//   5. Cross-cloud arbitrage for cheapest compute
```

**Cloud spot configurations:**

| Cloud | Strategy | Spot Weight |
|---|---|---|
| AWS ECS | Spot + On-Demand mix | 80% spot / 20% on-demand |
| GCP | Preemptible VMs | Enabled |
| Azure AKS | Spot node pool | Deallocate eviction policy |
| Alibaba ECS | SpotWithPriceLimit | Configurable max price |

---

## 20. FinOps (Financial Operations)

### Cost Attribution

Real-time cost tracking across 10 categories:

| # | Category | Description |
|---|---|---|
| 1 | Compute | CPU/GPU hours |
| 2 | Memory | RAM allocation |
| 3 | Storage | Disk / object storage |
| 4 | Network | Data transfer |
| 5 | LLM API | Chat completion calls |
| 6 | Embedding API | Embedding generation calls |
| 7 | Vector DB | Search / index operations |
| 8 | State Backend | Session / memory storage |
| 9 | External APIs | Third-party service calls |
| 10 | Miscellaneous | Other costs |

**Attribution dimensions:** per agent, per task, per user, per project, per provider, per region, per instance type

```neam
// Budget with alerts
budget TeamBudget {
  cost: 500.00,       // $500/day
  tokens: 10000000    // 10M tokens/day
}

// Alert thresholds (built-in):
//   - Warning at 80% consumed
//   - Critical at 95% consumed
//   - Hard limit enforcement (optional)

agent TrackedAgent {
  provider: "openai",
  model: "gpt-4o-mini",
  system: "Cost-tracked agent.",
  budget: TeamBudget
}
```

**Export:** CSV/JSON for external FinOps tools

### FinOps Dashboard

Real-time monitoring served at `http://localhost:8080`.

**14 widget types:**

| Widget | Type |
|---|---|
| Cost over time | Line chart |
| Cost by category | Pie chart |
| Cost by agent | Bar chart |
| Cost by provider | Bar chart |
| Budget utilization | Gauge |
| Top spenders | Table |
| Latency trends | Line chart |
| Throughput | Line chart |
| CPU utilization | Gauge |
| Memory utilization | Gauge |
| GPU utilization | Gauge |
| Error rate | Line chart |
| Alerts feed | List |
| Savings opportunities | Table |

**Features:**

- WebSocket streaming for live updates
- Time range presets: 1h, 6h, 24h, 7d, 30d
- Export to JSON/PDF
- Dark mode
- Embeddable widgets (HTML/iframe)

```neam
env Monitored {
  FINOPS_DASHBOARD: "true",
  FINOPS_PORT: "8080",
  ALERT_WEBHOOK: env("SLACK_WEBHOOK"),
  BUDGET_WARNING_PCT: "80",
  BUDGET_CRITICAL_PCT: "95"
}

agent MonitoredAgent {
  provider: "openai",
  model: "gpt-4o-mini",
  system: "Production agent with cost tracking.",
  env: Monitored
}
```

### Continuous Benchmarking

Automated benchmarks with regression detection, integrated into CI/CD.

**Tracked metrics:**

| Metric | Variants |
|---|---|
| Latency | p50, p95, p99, min, max, avg, stddev |
| Throughput | req/s, tokens/s |
| Cost | Per request, per 1K tokens |
| Resources | CPU, memory, GPU |
| Quality | Error rate, cache hit rate |

**Regression thresholds:**

| Condition | Verdict |
|---|---|
| Latency increase > 10% | FAIL |
| Cost increase > 5% | FAIL |
| Throughput decrease > 10% | FAIL |
| Statistical significance | p-value < 0.05 |

**CI/CD output:** JUnit XML, GitHub Actions annotations

---

## 21. Cloud Testing Framework

### Overview

The cloud test framework runs identical agents across all 4 cloud providers, comparing performance and cost.

### Test Configuration

```toml
# tests/cloud/cloud_test_config.toml (507 lines)

# 5 pre-configured test agents:
#   1. Simple Q&A         — gpt-4o-mini, 256 tokens
#   2. RAG Knowledge      — gpt-4o, 1024 tokens, vector search
#   3. Multi-Step Reasoning — claude-3-5-sonnet, 4096 tokens
#   4. Voice Pipeline     — STT -> Agent -> TTS
#   5. GPU-Intensive      — llama-3.1-70b, 40GB GPU

# 5 workload profiles:
#   1. Latency Test       — 1,000 requests, concurrency 1
#   2. Throughput Test    — 5,000 requests, concurrency 50, 300s
#   3. Cost Analysis      — 100 requests, realistic workload
#   4. Voice Pipeline     — 50 requests, audio files
#   5. GPU Workload       — 200 requests, concurrency 4
```

### Running Cloud Tests

```bash
# Full test suite across all providers
./tests/cloud/run_cloud_tests.sh

# Single provider
./tests/cloud/run_cloud_tests.sh --provider aws

# Single workload
./tests/cloud/run_cloud_tests.sh --workload latency

# Dry run (validate config only)
./tests/cloud/run_cloud_tests.sh --dry-run
```

### Test Prompts

```
tests/cloud/prompts/
  latency_test.jsonl     # 20+ simple prompts for latency testing
  cost_test.jsonl        # Complex prompts for cost analysis
```

### Example Output

```
+================+==========+==========+==========+==========+===========+
|                |   Neam v0.6.5 Cloud Test Results                      |
+================+==========+==========+==========+==========+===========+
| Metric         |   AWS    |   GCP    |  Azure   | Alibaba  |   Best    |
+================+==========+==========+==========+==========+===========+
| Latency p50    |  120ms   |  115ms   |  130ms   |  140ms   | GCP       |
| Latency p99    |  350ms   |  320ms   |  380ms   |  400ms   | GCP       |
| Throughput     |  45/s    |  48/s    |  42/s    |  38/s    | GCP       |
| Cost/1K req    |  $0.42   |  $0.38   |  $0.45   |  $0.31   | Alibaba   |
| Cold start     |   80ms   |   65ms   |   95ms   |  110ms   | GCP       |
| Memory (RSS)   |   12MB   |   12MB   |   12MB   |   12MB   | (tie)     |
| Binary size    |   ~4MB   |   ~4MB   |   ~4MB   |   ~4MB   | (tie)     |
+================+==========+==========+==========+==========+===========+
```

### Cloud Provider Test Configurations

**AWS:**

| Service | Config |
|---|---|
| Lambda | 256MB–4GB, arm64, provisioned concurrency |
| ECS Fargate | 256–4096 CPU units, spot enabled (80% weight) |
| EC2 | t3.medium to r6i.large, spot at 70% of on-demand |
| EC2 GPU | g4dn, g5, p4d with spot |
| Bedrock | Claude, Titan models |

**GCP:**

| Service | Config |
|---|---|
| Cloud Functions | 256MB–8GB, Python 3.11 |
| Cloud Run | 1–8 CPUs, 512MB–16GB, CPU boost, 80 concurrency |
| Compute Engine | e2-medium to c2-standard-4, spot/preemptible |
| Compute Engine GPU | T4, L4, A100 |
| Vertex AI | Gemini 1.5/2.0 models |

**Azure:**

| Service | Config |
|---|---|
| Azure Functions | Consumption/Premium, 256MB–2GB |
| Container Apps | 0.25–4 CPUs, 0.5–8GB memory |
| AKS | D2s_v3 to F4s_v2, spot enabled |
| VM GPU | NC4as (T4), NC24ads (A100) |
| Azure OpenAI | gpt-4o, gpt-4o-mini |

**Alibaba:**

| Service | Config |
|---|---|
| Function Compute | 128MB–3GB, custom runtime |
| ECI | 0.25–8 CPUs, spot enabled |
| ECS | g6, c6, r6 instances, spot strategy |
| ECS GPU | GN6i (T4), GN6v (V100), GN7i (A10) |
| PAI | Qwen models |

---

## 22. Complete Cloud Deployment Examples

### Example 1: Cost-Optimized RAG Service

```neam
budget ProductionBudget {
  cost: 100.00,
  tokens: 5000000
}

env Production {
  PREFERRED_CLOUD: "auto",
  DATA_RESIDENCY: "US",
  ENABLE_SPOT: "true",
  SPOT_MAX_PRICE: "0.70"
}

knowledge ProductDocs {
  vector_store: "usearch",
  embedding_model: "nomic-embed-text",
  chunk_size: 200,
  chunk_overlap: 50,
  sources: [
    { type: "file", path: "./docs/product.md" },
    { type: "file", path: "./docs/faq.md" },
    { type: "file", path: "./docs/troubleshooting.md" }
  ],
  retrieval_strategy: "hybrid",
  top_k: 5
}

guard RateLimiter {
  description: "Rate limits requests per user",
  handlers: [
    on_tool_input(input) -> Bool {
      return true;
    }
  ]
}

guardchain SecurityChain {
  guards: [RateLimiter]
}

agent SupportBot {
  provider: "openai",
  model: "gpt-4o-mini",
  system: "You are a product support agent. Use documentation to answer accurately.",
  connected_knowledge: [ProductDocs],
  guardchains: [SecurityChain],
  budget: ProductionBudget,
  env: Production
}

let question = input();
let answer = SupportBot.ask(question);
emit answer;
```

```bash
neam deploy --target kubernetes \
  --replicas 3 --min-replicas 1 --max-replicas 20 \
  --cpu 500m --memory 1Gi --namespace production
```

### Example 2: Multi-Cloud Multi-Agent Pipeline

```neam
budget ResearchBudget {
  cost: 25.00,
  tokens: 500000
}

agent Planner {
  provider: "bedrock",
  model: "anthropic.claude-3-5-sonnet-20241022-v2:0",
  system: "Decompose research topics into 5 focused sub-questions.",
  budget: ResearchBudget
}

agent Researcher {
  provider: "openai",
  model: "gpt-4o",
  system: "Research each question. Provide detailed findings with sources."
}

agent Synthesizer {
  provider: "ollama",
  model: "qwen2.5:14b",
  system: "Combine research findings into a coherent report."
}

let topic = "Impact of quantum computing on cryptography";
let questions = Planner.ask("Create research plan for: " + topic);
let findings = Researcher.ask("Research these questions:\n" + questions);
let report = Synthesizer.ask("Write comprehensive report from:\n" + findings);

file_write_string("./report.md", report);
emit report;
```

### Example 3: GPU-Accelerated Large Corpus Agent

```neam
knowledge LargeCorpus {
  vector_store: "usearch",
  embedding_model: "nomic-embed-text",
  chunk_size: 200,
  chunk_overlap: 50,
  sources: [
    { type: "file", path: "./corpus/volume1.md" },
    { type: "file", path: "./corpus/volume2.md" },
    { type: "file", path: "./corpus/volume3.md" },
    { type: "file", path: "./corpus/volume4.md" }
  ],
  retrieval_strategy: "agentic",
  top_k: 10,
  max_iterations: 5,
  enable_reflection: true
}

agent DeepExpert {
  provider: "bedrock",
  model: "anthropic.claude-3-5-sonnet-20241022-v2:0",
  system: "You are a domain expert. Use all available knowledge.",
  connected_knowledge: [LargeCorpus]
}

let question = input();
let answer = DeepExpert.ask(question);
emit answer;
```

```bash
neam deploy --target kubernetes --gpu nvidia-t4 --gpu-memory 16Gi --replicas 2
```

### Example 4: Serverless Event-Driven Agent

```neam
budget ServerlessBudget {
  time: 10000,
  cost: 0.01,
  tokens: 2000
}

agent EventHandler {
  provider: "bedrock",
  model: "anthropic.claude-3-5-sonnet-20241022-v2:0",
  system: "Process events concisely. Return structured JSON.",
  budget: ServerlessBudget
}

let event = input();
let result = EventHandler.ask("Process this event and return JSON: " + event);
emit result;
```

```bash
# AWS Lambda
neam deploy --target aws-lambda --memory 512 --timeout 15 --arch arm64

# GCP Cloud Run
neam deploy --target gcp-cloudrun --region us-central1 --concurrency 80 --cpu-boost
```

### Example 5: FinOps-Monitored Production System

```neam
budget TeamBudget {
  cost: 500.00,
  tokens: 10000000
}

env Monitored {
  FINOPS_DASHBOARD: "true",
  FINOPS_PORT: "8080",
  ALERT_WEBHOOK: env("SLACK_WEBHOOK"),
  BUDGET_WARNING_PCT: "80",
  BUDGET_CRITICAL_PCT: "95"
}

knowledge InternalDocs {
  vector_store: "usearch",
  embedding_model: "nomic-embed-text",
  chunk_size: 200,
  chunk_overlap: 50,
  sources: [{ type: "file", path: "./docs/" }],
  retrieval_strategy: "mmr",
  top_k: 5,
  mmr_lambda: 0.7
}

agent ProductionAgent {
  provider: "openai",
  model: "gpt-4o-mini",
  system: "Production agent with cost tracking.",
  connected_knowledge: [InternalDocs],
  budget: TeamBudget,
  env: Monitored
}

// FinOps dashboard auto-starts at http://localhost:8080
let question = input();
let answer = ProductionAgent.ask(question);
emit answer;
```

---

## 23. Deployment Targets Summary

| Target | Command | Use Case |
|---|---|---|
| Docker | `neam deploy --target docker` | Local / dev containers |
| Kubernetes | `neam deploy --target kubernetes` | Production clusters |
| Helm | `neam deploy --target helm` | K8s package management |
| AWS Lambda | `neam deploy --target aws-lambda` | Serverless (AWS) |
| GCP Cloud Run | `neam deploy --target gcp-cloudrun` | Serverless containers (GCP) |
| Azure Functions | `neam deploy --target azure-functions` | Serverless (Azure) |
| Terraform | `neam deploy --target terraform` | Infrastructure as Code |

---

## 24. Cloud Provider Support Matrix

| Feature | AWS | GCP | Azure | Alibaba |
|---|---|---|---|---|
| **Serverless** | Lambda | Cloud Run | Functions | Function Compute |
| **Containers** | ECS Fargate | Cloud Run | Container Apps | ECI |
| **VMs** | EC2 | Compute Engine | Virtual Machines | ECS |
| **GPU** | g4dn / g5 / p4d | T4 / L4 / A100 | NC4 / NC24 | GN6i / GN6v / GN7i |
| **Managed LLM** | Bedrock | Vertex AI | Azure OpenAI | PAI |
| **Spot / Preemptible** | Yes | Yes | Yes | Yes |
| **Kubernetes** | EKS | GKE | AKS | ACK |
| **Cost Routing** | Yes | Yes | Yes | Yes |
| **Auto-Scaling** | Yes | Yes | Yes | Yes |
| **Data Residency** | US / EU / AP | US / EU / AP | US / EU / AP | CN / AP / EU |

---

## v0.6.5 Feature Summary

### Core Language
- Variables, constants, functions, closures, recursion
- Lists, maps, strings, numbers, booleans, nil
- Control flow: if/else, while, for-in
- Module system with imports and visibility modifiers
- Checkpoint/rewind (time travel debugging)
- Comprehensive testing framework with 9 assertion types

### Agent System
- 3 LLM providers: Ollama (local), OpenAI, AWS Bedrock
- Multi-agent pipelines with arbitrary chaining
- Skills with typed parameters and implementations
- Tools with capability gates and guard validation
- Guard chains for ordered input/output validation
- Budget enforcement (time, cost, tokens)
- Memory systems (Redis-backed session/persistent)
- World models and hierarchical planning
- Subagent delegation with budget sharing
- Connectors for external service integration

### Knowledge Bases (RAG)
- 7 retrieval strategies: Basic, MMR, Hybrid, HyDE, Self-RAG, CRAG, Agentic
- USearch vector store with configurable chunking
- Automatic embedding via nomic-embed-text

### Built-in Functions (60+)
- Math (25+), JSON, Time, File I/O, HTTP, Crypto, Futures, Assertions

### Cloud & Deployment
- 7 deployment targets: Docker, Kubernetes, Helm, Lambda, Cloud Run, Azure Functions, Terraform
- 4 cloud providers: AWS, GCP, Azure, Alibaba Cloud
- Cost-aware multi-cloud routing with spot optimization
- Cross-cloud failover (3 errors, 60s cooldown)
- GPU acceleration: CUDA, Metal, OpenCL, Vulkan
- SIMD acceleration: AVX-512, AVX2, SSE4, NEON, SVE (470+ ops)
- Predictive auto-scaling with ML forecasting
- Warm pool for zero cold-start scale-out
- FinOps dashboard with 14 widget types
- Continuous benchmarking with regression detection
- Cloud testing framework across all 4 providers

### v0.6.5 Improvements (over v0.6.4)
- Improved error messages with context (variable names, types, indices)
- Raised hard-coded limits: lists/maps from 255 to 65,535 elements
- Configurable runtime limits via environment variables
- Registry client rewritten with libcurl + proper JSON parsing
- 85 new test cases across 4 test suites (all passing)
- Async runtime deprecation notes (migration planned for v0.7.0)
