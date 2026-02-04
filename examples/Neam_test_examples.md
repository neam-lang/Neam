# Neam Test Examples - RAG Strategies & Agentic Patterns

This document contains comprehensive test examples for all RAG retrieval strategies and agentic orchestration patterns supported by Neam.

## Table of Contents

### Part 1: RAG Retrieval Strategies
1. [Basic Strategies](#basic-strategies)
   - [Basic RAG](#1-basic-rag)
   - [MMR RAG](#2-mmr-rag-maximal-marginal-relevance)
   - [Hybrid RAG](#3-hybrid-rag-keyword--vector)
2. [Advanced Strategies](#advanced-strategies)
   - [HyDE RAG](#4-hyde-rag-hypothetical-document-embeddings)
   - [Self-RAG](#5-self-rag-self-reflective)
   - [CRAG](#6-crag-corrective-rag)
   - [Agentic RAG](#7-agentic-rag)
3. [Strategy Options Reference](#strategy-options-reference)
4. [Complete RAG Test Files](#complete-test-files)

### Part 2: Agentic Patterns
5. [Agentic Orchestration Patterns](#agentic-orchestration-patterns)
   - [Pattern 1: Single Agent](#pattern-1-single-agent)
   - [Pattern 2: Multi-Agent Collaboration](#pattern-2-multi-agent-collaboration)
   - [Pattern 3: Sequential Pipeline](#pattern-3-sequential-pipeline)
   - [Pattern 4: Supervisor/Worker](#pattern-4-supervisorworker)
   - [Pattern 5: Router/Dispatcher](#pattern-5-routerdispatcher)
   - [Pattern 6: Debate/Adversarial](#pattern-6-debateadversarial)
   - [Pattern 7: Expert Retrieval Agent](#pattern-7-expert-retrieval-agent)
   - [Pattern 8: Research + RAG Pipeline](#pattern-8-research--rag-pipeline)
   - [Pattern 9: QA Validator with RAG](#pattern-9-qa-validator-with-rag)
   - [Pattern 10: Multi-KB Routing](#pattern-10-multi-kb-routing)

### Part 3: Native API Server
6. [Native REST API Server](#rest-api-server-neam-api)
   - [Starting the Server](#starting-the-server)
   - [API Endpoints](#api-endpoints)
   - [Testing with curl](#testing-with-curl)

---

## Basic Strategies

### 1. Basic RAG

Standard vector similarity search - the default retrieval strategy.

```neam
knowledge BasicKB {
  vector_store: "usearch"
  embedding_model: "nomic-embed-text"
  chunk_size: 200
  chunk_overlap: 50
  sources: [
    { "type": "file", "path": "./readme.md" }
  ]
  retrieval_strategy: "basic"
  top_k: 3
}

agent BasicExpert {
  provider: "ollama"
  model: "qwen3:1.7b"
  system: "You are a helpful assistant. Answer based on the provided context."
  connected_knowledge: [BasicKB]
}

{
  emit "--- Basic RAG Test ---";
  let answer = BasicExpert.ask("What is Neam?");
  emit "Answer: " + answer;
}
```

### 2. MMR RAG (Maximal Marginal Relevance)

Diversity-focused retrieval that balances relevance with result diversity.

```neam
knowledge MMRKB {
  vector_store: "usearch"
  embedding_model: "nomic-embed-text"
  chunk_size: 200
  chunk_overlap: 50
  sources: [
    { "type": "file", "path": "./readme.md" }
  ]
  retrieval_strategy: "mmr"
  top_k: 5
  mmr_lambda: 0.7  // 0.0 = max diversity, 1.0 = max relevance
}

agent MMRExpert {
  provider: "ollama"
  model: "qwen3:1.7b"
  system: "You are a helpful assistant. Answer based on the provided context."
  connected_knowledge: [MMRKB]
}

{
  emit "--- MMR RAG Test (Diversity-focused) ---";
  let answer = MMRExpert.ask("Explain the main features of Neam");
  emit "Answer: " + answer;
}
```

### 3. Hybrid RAG (Keyword + Vector)

Combines traditional keyword search with vector similarity for better precision.

```neam
knowledge HybridKB {
  vector_store: "usearch"
  embedding_model: "nomic-embed-text"
  chunk_size: 200
  chunk_overlap: 50
  sources: [
    { "type": "file", "path": "./readme.md" },
    { "type": "file", "path": "./docs/*.md" }
  ]
  retrieval_strategy: "hybrid"
  top_k: 4
}

agent HybridExpert {
  provider: "ollama"
  model: "qwen3:1.7b"
  system: "You are a technical expert. Be precise and accurate."
  connected_knowledge: [HybridKB]
}

{
  emit "--- Hybrid RAG Test (Keyword + Vector) ---";
  let answer = HybridExpert.ask("How do knowledge blocks work?");
  emit "Answer: " + answer;
}
```

---

## Advanced Strategies

### 4. HyDE RAG (Hypothetical Document Embeddings)

Generates hypothetical answer documents first, then uses them to find similar real documents.

```neam
knowledge HyDEKB {
  vector_store: "usearch"
  embedding_model: "nomic-embed-text"
  chunk_size: 200
  chunk_overlap: 50
  sources: [
    { "type": "file", "path": "./readme.md" }
  ]
  retrieval_strategy: "hyde"
  top_k: 3
  num_hypothetical: 1  // Number of hypothetical docs to generate
}

agent HyDEExpert {
  provider: "ollama"
  model: "qwen3:1.7b"
  system: "You are a helpful assistant. Answer based on the provided context."
  connected_knowledge: [HyDEKB]
}

{
  emit "--- HyDE RAG Test ---";
  emit "Strategy: Generate hypothetical answer, then find similar docs";
  let answer = HyDEExpert.ask("What programming paradigm does Neam follow?");
  emit "Answer: " + answer;
}
```

### 5. Self-RAG (Self-Reflective)

Retrieves documents and checks their relevance before using them.

```neam
knowledge SelfRAGKB {
  vector_store: "usearch"
  embedding_model: "nomic-embed-text"
  chunk_size: 200
  chunk_overlap: 50
  sources: [
    { "type": "file", "path": "./readme.md" }
  ]
  retrieval_strategy: "self_rag"
  top_k: 5
  enable_relevance_check: true   // Check if docs are relevant
  enable_support_check: true     // Verify answer is grounded in context
}

agent SelfRAGExpert {
  provider: "ollama"
  model: "qwen3:1.7b"
  system: "You are a careful assistant. Only use information from the context."
  connected_knowledge: [SelfRAGKB]
}

{
  emit "--- Self-RAG Test (Relevance Checking) ---";
  let answer = SelfRAGExpert.ask("How do agents communicate in Neam?");
  emit "Answer: " + answer;
}
```

### 6. CRAG (Corrective RAG)

Decomposes complex queries and corrects retrieval when results are insufficient.

```neam
knowledge CRAGKB {
  vector_store: "usearch"
  embedding_model: "nomic-embed-text"
  chunk_size: 200
  chunk_overlap: 50
  sources: [
    { "type": "file", "path": "./readme.md" },
    { "type": "file", "path": "./docs/registry_spec.md" }
  ]
  retrieval_strategy: "crag"
  top_k: 4
  enable_query_decomposition: true  // Break complex queries into sub-queries
  max_corrections: 2                // Maximum correction attempts
  relevance_threshold: 0.5          // Threshold to trigger correction
}

agent CRAGExpert {
  provider: "ollama"
  model: "qwen3:1.7b"
  system: "You are a thorough researcher. Provide comprehensive answers."
  connected_knowledge: [CRAGKB]
}

{
  emit "--- CRAG Test (Query Decomposition) ---";
  let complex_question = "What are the steps to build Neam and what artifacts are produced?";
  let answer = CRAGExpert.ask(complex_question);
  emit "Answer: " + answer;
}
```

### 7. Agentic RAG

Iterative retrieval with reflection - refines queries until sufficient information is found.

```neam
knowledge AgenticKB {
  vector_store: "usearch"
  embedding_model: "nomic-embed-text"
  chunk_size: 200
  chunk_overlap: 50
  sources: [
    { "type": "file", "path": "./readme.md" }
  ]
  retrieval_strategy: "agentic"
  top_k: 3
  max_iterations: 3       // Maximum refinement iterations
  enable_reflection: true // Enable self-reflection on retrieved content
}

agent AgenticExpert {
  provider: "ollama"
  model: "qwen3:1.7b"
  system: "You are an intelligent research assistant."
  connected_knowledge: [AgenticKB]
}

{
  emit "--- Agentic RAG Test (Iterative Refinement) ---";
  let answer = AgenticExpert.ask("Explain how Neam's VM executes agent tasks");
  emit "Answer: " + answer;
}
```

---

## Strategy Options Reference

### Common Options (All Strategies)

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `top_k` | number | 4 | Number of documents to retrieve |
| `relevance_threshold` | number | 0.5 | Minimum relevance score (0.0-1.0) |

### MMR-Specific Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `mmr_lambda` | number | 0.5 | Balance between relevance (1.0) and diversity (0.0) |

### HyDE-Specific Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `num_hypothetical` | number | 1 | Number of hypothetical documents to generate |

### Self-RAG-Specific Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `enable_relevance_check` | boolean | true | Check document relevance before use |
| `enable_support_check` | boolean | true | Verify answer is grounded in context |

### CRAG-Specific Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `enable_query_decomposition` | boolean | true | Break complex queries into sub-queries |
| `enable_web_fallback` | boolean | false | Fall back to web search if retrieval fails |
| `max_corrections` | number | 2 | Maximum correction attempts |

### Agentic-Specific Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `max_iterations` | number | 5 | Maximum refinement iterations |
| `enable_reflection` | boolean | true | Enable self-reflection on progress |

### Graph RAG-Specific Options

| Option | Type | Default | Description |
|--------|------|---------|-------------|
| `search_depth` | number | 2 | Graph traversal depth |
| `include_communities` | boolean | true | Include community summaries |

---

## Complete Test Files

### Test All Basic Strategies

```neam
// File: test_basic_strategies.neam
// Tests: Basic, MMR, Hybrid

knowledge BasicKB {
  vector_store: "usearch"
  embedding_model: "nomic-embed-text"
  chunk_size: 200
  chunk_overlap: 50
  sources: [
    { "type": "file", "path": "./readme.md" }
  ]
  retrieval_strategy: "basic"
  top_k: 3
}

knowledge MMRKB {
  vector_store: "usearch"
  embedding_model: "nomic-embed-text"
  chunk_size: 200
  chunk_overlap: 50
  sources: [
    { "type": "file", "path": "./readme.md" }
  ]
  retrieval_strategy: "mmr"
  top_k: 3
  mmr_lambda: 0.7
}

knowledge HybridKB {
  vector_store: "usearch"
  embedding_model: "nomic-embed-text"
  chunk_size: 200
  chunk_overlap: 50
  sources: [
    { "type": "file", "path": "./readme.md" }
  ]
  retrieval_strategy: "hybrid"
  top_k: 3
}

agent BasicExpert {
  provider: "ollama"
  model: "qwen3:1.7b"
  system: "Be concise."
  connected_knowledge: [BasicKB]
}

agent MMRExpert {
  provider: "ollama"
  model: "qwen3:1.7b"
  system: "Be concise."
  connected_knowledge: [MMRKB]
}

agent HybridExpert {
  provider: "ollama"
  model: "qwen3:1.7b"
  system: "Be concise."
  connected_knowledge: [HybridKB]
}

{
  emit "=== Testing Basic RAG Strategies ===";
  emit "";

  let q = "What is Neam?";

  emit "1. Basic Strategy:";
  emit BasicExpert.ask(q);
  emit "";

  emit "2. MMR Strategy:";
  emit MMRExpert.ask(q);
  emit "";

  emit "3. Hybrid Strategy:";
  emit HybridExpert.ask(q);
  emit "";

  emit "=== Test Complete ===";
}
```

### Test All Advanced Strategies

```neam
// File: test_advanced_strategies.neam
// Tests: HyDE, Self-RAG, CRAG, Agentic

knowledge HyDEKB {
  vector_store: "usearch"
  embedding_model: "nomic-embed-text"
  chunk_size: 200
  chunk_overlap: 50
  sources: [
    { "type": "file", "path": "./readme.md" }
  ]
  retrieval_strategy: "hyde"
  top_k: 3
  num_hypothetical: 1
}

knowledge SelfRAGKB {
  vector_store: "usearch"
  embedding_model: "nomic-embed-text"
  chunk_size: 200
  chunk_overlap: 50
  sources: [
    { "type": "file", "path": "./readme.md" }
  ]
  retrieval_strategy: "self_rag"
  top_k: 3
  enable_relevance_check: true
}

knowledge CRAGKB {
  vector_store: "usearch"
  embedding_model: "nomic-embed-text"
  chunk_size: 200
  chunk_overlap: 50
  sources: [
    { "type": "file", "path": "./readme.md" }
  ]
  retrieval_strategy: "crag"
  top_k: 3
  enable_query_decomposition: true
}

knowledge AgenticKB {
  vector_store: "usearch"
  embedding_model: "nomic-embed-text"
  chunk_size: 200
  chunk_overlap: 50
  sources: [
    { "type": "file", "path": "./readme.md" }
  ]
  retrieval_strategy: "agentic"
  top_k: 3
  max_iterations: 2
  enable_reflection: true
}

agent HyDEExpert {
  provider: "ollama"
  model: "qwen3:1.7b"
  system: "Be concise. Answer in 1-2 sentences."
  connected_knowledge: [HyDEKB]
}

agent SelfRAGExpert {
  provider: "ollama"
  model: "qwen3:1.7b"
  system: "Be concise. Answer in 1-2 sentences."
  connected_knowledge: [SelfRAGKB]
}

agent CRAGExpert {
  provider: "ollama"
  model: "qwen3:1.7b"
  system: "Be concise. Answer in 1-2 sentences."
  connected_knowledge: [CRAGKB]
}

agent AgenticExpert {
  provider: "ollama"
  model: "qwen3:1.7b"
  system: "Be concise. Answer in 1-2 sentences."
  connected_knowledge: [AgenticKB]
}

{
  emit "=== Testing Advanced RAG Strategies ===";
  emit "";

  let q = "What is Neam?";

  emit "1. HyDE (Hypothetical Document):";
  emit HyDEExpert.ask(q);
  emit "";

  emit "2. Self-RAG (Relevance Check):";
  emit SelfRAGExpert.ask(q);
  emit "";

  emit "3. CRAG (Query Decomposition):";
  emit CRAGExpert.ask(q);
  emit "";

  emit "4. Agentic (Iterative):";
  emit AgenticExpert.ask(q);
  emit "";

  emit "=== Test Complete ===";
}
```

### Multi-Source RAG Test

```neam
// File: test_multi_source_rag.neam
// Tests retrieval from multiple sources with different types

knowledge MultiSourceKB {
  vector_store: "usearch"
  embedding_model: "nomic-embed-text"
  chunk_size: 256
  chunk_overlap: 64
  sources: [
    { "type": "file", "path": "./readme.md" },
    { "type": "file", "path": "./docs/*.md" },
    { "type": "web", "path": "https://raw.githubusercontent.com/anthropics/anthropic-cookbook/main/README.md" }
  ]
  retrieval_strategy: "mmr"
  top_k: 5
  mmr_lambda: 0.6
}

agent MultiSourceExpert {
  provider: "ollama"
  model: "qwen3:1.7b"
  system: "You are a technical expert with access to multiple sources. Synthesize information from all available sources."
  connected_knowledge: [MultiSourceKB]
}

{
  emit "=== Multi-Source RAG Test ===";
  emit "Sources: Local files + Web content";
  emit "";

  emit "Q: Compare Neam with other AI frameworks";
  let answer = MultiSourceExpert.ask("Compare Neam with other AI frameworks");
  emit "A: " + answer;
  emit "";

  emit "=== Test Complete ===";
}
```

---

## Running Tests

### Compile and Run

```bash
# Compile
./build/neamc examples/test_basic_strategies.neam -o /tmp/test.neamb

# Run
./build/neam /tmp/test.neamb
```

### Expected Output

When running the tests, you should see output like:

```
[RAG] Searching KB 'BasicKB' with strategy 'basic' for: 'What is Neam?'
[RAG] Found 3 documents via basic
[RAG] Searching KB 'MMRKB' with strategy 'mmr' for: 'What is Neam?'
[RAG] Found 3 documents via mmr
[RAG] Searching KB 'HyDEKB' with strategy 'hyde' for: 'What is Neam?'
[RAG] Found 3 documents via hyde
[RAG] HyDE generated 1 hypothetical doc(s)
```

---

## Notes

1. **Ollama Required**: These examples use Ollama with the `qwen3:1.7b` model. Ensure Ollama is running:
   ```bash
   ollama serve
   ollama pull qwen3:1.7b
   ollama pull nomic-embed-text
   ```

2. **Source Paths**: Update the `path` values in `sources` to match your actual file locations.

3. **Strategy Selection**: Choose strategies based on your use case:
   - **Basic**: Simple, fast retrieval
   - **MMR**: When you need diverse results
   - **Hybrid**: When exact keyword matches matter
   - **HyDE**: For abstract or conceptual queries
   - **Self-RAG**: When accuracy is critical
   - **CRAG**: For complex multi-part questions
   - **Agentic**: For research-style iterative exploration

---

# Part 2: Agentic Orchestration Patterns

## Agentic Orchestration Patterns

Neam supports various multi-agent orchestration patterns for complex AI workflows.

### Pattern 1: Single Agent

The simplest pattern - a single agent handles all queries.

```neam
agent SimpleAssistant {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You are a helpful assistant. Be concise."
}

{
  let answer = SimpleAssistant.ask("What is the capital of France?");
  emit answer;
}
```

### Pattern 2: Multi-Agent Collaboration

Specialized agents work on different aspects of a task.

```neam
agent Researcher {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You are a researcher. Provide factual information and key points."
}

agent Writer {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You are a writer. Take research notes and create polished prose."
}

agent Editor {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You are an editor. Review and improve text for clarity."
}

{
  emit "--- Multi-Agent Collaboration ---";

  // Step 1: Research
  let research = Researcher.ask("Provide 3 key facts about artificial intelligence");
  emit "Research: " + research;

  // Step 2: Write based on research
  let draft = Writer.ask("Write a paragraph based on: " + research);
  emit "Draft: " + draft;

  // Step 3: Edit the draft
  let final = Editor.ask("Edit and improve: " + draft);
  emit "Final: " + final;
}
```

### Pattern 3: Sequential Pipeline

Output of one agent feeds into the next.

```neam
agent Translator {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "Translate the given text to French. Output only the translation."
}

agent Summarizer {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "Summarize the given text in exactly one sentence."
}

{
  let original = "The quick brown fox jumps over the lazy dog.";
  emit "Original: " + original;

  let translated = Translator.ask(original);
  emit "French: " + translated;

  let summary = Summarizer.ask(translated);
  emit "Summary: " + summary;
}
```

### Pattern 4: Supervisor/Worker

Supervisor delegates tasks and validates results.

```neam
agent Supervisor {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You are a supervisor. Evaluate if the worker's response is complete. Reply with 'APPROVED' or 'NEEDS_REVISION: <reason>'."
}

agent Worker {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You are a worker. Complete the assigned task thoroughly."
}

{
  let task = "List 3 benefits of exercise";
  emit "Task: " + task;

  // Worker attempts the task
  let result = Worker.ask(task);
  emit "Worker: " + result;

  // Supervisor validates
  let validation = Supervisor.ask("Evaluate this response to '" + task + "': " + result);
  emit "Supervisor: " + validation;
}
```

### Pattern 5: Router/Dispatcher

Routes queries to specialized agents based on classification.

```neam
agent Router {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "Classify the query: MATH, CODE, or GENERAL. Reply with only the category."
}

agent MathExpert {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You are a math expert. Solve math problems step by step."
}

agent CodeExpert {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You are a coding expert. Provide code solutions."
}

agent GeneralExpert {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You are a general knowledge expert."
}

{
  let query = "What is 15 * 23?";
  emit "Query: " + query;

  let route = Router.ask(query);
  emit "Route: " + route;

  // Route to appropriate expert (simplified - full version would use conditionals)
  let answer = MathExpert.ask(query);
  emit "Answer: " + answer;
}
```

### Pattern 6: Debate/Adversarial

Multiple agents present different perspectives.

```neam
agent Advocate {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You argue IN FAVOR of the given topic. Present 2-3 strong points."
}

agent Critic {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You argue AGAINST the given topic. Present 2-3 counterpoints."
}

agent Judge {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You are an impartial judge. Given pro and con arguments, provide a balanced conclusion."
}

{
  let topic = "Remote work should be the default for knowledge workers";
  emit "Topic: " + topic;

  let pro = Advocate.ask(topic);
  emit "PRO: " + pro;

  let con = Critic.ask(topic);
  emit "CON: " + con;

  let verdict = Judge.ask("PRO: " + pro + " --- CON: " + con);
  emit "VERDICT: " + verdict;
}
```

---

## RAG-Enhanced Agentic Patterns

### Pattern 7: Expert Retrieval Agent

Agent with connected knowledge base for domain expertise.

```neam
knowledge DomainKB {
  vector_store: "usearch"
  embedding_model: "nomic-embed-text"
  chunk_size: 200
  chunk_overlap: 50
  sources: [ { "type": "file", "path": "./docs/*.md" } ]
  retrieval_strategy: "basic"
  top_k: 3
}

agent DomainExpert {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You are a domain expert. Use the provided documentation to answer accurately."
  connected_knowledge: [DomainKB]
}

{
  let answer = DomainExpert.ask("How does this system work?");
  emit answer;
}
```

### Pattern 8: Research + RAG Pipeline

RAG-enhanced researcher feeds into an analyst.

```neam
knowledge ResearchKB {
  vector_store: "usearch"
  embedding_model: "nomic-embed-text"
  chunk_size: 200
  chunk_overlap: 50
  sources: [ { "type": "file", "path": "./readme.md" } ]
  retrieval_strategy: "hyde"
  top_k: 4
}

agent RAGResearcher {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "Extract and summarize relevant facts from the documentation."
  connected_knowledge: [ResearchKB]
}

agent Analyst {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "Provide insights and recommendations based on research."
}

{
  let research = RAGResearcher.ask("Explain the main features");
  emit "Research: " + research;

  let analysis = Analyst.ask("Analyze this: " + research);
  emit "Analysis: " + analysis;
}
```

### Pattern 9: QA Validator with RAG

Answer questions then validate against documentation.

```neam
knowledge ValidatorKB {
  vector_store: "usearch"
  embedding_model: "nomic-embed-text"
  chunk_size: 200
  chunk_overlap: 50
  sources: [ { "type": "file", "path": "./readme.md" } ]
  retrieval_strategy: "basic"
  top_k: 3
}

agent Responder {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "Answer questions based on the documentation."
  connected_knowledge: [ValidatorKB]
}

agent FactChecker {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "Verify if the answer is accurate. Reply: VERIFIED, PARTIALLY_CORRECT, or INCORRECT."
  connected_knowledge: [ValidatorKB]
}

{
  let question = "What vector store is used?";

  let answer = Responder.ask(question);
  emit "Answer: " + answer;

  let check = FactChecker.ask("Question: '" + question + "' Answer: '" + answer + "' - Verify.");
  emit "Verification: " + check;
}
```

### Pattern 10: Multi-KB Routing

Route queries to specialized knowledge bases.

```neam
knowledge SyntaxKB {
  vector_store: "usearch"
  embedding_model: "nomic-embed-text"
  chunk_size: 200
  chunk_overlap: 50
  sources: [ { "type": "file", "path": "./examples/*.neam" } ]
  retrieval_strategy: "basic"
  top_k: 3
}

knowledge ConceptKB {
  vector_store: "usearch"
  embedding_model: "nomic-embed-text"
  chunk_size: 200
  chunk_overlap: 50
  sources: [ { "type": "file", "path": "./docs/*.md" } ]
  retrieval_strategy: "hyde"
  top_k: 3
}

agent KBRouter {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "Classify: SYNTAX (code) or CONCEPT (architecture). Reply with only the category."
}

agent SyntaxExpert {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "Explain syntax and provide code examples."
  connected_knowledge: [SyntaxKB]
}

agent ConceptExpert {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "Explain concepts and architecture."
  connected_knowledge: [ConceptKB]
}

{
  let query = "Show me an agent declaration example";

  let route = KBRouter.ask(query);
  emit "Route: " + route;

  let answer = SyntaxExpert.ask(query);
  emit "Answer: " + answer;
}
```

---

## Pattern Selection Guide

| Pattern | Use Case | Complexity |
|---------|----------|------------|
| Single Agent | Simple Q&A | Low |
| Multi-Agent Collaboration | Content creation workflow | Medium |
| Sequential Pipeline | Data transformation | Low |
| Supervisor/Worker | Quality-critical tasks | Medium |
| Router/Dispatcher | Multi-domain queries | Medium |
| Debate/Adversarial | Decision making, analysis | Medium |
| Expert Retrieval | Domain-specific Q&A | Low |
| Research + RAG | Deep research tasks | High |
| QA Validator | Fact-checking workflows | Medium |
| Multi-KB Routing | Complex knowledge systems | High |

---

## Test Files Reference

| File | Description |
|------|-------------|
| `rag_basic_strategies.neam` | Basic, MMR, Hybrid RAG tests |
| `rag_advanced_strategies.neam` | HyDE, Self-RAG, CRAG, Agentic tests |
| `rag_multi_source.neam` | File + Web source tests |
| `rag_all_strategies.neam` | All 7 strategies comparison |
| `agentic_patterns_openai.neam` | All orchestration patterns |
| `agentic_rag_patterns.neam` | RAG-enhanced agentic patterns |

---

## Running with Different Providers

### Ollama (Local)
```bash
ollama serve
ollama pull qwen3:1.7b
ollama pull nomic-embed-text

./build/neamc program.neam -o /tmp/out.neamb && ./build/neam /tmp/out.neamb
```

### OpenAI
```bash
export OPENAI_API_KEY="your-key"
./build/neamc program.neam -o /tmp/out.neamb && ./build/neam /tmp/out.neamb
```

Change `provider: "ollama"` to `provider: "openai"` and update the model name accordingly

---

# Part 3: Native REST API Server

## REST API Server (neam-api)

Expose Neam agents as REST API endpoints using the native C++ `neam-api` server.

### Building the Server

```bash
# From the build directory
cmake --build . --target neam-api --parallel

# Verify build
./neam-api --help
```

### Starting the Server

```bash
# Set OpenAI API key
export OPENAI_API_KEY="your-key"

# Start the native server
./neam-api --port 8080
```

**Output:**
```
Starting Neam API Server...
  Host: 0.0.0.0
  Port: 8080
  Endpoints:
    GET  /api/v1/health
    GET  /api/v1/agents
    POST /api/v1/agent/ask

Neam API Server running on http://0.0.0.0:8080
```

### API Endpoints

| Endpoint | Method | Description |
|----------|--------|-------------|
| `/api/v1/health` | GET | Health check |
| `/api/v1/agents` | GET | List available agents |
| `/api/v1/agent/ask` | POST | Query an agent |

### Available Agents

| Agent ID | Name | Description | RAG |
|----------|------|-------------|-----|
| `assistant` | Assistant | General purpose helpful assistant | No |
| `coder` | Coder | Expert programmer, code solutions | No |
| `analyst` | Analyst | Data analysis and insights | No |
| `writer` | Writer | Creative writing | No |
| `researcher` | Researcher | Research with knowledge base | Yes |

### Testing with curl

#### Health Check
```bash
curl http://localhost:8080/api/v1/health
```

**Response:**
```json
{
  "status": "healthy",
  "version": "1.0.0"
}
```

#### List Agents
```bash
curl http://localhost:8080/api/v1/agents
```

**Response:**
```json
{
  "agents": {
    "assistant": {
      "name": "Assistant",
      "provider": "openai",
      "model": "gpt-4o-mini",
      "has_knowledge_base": false
    },
    "coder": {...},
    "researcher": {...}
  }
}
```

#### Query Assistant Agent
```bash
curl -X POST http://localhost:8080/api/v1/agent/ask \
  -H "Content-Type: application/json" \
  -d '{"agent_id": "assistant", "query": "What is 2+2?"}'
```

**Response:**
```json
{
  "agent_id": "assistant",
  "query": "What is 2+2?",
  "response": "2 + 2 equals 4."
}
```

#### Query Coder Agent
```bash
curl -X POST http://localhost:8080/api/v1/agent/ask \
  -H "Content-Type: application/json" \
  -d '{"agent_id": "coder", "query": "Write a Python function to check if a number is prime"}'
```

#### Query Researcher Agent (RAG-enabled)
```bash
curl -X POST http://localhost:8080/api/v1/agent/ask \
  -H "Content-Type: application/json" \
  -d '{"agent_id": "researcher", "query": "What is Neam?"}'
```

#### Query Writer Agent
```bash
curl -X POST http://localhost:8080/api/v1/agent/ask \
  -H "Content-Type: application/json" \
  -d '{"agent_id": "writer", "query": "Write a haiku about programming"}'
```

### Integration Examples

#### Python
```python
import requests

response = requests.post(
    "http://localhost:8080/api/v1/agent/ask",
    json={"agent_id": "assistant", "query": "Hello!"}
)
print(response.json()["response"])
```

#### JavaScript
```javascript
const response = await fetch("http://localhost:8080/api/v1/agent/ask", {
    method: "POST",
    headers: {"Content-Type": "application/json"},
    body: JSON.stringify({agent_id: "assistant", query: "Hello!"})
});
const data = await response.json();
console.log(data.response);
```

#### Shell Script
```bash
#!/bin/bash
QUERY="$1"
curl -s -X POST http://localhost:8080/api/v1/agent/ask \
  -H "Content-Type: application/json" \
  -d "{\"agent_id\": \"assistant\", \"query\": \"$QUERY\"}" | jq -r '.response'
```

### Stop the Server
```bash
pkill -f neam-api
```

### Server Files

| File | Description |
|------|-------------|
| `build/neam-api` | Native API server executable |
| `NeamC/src/neam_api.cpp` | API server source code |
| `NeamC/src/api/http_server.cpp` | HTTP server implementation |
| `NeamC/include/neamc/api/http_server.hpp` | HTTP server header |
| `examples/api_server/README.md` | Full API documentation |

---

## Complete File Reference

| Category | File | Description |
|----------|------|-------------|
| **RAG** | `rag_basic_strategies.neam` | Basic, MMR, Hybrid |
| **RAG** | `rag_advanced_strategies.neam` | HyDE, Self-RAG, CRAG, Agentic |
| **RAG** | `rag_all_strategies.neam` | All 7 strategies |
| **RAG** | `rag_multi_source.neam` | File + Web sources |
| **Agents** | `agentic_patterns_openai.neam` | 6 orchestration patterns |
| **Agents** | `agentic_rag_patterns.neam` | RAG-enhanced patterns |
| **Agents** | `special_agents_openai.neam` | 8 special patterns |
| **API** | `build/neam-api` | Native REST API server |
| **Docs** | `Neam_test_examples.md` | This file |
| **Docs** | `Agentic_Patterns_readme.md` | Patterns guide |
| **Docs** | `api_server/README.md` | API documentation |

---

*Last updated: January 2026*
