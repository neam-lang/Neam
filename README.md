# Neam

**The programming language for AI agents.**

Neam is a compiled domain-specific language for building AI agent systems. It provides first-class support for LLM providers, RAG (Retrieval-Augmented Generation), multi-agent orchestration, multi-cloud deployment, GPU/SIMD acceleration, and cost management — all in a clean, expressive syntax.

```neam
agent Assistant {
  provider: "openai",
  model: "gpt-4o-mini",
  system: "You are a helpful assistant."
}

let answer = Assistant.ask("What is the capital of France?");
emit answer;
```

---

## Setup

### Prerequisites

| Requirement | Version |
|---|---|
| C++ compiler | C++20 (GCC 12+, Clang 15+, MSVC 2022+) |
| CMake | 3.20+ |
| libcurl | Any recent version |
| OpenSSL | 1.1+ (for crypto and AWS Bedrock signing) |

### Build from Source

```bash
git clone https://github.com/neam-lang/Neam.git
cd Neam

mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --parallel
```

This produces two binaries:

| Binary | Purpose |
|---|---|
| `neamc` | Compiler — compiles `.neam` source to `.neamb` bytecode |
| `neam-cli` | Runtime — executes bytecode, includes interactive REPL |

### Verify Installation

```bash
# Compile and run a program
echo '{ emit 1 + 2; }' > hello.neam
./neamc hello.neam -o hello.neamb
./neam-cli hello.neamb
# Output: 3

# Or use the interactive REPL
./neam-cli
neam> 1 + 2
3
neam> let x = "hello"
neam> x
hello
```

### Run Tests

```bash
cd build
ctest --output-on-failure
```

---

## Quick Start

### Hello World

```neam
print("Hello, Neam!");
```

### Variables and Functions

```neam
let name = "World";
const MAX = 100;

fun greet(who) {
  return "Hello, " + who + "!";
}

emit greet(name);
```

### Control Flow

```neam
let i = 0;
while (i < 5) {
  print(i);
  i = i + 1;
}

for (item in [10, 20, 30]) {
  print(item);
}
```

---

## Agents

Agents are the core abstraction. An agent wraps an LLM provider with a system prompt and calls it via `.ask()`.

```neam
agent QA {
  provider: "openai",
  model: "gpt-4o-mini",
  system: "Answer questions concisely in one sentence."
}

let answer = QA.ask("What is the speed of light?");
emit answer;
```

### Supported Providers

| Provider | Value | Auth |
|---|---|---|
| Ollama (local) | `"ollama"` | None |
| OpenAI | `"openai"` | `OPENAI_API_KEY` env var |
| AWS Bedrock | `"bedrock"` | `AWS_ACCESS_KEY_ID` + `AWS_SECRET_ACCESS_KEY` |

### Local LLM (Ollama)

```neam
agent LocalBot {
  provider: "ollama",
  model: "qwen2.5:14b",
  system: "You are a coding assistant.",
  endpoint: "http://localhost:11434"
}
```

### Multi-Agent Pipeline

Chain agents together for complex workflows:

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

### Full Agent Configuration

```neam
agent Analyst {
  provider: "openai",
  model: "gpt-4o",
  system: "You are a data analyst.",
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

---

## Knowledge Bases (RAG)

Connect agents to documents for grounded answers.

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
  system: "Answer using the product documentation.",
  connected_knowledge: [ProductDocs]
}

let answer = Support.ask("How do I install the CLI?");
```

### 7 Retrieval Strategies

| Strategy | Description |
|---|---|
| `"basic"` | Standard vector similarity search |
| `"mmr"` | Maximal Marginal Relevance — diversity-aware results |
| `"hybrid"` | Combined keyword + vector search |
| `"hyde"` | Hypothetical Document Embeddings |
| `"self_rag"` | Self-reflective retrieval with relevance checks |
| `"crag"` | Corrective RAG with query decomposition |
| `"agentic"` | Tool-based retrieval with planning and reflection |

```neam
knowledge SmartKB {
  vector_store: "usearch",
  embedding_model: "nomic-embed-text",
  chunk_size: 200,
  chunk_overlap: 50,
  sources: [{ type: "file", path: "./data.md" }],
  retrieval_strategy: "hybrid",
  top_k: 5
}
```

---

## Skills and Tools

### Skills

Extend agents with callable functions:

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

agent SmartBot {
  provider: "openai",
  model: "gpt-4o-mini",
  system: "Use your skills to help users.",
  skills: [Calculator]
}
```

### Guards and Capabilities

Control what tools can do with permission boundaries and validation pipelines:

```neam
capability FileAccess {
  pattern: "file:*"
}

guard InputValidator {
  description: "Validates input size",
  handlers: [
    on_tool_input(input) -> Bool {
      if (input.length > 10000) { return false; }
      return true;
    }
  ]
}

guardchain SecurityChain {
  guards: [InputValidator, OutputSanitizer, RateLimiter]
}

tool ReadFile {
  description: "Reads a file from disk",
  capabilities: [FileAccess],
  guards: [InputValidator],
  params: [{ name: "path", type: String }],
  returns: String,
  impl: fun(path) {
    return file_read_string(path);
  }
}
```

---

## Budget and Resource Management

Enforce cost, time, and token limits on agents:

```neam
budget QuickTask {
  time: 5000,       // milliseconds
  cost: 0.10,       // dollars
  tokens: 5000
}

agent CheapBot {
  provider: "openai",
  model: "gpt-4o-mini",
  system: "Be brief and efficient.",
  budget: QuickTask
}
```

---

## Environment Configuration

```neam
env Production {
  API_URL: "https://api.prod.com",
  DEBUG: "false",
  API_KEY: env("PROD_API_KEY")
}

agent ProdAgent {
  provider: "openai",
  model: "gpt-4o",
  system: "Production agent",
  env: Production
}
```

---

## Memory, World Model, and Planning

```neam
memory ConversationMemory {
  backend: "redis",
  retention: "session",
  max_events: 10000
}

world_model TaskWorld {
  tier: 1,
  state_schema: "task_state_v1",
  update_frequency: 1000
}

plan HierarchicalPlanner {
  pattern: "hierarchical",
  max_depth: 5,
  backtrack: true,
  pruning: "alpha_beta"
}

agent StrategicAgent {
  provider: "openai",
  model: "gpt-4o",
  system: "Think strategically.",
  memory: ConversationMemory,
  world_model: TaskWorld,
  plan: HierarchicalPlanner
}
```

---

## Subagents and Connectors

```neam
subagent Worker {
  base_agent: "MainAgent",
  budget_share: 0.5,
  capability_inherit: true
}

connector SlackAPI {
  protocol: "http",
  endpoint: "https://slack.com/api",
  auth: env("SLACK_TOKEN")
}
```

---

## Built-in Functions

### Math (25+)

```neam
math_abs(-5);              // 5
math_floor(3.7);           // 3
math_ceil(3.2);            // 4
math_round(3.5);           // 4
math_min(5, 10);           // 5
math_max(5, 10);           // 10
math_clamp(15, 0, 10);     // 10
math_pow(2, 8);            // 256
math_sqrt(16);             // 4
math_sin(angle);           // Trig functions
math_random();             // 0.0 to 1.0
math_random_int(1, 100);   // 1 to 100
```

### JSON

```neam
let obj = json_parse('{"name": "Alice"}');
let text = json_stringify(obj);
```

### Time

```neam
let now = clock();
let ms = time_now();
time_sleep(1000);
let fmt = time_format(ms, "%Y-%m-%d");
```

### File I/O

```neam
let content = file_read_string("./data.txt");
file_write_string("./output.txt", "Hello");
let exists = file_exists("./file.txt");
file_copy("./src.txt", "./dst.txt");
```

### HTTP

```neam
let resp = http_get("https://api.example.com/data");
let resp = http_request("POST", "https://api.example.com", body, headers);
```

### Crypto

```neam
let hash = crypto_hash("sha256", "data");
let hmac = crypto_hmac("sha256", "secret", "message");
let uuid = crypto_uuid_v4();
let encoded = crypto_base64_encode("hello");
```

### Futures / Async

```neam
let resolved = future_resolve(42);
let all = future_all([f1, f2, f3]);
let first = future_race([f1, f2]);
let delayed = future_delay(1000);
```

---

## Testing

```neam
test "addition works" {
  assert_eq(add(2, 3), 5);
}

test "string operations" {
  let result = "Hello" + " " + "World";
  assert_eq(result, "Hello World");
}
```

| Function | Description |
|---|---|
| `assert_eq(a, b)` | Assert equality |
| `assert_ne(a, b)` | Assert inequality |
| `assert_true(cond)` | Assert truthy |
| `assert_false(cond)` | Assert falsy |
| `assert_throws(fn)` | Assert throws |

---

## Checkpoint and Rewind

Time-travel debugging for agent workflows:

```neam
checkpoint "safe_point";

let result = risky_operation();
if (!result) {
  rewind "safe_point";
}
```

---

## Module System

```neam
module my.app.utils;

import std.list;
import std.map;
import my.app.config as cfg;

pub fun public_helper() { }
fun internal_helper() { }
```

---

## Cloud Deployment

Neam compiles agents to native binaries and generates deployment artifacts for multiple cloud targets.

### Deployment Targets

| Target | Command | Use Case |
|---|---|---|
| Docker | `neam deploy --target docker` | Local / dev containers |
| Kubernetes | `neam deploy --target kubernetes` | Production clusters |
| Helm | `neam deploy --target helm` | K8s package management |
| AWS Lambda | `neam deploy --target aws-lambda` | Serverless (AWS) |
| GCP Cloud Run | `neam deploy --target gcp-cloudrun` | Serverless containers (GCP) |
| Azure Functions | `neam deploy --target azure-functions` | Serverless (Azure) |
| Terraform | `neam deploy --target terraform` | Infrastructure as Code |

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

Generates `deployment.yaml`, `service.yaml`, `configmap.yaml`, and `ingress.yaml` in `build/deploy/kubernetes/`.

### AWS Lambda

```bash
neam deploy --target aws-lambda --memory 1024 --timeout 30 --arch arm64
```

### GCP Cloud Run

```bash
neam deploy --target gcp-cloudrun --region us-central1 --concurrency 80 --cpu-boost
```

### Docker

```bash
neam deploy --target docker
docker build -t my-agent -f build/deploy/docker/Dockerfile .
docker run -e OPENAI_API_KEY=$OPENAI_API_KEY my-agent
```

### Generated Artifacts

```
build/deploy/
  kubernetes/
    deployment.yaml        # Deployment with replicas, resource limits, health probes
    service.yaml           # ClusterIP service
    configmap.yaml         # Environment configuration
    ingress.yaml           # Nginx ingress routing
  cloudrun/
    service.yaml           # GCP Cloud Run Knative service
  aws/
    template.yaml          # AWS SAM template
  helm/
    myapp/                 # Full Helm chart
  docker/
    Dockerfile
    docker-compose.yml
  terraform/
    main.tf
```

---

## Multi-Cloud Orchestration

Neam routes workloads across AWS, GCP, Azure, and Alibaba Cloud using real-time cost and latency data.

### Cost-Aware Routing

The built-in `CostAwareRouter` automatically selects the cheapest provider:

1. Polls spot prices across all 4 clouds
2. Evaluates latency from the current region
3. Enforces data residency constraints
4. Routes to the cheapest viable option
5. Fails over automatically after 3 consecutive errors (60s cooldown)

### Routing Constraints

| Constraint | Description |
|---|---|
| `max_latency` | Maximum acceptable latency (ms) |
| `max_cost` | Maximum cost per request ($) |
| `preferred_regions` | Preferred deployment regions |
| `preferred_providers` | Preferred cloud providers |
| `data_residency` | Required data residency (e.g., `"EU"`) |

### Cloud Provider Support

| Feature | AWS | GCP | Azure | Alibaba |
|---|---|---|---|---|
| Serverless | Lambda | Cloud Run | Functions | Function Compute |
| Containers | ECS Fargate | Cloud Run | Container Apps | ECI |
| VMs | EC2 | Compute Engine | Virtual Machines | ECS |
| GPU | g4dn / g5 / p4d | T4 / L4 / A100 | NC4 / NC24 | GN6i / GN6v / GN7i |
| Managed LLM | Bedrock | Vertex AI | Azure OpenAI | PAI |
| Spot Instances | Yes | Yes | Yes | Yes |
| Kubernetes | EKS | GKE | AKS | ACK |

---

## GPU and SIMD Acceleration

### GPU

Automatically accelerates embedding similarity, top-K retrieval, and batch operations when a GPU is available.

| Backend | Platform |
|---|---|
| CUDA | NVIDIA GPUs |
| Metal | Apple Silicon |
| OpenCL | Cross-platform |
| Vulkan | Cross-platform |
| CPU | Fallback (always available) |

```bash
neam deploy --target kubernetes --gpu nvidia-t4 --gpu-memory 16Gi --replicas 2
```

### SIMD

Auto-detected at compile time. 470+ operations accelerated transparently:

| ISA | Width | Platform |
|---|---|---|
| AVX-512 | 512-bit | x86 (Intel/AMD) |
| AVX2 | 256-bit | x86 (Intel/AMD) |
| SSE4 | 128-bit | x86 (Intel/AMD) |
| NEON | 128-bit | ARM (Apple Silicon, Graviton) |
| SVE | Scalable | ARM (server-class) |

Accelerated operations: vector dot product, cosine similarity, matrix multiply, ReLU/GELU/Softmax, float32-int8 quantization, substring search, JSON key search, batch normalization, and more.

---

## Auto-Scaling

ML-based predictive scaling with time-series forecasting:

- **Forecast windows:** 5 min, 1 hour, 24 hours
- **Pattern detection:** daily cycles, weekly cycles, anomaly spikes
- **Warm pool:** pre-warmed instances for zero cold-start scale-out
- **Spot optimization:** automatic spot vs on-demand switching across clouds

| Action | Trigger |
|---|---|
| Scale up | Load forecast exceeds capacity |
| Scale down | Sustained low utilization |
| Switch to spot | Spot price < 70% of on-demand |
| Switch to on-demand | Spot interruption notice |
| Switch to FaaS | Bursty, unpredictable traffic |

---

## FinOps

### Cost Attribution

Real-time cost tracking per agent, task, user, and project across 10 categories: compute, memory, storage, network, LLM API, embedding API, vector DB, state backend, external APIs, and miscellaneous.

```neam
budget TeamBudget {
  cost: 500.00,        // $500/day
  tokens: 10000000     // 10M tokens/day
}

// Built-in alerts:
//   Warning at 80% consumed
//   Critical at 95% consumed
```

### Dashboard

Real-time monitoring at `http://localhost:8080` with 14 widget types: cost over time, cost by category/agent/provider, budget gauges, top spenders, latency/throughput trends, CPU/memory/GPU utilization, error rate, alerts feed, and savings opportunities.

Features: WebSocket live updates, time range presets (1h to 30d), JSON/PDF export, dark mode, embeddable widgets.

### Continuous Benchmarking

Automated benchmarks with regression detection for CI/CD:

| Regression | Threshold |
|---|---|
| Latency increase | > 10% |
| Cost increase | > 5% |
| Throughput decrease | > 10% |

Outputs JUnit XML and GitHub Actions annotations.

---

## Cloud Testing

Run identical agents across all 4 cloud providers to compare performance and cost:

```bash
./tests/cloud/run_cloud_tests.sh                        # Full suite
./tests/cloud/run_cloud_tests.sh --provider aws          # Single provider
./tests/cloud/run_cloud_tests.sh --workload latency      # Single workload
./tests/cloud/run_cloud_tests.sh --dry-run               # Validate only
```

5 pre-configured workloads: latency (1K req), throughput (5K req), cost analysis (100 req), voice pipeline (50 req), GPU workload (200 req).

---

## Complete Examples

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
  system: "Answer questions based on the documentation.",
  connected_knowledge: [Docs]
}

let question = input();
let answer = DocAssistant.ask(question);
emit answer;
```

### Multi-Cloud Research System

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
  system: "Research each question. Provide detailed findings."
}

agent Synthesizer {
  provider: "ollama",
  model: "qwen2.5:14b",
  system: "Combine findings into a coherent report."
}

let topic = "Impact of quantum computing on cryptography";
let questions = Planner.ask("Create research plan for: " + topic);
let findings = Researcher.ask("Research these questions:\n" + questions);
let report = Synthesizer.ask("Write report from:\n" + findings);
emit report;
```

### Serverless Event Handler

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
let result = EventHandler.ask("Process this event: " + event);
emit result;
```

```bash
neam deploy --target aws-lambda --memory 512 --timeout 15 --arch arm64
```

### GPU-Accelerated Knowledge Agent

```neam
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
  retrieval_strategy: "agentic",
  top_k: 10,
  max_iterations: 5,
  enable_reflection: true
}

agent DeepExpert {
  provider: "bedrock",
  model: "anthropic.claude-3-5-sonnet-20241022-v2:0",
  system: "Use all available knowledge to give thorough answers.",
  connected_knowledge: [LargeCorpus]
}

let answer = DeepExpert.ask(input());
emit answer;
```

```bash
neam deploy --target kubernetes --gpu nvidia-t4 --gpu-memory 16Gi --replicas 2
```

### Production System with FinOps

```neam
budget TeamBudget {
  cost: 500.00,
  tokens: 10000000
}

env Monitored {
  FINOPS_DASHBOARD: "true",
  FINOPS_PORT: "8080",
  ALERT_WEBHOOK: env("SLACK_WEBHOOK")
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

let answer = ProductionAgent.ask(input());
emit answer;
```

---

## Environment Variables

| Variable | Default | Description |
|---|---|---|
| `NEAM_MAX_CALL_DEPTH` | 1000 | Max recursion depth (1 -- 100,000) |
| `NEAM_MAX_REACT_STEPS` | 100 | Max ReAct loop iterations (1 -- 10,000) |
| `NEAM_REPL_HISTORY` | 1000 | REPL history size (10 -- 100,000) |
| `OPENAI_API_KEY` | -- | OpenAI API key |
| `AWS_ACCESS_KEY_ID` | -- | AWS credentials for Bedrock |
| `AWS_SECRET_ACCESS_KEY` | -- | AWS credentials for Bedrock |
| `AWS_REGION` | us-east-1 | AWS region for Bedrock |

---

## Contributing

See [CONTRIBUTING.md](./CONTRIBUTING.md) for branching strategy and guidelines.

## License

See [LICENSE](./LICENSE).
