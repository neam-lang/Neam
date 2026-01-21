# Neam Agentic Patterns Guide

Comprehensive guide to multi-agent orchestration patterns in Neam, tested with OpenAI GPT-4o-mini.

## Table of Contents

- [Overview](#overview)
- [Basic Patterns](#basic-patterns)
  - [Pattern 1: Single Agent](#pattern-1-single-agent)
  - [Pattern 2: Multi-Agent Collaboration](#pattern-2-multi-agent-collaboration)
  - [Pattern 3: Sequential Pipeline](#pattern-3-sequential-pipeline)
  - [Pattern 4: Supervisor/Worker](#pattern-4-supervisorworker)
  - [Pattern 5: Router/Dispatcher](#pattern-5-routerdispatcher)
  - [Pattern 6: Debate/Adversarial](#pattern-6-debateadversarial)
- [RAG-Enhanced Patterns](#rag-enhanced-patterns)
  - [Pattern 7: Expert Retrieval Agent](#pattern-7-expert-retrieval-agent)
  - [Pattern 8: Research + RAG Pipeline](#pattern-8-research--rag-pipeline)
  - [Pattern 9: QA Validator with RAG](#pattern-9-qa-validator-with-rag)
  - [Pattern 10: Multi-KB Routing](#pattern-10-multi-kb-routing)
- [Pattern Selection Guide](#pattern-selection-guide)
- [Running the Examples](#running-the-examples)

---

## Overview

Neam supports sophisticated multi-agent orchestration patterns that enable:

- **Task decomposition** - Breaking complex tasks into specialized subtasks
- **Agent collaboration** - Multiple agents working together
- **Quality validation** - Supervisor agents checking work
- **Intelligent routing** - Directing queries to appropriate experts
- **Knowledge augmentation** - Enhancing agents with RAG capabilities

### Providers Supported

| Provider | Model Example | Use Case |
|----------|---------------|----------|
| OpenAI | `gpt-4o-mini`, `gpt-4o` | Production, high quality |
| Ollama | `qwen3:1.7b`, `llama3` | Local development, privacy |

---

## Basic Patterns

### Pattern 1: Single Agent

The simplest pattern - one agent handles all queries directly.

**Use Case:** Simple Q&A, chatbots, basic assistance

```neam
agent SimpleAssistant {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You are a helpful assistant. Be concise, answer in 1-2 sentences."
}

{
  emit "--- Single Agent Pattern ---";
  let answer = SimpleAssistant.ask("What is the capital of France?");
  emit "Answer: " + answer;
}
```

**Expected Output:**
```
--- Single Agent Pattern ---
Answer: The capital of France is Paris.
```

---

### Pattern 2: Multi-Agent Collaboration

Specialized agents work on different aspects of a task, each contributing their expertise.

**Use Case:** Content creation, report generation, complex analysis

```neam
agent Researcher {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You are a researcher. Provide factual information and key points. Be concise."
}

agent Writer {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You are a writer. Take research notes and create polished prose. Be concise."
}

agent Editor {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You are an editor. Review and improve text for clarity. Output only the improved text."
}

{
  emit "--- Multi-Agent Collaboration ---";
  emit "Topic: Artificial Intelligence";

  // Step 1: Research phase
  let research = Researcher.ask("Provide 3 key facts about artificial intelligence");
  emit "Researcher: " + research;

  // Step 2: Writing phase (uses research output)
  let draft = Writer.ask("Write a short paragraph based on these notes: " + research);
  emit "Writer: " + draft;

  // Step 3: Editing phase (uses writer output)
  let final_text = Editor.ask("Edit and improve this text: " + draft);
  emit "Editor: " + final_text;
}
```

**Flow Diagram:**
```
[Researcher] → research notes → [Writer] → draft → [Editor] → final text
```

---

### Pattern 3: Sequential Pipeline

Output of one agent feeds directly into the next, like a data transformation pipeline.

**Use Case:** Translation workflows, data transformation, multi-step processing

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
  emit "--- Sequential Pipeline ---";

  let original = "The quick brown fox jumps over the lazy dog.";
  emit "Original: " + original;

  // Stage 1: Translate
  let translated = Translator.ask(original);
  emit "Translated (French): " + translated;

  // Stage 2: Summarize the translation
  let summary = Summarizer.ask("Summarize this French text: " + translated);
  emit "Summary: " + summary;
}
```

**Flow Diagram:**
```
[Input] → [Translator] → French text → [Summarizer] → Summary
```

---

### Pattern 4: Supervisor/Worker

A supervisor agent delegates tasks and validates the worker's output.

**Use Case:** Quality assurance, task validation, automated review

```neam
agent Supervisor {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You are a supervisor. Evaluate if the worker's response is complete and correct. Reply with 'APPROVED' or 'NEEDS_REVISION: <reason>'."
}

agent Worker {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You are a worker. Complete the assigned task thoroughly but concisely."
}

{
  emit "--- Supervisor/Worker Pattern ---";

  let task = "List 3 benefits of exercise";
  emit "Task: " + task;

  // Worker attempts the task
  let work_result = Worker.ask(task);
  emit "Worker Output: " + work_result;

  // Supervisor validates the work
  let validation = Supervisor.ask("Evaluate this response to '" + task + "': " + work_result);
  emit "Supervisor: " + validation;
}
```

**Flow Diagram:**
```
[Task] → [Worker] → result → [Supervisor] → APPROVED / NEEDS_REVISION
```

---

### Pattern 5: Router/Dispatcher

A router agent classifies queries and dispatches them to specialized experts.

**Use Case:** Multi-domain support, customer service, specialized Q&A systems

```neam
agent Router {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "Classify the query into one category: MATH, CODE, or GENERAL. Reply with only the category name."
}

agent MathExpert {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You are a math expert. Solve math problems step by step. Be concise."
}

agent CodeExpert {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You are a coding expert. Provide code solutions with brief explanations."
}

agent GeneralExpert {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You are a general knowledge expert. Answer questions concisely."
}

{
  emit "--- Router/Dispatcher Pattern ---";

  // Math query
  let query1 = "What is 15 * 23?";
  emit "Query: " + query1;
  let route1 = Router.ask(query1);
  emit "Route: " + route1;
  let answer1 = MathExpert.ask(query1);
  emit "MathExpert: " + answer1;
  emit "";

  // Code query
  let query2 = "Write a hello world in Python";
  emit "Query: " + query2;
  let route2 = Router.ask(query2);
  emit "Route: " + route2;
  let answer2 = CodeExpert.ask(query2);
  emit "CodeExpert: " + answer2;
}
```

**Flow Diagram:**
```
[Query] → [Router] → MATH → [MathExpert]
                   → CODE → [CodeExpert]
                   → GENERAL → [GeneralExpert]
```

---

### Pattern 6: Debate/Adversarial

Multiple agents present different perspectives, with a judge synthesizing the conclusion.

**Use Case:** Decision support, balanced analysis, exploring trade-offs

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
  system: "You are an impartial judge. Given pro and con arguments, provide a balanced conclusion in 2 sentences."
}

{
  emit "--- Debate Pattern ---";

  let topic = "Remote work should be the default for knowledge workers";
  emit "Topic: " + topic;
  emit "";

  // Pro arguments
  let pro = Advocate.ask(topic);
  emit "Advocate (PRO): " + pro;
  emit "";

  // Con arguments
  let con = Critic.ask(topic);
  emit "Critic (CON): " + con;
  emit "";

  // Judge weighs in
  let verdict = Judge.ask("PRO arguments: " + pro + " --- CON arguments: " + con);
  emit "Judge (VERDICT): " + verdict;
}
```

**Flow Diagram:**
```
[Topic] → [Advocate] → PRO ─┐
                            ├→ [Judge] → Verdict
        → [Critic]   → CON ─┘
```

---

## RAG-Enhanced Patterns

These patterns combine agents with knowledge bases for grounded, accurate responses.

### Pattern 7: Expert Retrieval Agent

An agent connected to a knowledge base for domain-specific expertise.

**Use Case:** Documentation Q&A, technical support, domain experts

```neam
knowledge DomainKB {
  vector_store: "usearch"
  embedding_model: "nomic-embed-text"
  chunk_size: 200
  chunk_overlap: 50
  sources: [ { "type": "file", "path": "./readme.md" } ]
  retrieval_strategy: "basic"
  top_k: 3
}

agent DomainExpert {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You are a domain expert. Use the provided documentation context to answer accurately. Be concise."
  connected_knowledge: [DomainKB]
}

{
  emit "--- Expert Retrieval Agent ---";

  let question = "How do knowledge bases work in Neam?";
  emit "Q: " + question;

  let answer = DomainExpert.ask(question);
  emit "Expert: " + answer;
}
```

**RAG Flow:**
```
[Query] → [Knowledge Base] → relevant docs → [Agent + Context] → Answer
```

---

### Pattern 8: Research + RAG Pipeline

A RAG-enhanced researcher gathers information, then an analyst processes it.

**Use Case:** Research tasks, deep analysis, comprehensive reports

```neam
knowledge ResearchKB {
  vector_store: "usearch"
  embedding_model: "nomic-embed-text"
  chunk_size: 200
  chunk_overlap: 50
  sources: [ { "type": "file", "path": "./readme.md" } ]
  retrieval_strategy: "hyde"
  top_k: 3
  num_hypothetical: 1
}

agent RAGResearcher {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You are a technical researcher. Extract and summarize relevant facts from the documentation context."
  connected_knowledge: [ResearchKB]
}

agent Analyst {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You are a technical analyst. Given research notes, provide insights and recommendations. Be concise."
}

{
  emit "--- Research + RAG Pipeline ---";

  let topic = "Neam agent declarations and methods";
  emit "Topic: " + topic;

  // RAG-enhanced research
  let research = RAGResearcher.ask("Explain " + topic);
  emit "Researcher: " + research;

  // Analysis (no RAG, uses research output)
  let analysis = Analyst.ask("Based on this research, what are the key capabilities: " + research);
  emit "Analyst: " + analysis;
}
```

**Flow Diagram:**
```
[Topic] → [RAGResearcher + KB] → research → [Analyst] → insights
```

---

### Pattern 9: QA Validator with RAG

Answer questions then validate the answer against documentation.

**Use Case:** Fact-checking, accuracy validation, reliable Q&A

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
  system: "Answer questions based on the documentation. Be concise."
  connected_knowledge: [ValidatorKB]
}

agent FactChecker {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You are a fact checker. Verify if the given answer is accurate based on the documentation. Reply: VERIFIED, PARTIALLY_CORRECT, or INCORRECT with brief explanation."
  connected_knowledge: [ValidatorKB]
}

{
  emit "--- QA Validator with RAG ---";

  let question = "What vector store does Neam use?";
  emit "Q: " + question;

  // Get answer
  let answer = Responder.ask(question);
  emit "Responder: " + answer;

  // Validate answer
  let check = FactChecker.ask("Question: '" + question + "' Answer given: '" + answer + "' - Verify this.");
  emit "FactChecker: " + check;
}
```

**Flow Diagram:**
```
[Question] → [Responder + KB] → answer ─┐
                                        ├→ [FactChecker + KB] → VERIFIED/INCORRECT
           → [FactChecker + KB] ────────┘
```

---

### Pattern 10: Multi-KB Routing

Route queries to agents with specialized knowledge bases.

**Use Case:** Complex knowledge systems, multi-domain expertise

```neam
knowledge SyntaxKB {
  vector_store: "usearch"
  embedding_model: "nomic-embed-text"
  chunk_size: 200
  chunk_overlap: 50
  sources: [ { "type": "file", "path": "./readme.md" } ]
  retrieval_strategy: "basic"
  top_k: 3
}

knowledge ConceptKB {
  vector_store: "usearch"
  embedding_model: "nomic-embed-text"
  chunk_size: 200
  chunk_overlap: 50
  sources: [ { "type": "file", "path": "./readme.md" } ]
  retrieval_strategy: "hyde"
  top_k: 3
  num_hypothetical: 1
}

agent KBRouter {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "Classify the query type: SYNTAX (code examples), CONCEPT (how things work), or SETUP (building/running). Reply with only the category."
}

agent SyntaxExpert {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You explain syntax and provide code examples from documentation."
  connected_knowledge: [SyntaxKB]
}

agent ConceptExpert {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You explain concepts and architecture based on documentation."
  connected_knowledge: [ConceptKB]
}

{
  emit "--- Multi-KB Routing ---";

  // Syntax query
  let syntax_q = "Show me a Neam agent declaration example";
  emit "Query: " + syntax_q;
  let route_s = KBRouter.ask(syntax_q);
  emit "Route: " + route_s;
  let syntax_a = SyntaxExpert.ask(syntax_q);
  emit "SyntaxExpert: " + syntax_a;
  emit "";

  // Concept query
  let concept_q = "How do agents communicate with knowledge bases?";
  emit "Query: " + concept_q;
  let route_c = KBRouter.ask(concept_q);
  emit "Route: " + route_c;
  let concept_a = ConceptExpert.ask(concept_q);
  emit "ConceptExpert: " + concept_a;
}
```

**Flow Diagram:**
```
[Query] → [KBRouter] → SYNTAX → [SyntaxExpert + SyntaxKB]
                     → CONCEPT → [ConceptExpert + ConceptKB]
                     → SETUP → [SetupExpert + SetupKB]
```

---

## Pattern Selection Guide

| Pattern | Best For | Complexity | Latency |
|---------|----------|------------|---------|
| Single Agent | Simple Q&A | Low | Low |
| Multi-Agent Collaboration | Content creation | Medium | Medium |
| Sequential Pipeline | Data transformation | Low | Medium |
| Supervisor/Worker | Quality-critical tasks | Medium | Medium |
| Router/Dispatcher | Multi-domain queries | Medium | Medium |
| Debate/Adversarial | Decision making | Medium | High |
| Expert Retrieval | Domain Q&A | Low | Low |
| Research + RAG | Deep research | High | High |
| QA Validator | Fact-checking | Medium | Medium |
| Multi-KB Routing | Complex systems | High | Medium |

### Decision Tree

```
Is it a simple Q&A?
├─ Yes → Single Agent or Expert Retrieval (with RAG)
└─ No
   ├─ Need multiple perspectives? → Debate Pattern
   ├─ Need quality validation? → Supervisor/Worker
   ├─ Different query types? → Router/Dispatcher
   ├─ Multi-step transformation? → Sequential Pipeline
   └─ Complex creation task? → Multi-Agent Collaboration
```

---

## Running the Examples

### Prerequisites

1. **Build Neam:**
   ```bash
   mkdir -p build && cd build
   cmake .. -DCMAKE_BUILD_TYPE=Release
   cmake --build . --parallel
   cd ..
   ```

2. **For OpenAI:**
   ```bash
   export OPENAI_API_KEY="your-api-key"
   ```

3. **For Ollama (local):**
   ```bash
   ollama serve
   ollama pull qwen3:1.7b
   ollama pull nomic-embed-text
   ```

### Run Example Files

```bash
# All orchestration patterns (OpenAI)
./build/neamc examples/agentic_patterns_openai.neam -o /tmp/test.neamb
./build/neam /tmp/test.neamb

# RAG-enhanced patterns (OpenAI)
./build/neamc examples/agentic_rag_patterns.neam -o /tmp/test.neamb
./build/neam /tmp/test.neamb
```

### Switch Between Providers

To use Ollama instead of OpenAI, change:
```neam
// From:
agent MyAgent {
  provider: "openai"
  model: "gpt-4o-mini"
  ...
}

// To:
agent MyAgent {
  provider: "ollama"
  model: "qwen3:1.7b"
  ...
}
```

---

## Test Files Reference

| File | Patterns | Provider |
|------|----------|----------|
| `agentic_patterns_openai.neam` | 1-6 (Basic patterns) | OpenAI |
| `agentic_rag_patterns.neam` | 7-10 (RAG-enhanced) | OpenAI |

---

## Test Results Summary

All patterns tested successfully with OpenAI `gpt-4o-mini`:

| Pattern | Status | Notes |
|---------|--------|-------|
| Single Agent | Passed | Basic Q&A works |
| Multi-Agent Collaboration | Passed | Researcher→Writer→Editor chain |
| Sequential Pipeline | Passed | Translation→Summary pipeline |
| Supervisor/Worker | Passed | APPROVED validation |
| Router/Dispatcher | Passed | MATH/CODE routing |
| Debate | Passed | PRO/CON with Judge verdict |
| Expert Retrieval | Passed | 3 docs retrieved |
| Research + RAG | Passed | HyDE generated hypothetical docs |
| QA Validator | Passed | VERIFIED fact-check |
| Multi-KB Routing | Passed | SYNTAX/CONCEPT routing |

---

---

## Part 2: Special Agent Patterns

### Special Agent 1: DeepSearch Agent

Multi-iteration research with planning, searching, synthesizing, and reflection.

```neam
agent DeepSearchPlanner {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "Generate 3 sub-questions for the given question."
}

agent DeepSearcher {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "Answer with factual information. Be concise."
}

agent DeepSearchSynthesizer {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "Synthesize findings into a comprehensive answer."
}

agent DeepSearchReflector {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "Reply COMPLETE or GAPS with missing aspects."
}

{
  let question = "What factors made Apollo 11 successful?";

  // Step 1: Plan sub-questions
  let subq = DeepSearchPlanner.ask(question);

  // Step 2: Research
  let research = DeepSearcher.ask("What technology enabled Apollo 11?");

  // Step 3: Synthesize
  let synthesis = DeepSearchSynthesizer.ask("Question: " + question + " Findings: " + research);

  // Step 4: Reflect
  let reflection = DeepSearchReflector.ask("Question: " + question + " Answer: " + synthesis);
  emit reflection;  // COMPLETE or GAPS
}
```

**Flow:**
```
[Question] → [Planner] → sub-questions → [Searcher] → findings → [Synthesizer] → answer → [Reflector] → COMPLETE/GAPS
```

---

### Special Agent 2: Chain-of-Thought Agent

Explicit step-by-step reasoning for complex problems.

```neam
agent ChainOfThought {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "Solve step-by-step. Show work for each step."
}

{
  let math_problem = "A train goes 60mph to B and 40mph back. 120 miles each way. Average speed?";
  let solution = ChainOfThought.ask(math_problem);
  emit solution;  // Shows step-by-step: 48 mph
}
```

---

### Special Agent 3: ReAct Agent

Reasoning + Action interleaved pattern.

```neam
agent ReActReasoner {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "Output Thought, Action, or Answer. Actions: Search, Calculate, Lookup."
}

agent ReActExecutor {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "Execute Search or Calculate actions."
}

{
  let question = "What is the population of Japan in millions?";

  // Thought
  let thought1 = ReActReasoner.ask(question);
  emit "Thought: " + thought1;

  // Action + Observation
  let obs = ReActExecutor.ask("Search for population of Japan");
  emit "Observation: " + obs;

  // Final Answer
  let answer = ReActReasoner.ask("Japan population is 125 million. Provide Answer:");
  emit answer;
}
```

**Flow:**
```
[Question] → Thought → Action[Search] → Observation → Thought → Answer
```

---

### Special Agent 4: Self-Reflection Agent

Create → Critique → Refine loop.

```neam
agent Creator {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "Create content based on requirements."
}

agent SelfCritic {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "Identify 2-3 improvements needed."
}

agent Refiner {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "Improve content based on feedback."
}

{
  let task = "Write a tagline for a smart water bottle";

  let draft = Creator.ask(task);
  let critique = SelfCritic.ask("Review: " + draft);
  let refined = Refiner.ask("Improve: " + draft + " Based on: " + critique);
  emit refined;
}
```

---

### Special Agent 5: Planning Agent

Goal decomposition and monitored execution.

```neam
agent GoalDecomposer {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "Break goals into 3-5 actionable steps."
}

agent StepExecutor {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "Execute steps and report outcomes."
}

agent PlanMonitor {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "Assess: ON_TRACK, NEEDS_ADJUSTMENT, or BLOCKED."
}

{
  let goal = "Build a personal portfolio website";

  let task_plan = GoalDecomposer.ask(goal);
  emit "Plan: " + task_plan;

  let result = StepExecutor.ask("Choose a tech stack for portfolio");
  emit "Step Result: " + result;

  let status = PlanMonitor.ask("Goal: " + goal + " Done: Step 1. Result: " + result);
  emit status;  // ON_TRACK
}
```

---

### Special Agent 6: Socratic Agent

Teaching through guided questions.

```neam
agent SocraticTeacher {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "Teach through probing questions."
}

agent StudentSimulator {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "Answer as a curious student."
}

{
  let topic = "Why does ice float on water?";

  let q1 = SocraticTeacher.ask(topic);
  emit "Teacher: " + q1;

  let a1 = StudentSimulator.ask(q1);
  emit "Student: " + a1;

  let q2 = SocraticTeacher.ask("Student said: " + a1 + " Guide further:");
  emit "Teacher: " + q2;
}
```

---

### Special Agent 7: Red/Blue Team (Adversarial Testing)

Security analysis with attack and defense.

```neam
agent RedTeam {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "Identify 3 security vulnerabilities."
}

agent BlueTeam {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "Propose mitigations for vulnerabilities."
}

{
  let system_desc = "Web app with user file uploads to cloud bucket";

  let vulns = RedTeam.ask(system_desc);
  emit "Vulnerabilities: " + vulns;

  let mitigations = BlueTeam.ask("Mitigate: " + vulns);
  emit "Mitigations: " + mitigations;
}
```

---

### Special Agent 8: Memory Agent

Contextual memory extraction and retrieval.

```neam
agent MemoryKeeper {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "Extract key facts. Output FACTS with bullet points."
}

agent ContextualResponder {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "Answer using provided context."
}

{
  // Store facts from conversation
  let facts1 = MemoryKeeper.ask("I am John, a developer at Google for 3 years.");
  let facts2 = MemoryKeeper.ask("I am working on a search ranking algorithm.");

  // Query using accumulated context
  let mem_ctx = facts1 + " " + facts2;
  let response = ContextualResponder.ask("Context: " + mem_ctx + " Question: What does John do?");
  emit response;
}
```

---

## Special Agent Selection Guide

| Pattern | Use Case | Key Benefit |
|---------|----------|-------------|
| DeepSearch | Research tasks | Comprehensive answers with reflection |
| Chain-of-Thought | Math/Logic problems | Explicit reasoning steps |
| ReAct | Tasks needing external info | Action-observation loop |
| Self-Reflection | Content creation | Iterative improvement |
| Planning | Project management | Goal decomposition |
| Socratic | Teaching/Learning | Guided discovery |
| Red/Blue Team | Security analysis | Attack + Defense |
| Memory | Conversational AI | Context retention |

---

## Test Files

| File | Patterns |
|------|----------|
| `special_agents_openai.neam` | All 8 special agent patterns |
| `agentic_patterns_openai.neam` | 6 basic orchestration patterns |
| `agentic_rag_patterns.neam` | 4 RAG-enhanced patterns |

---

## Reserved Keywords Note

When writing Neam programs, avoid using these reserved words as variable names:
- `plan` - Reserved for planning constructs
- `context` - Reserved for context handling

Use alternatives like `task_plan`, `mem_ctx`, etc.

---

*Last tested: January 2026 with OpenAI gpt-4o-mini*
