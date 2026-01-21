# Learn Neam by Example

A comprehensive guide to Neam programming with annotated examples, from basics to advanced patterns.

---

## Table of Contents

### Fundamentals
1. [Hello World](#1-hello-world)
2. [Variables & Types](#2-variables--types)
3. [Control Flow](#3-control-flow)
4. [Functions](#4-functions)

### Agents
5. [Your First Agent](#5-your-first-agent)
6. [Custom System Prompts](#6-custom-system-prompts)
7. [Temperature & Creativity](#7-temperature--creativity)
8. [Local LLMs with Ollama](#8-local-llms-with-ollama)
9. [Agent with Goals](#9-agent-with-goals)

### Knowledge (RAG)
10. [Basic RAG](#10-basic-rag)
11. [Multiple Sources](#11-multiple-sources)
12. [Web Sources](#12-web-sources)
13. [Advanced Retrieval Strategies](#13-advanced-retrieval-strategies)

### Multi-Agent Patterns
14. [Multi-Agent Collaboration](#14-multi-agent-collaboration)
15. [Sequential Pipeline](#15-sequential-pipeline)
16. [Supervisor/Worker](#16-supervisorworker)
17. [Router/Dispatcher](#17-routerdispatcher)
18. [Debate Pattern](#18-debate-pattern)

### Advanced Patterns
19. [DeepSearch Agent](#19-deepsearch-agent)
20. [ReAct Pattern](#20-react-pattern)
21. [Self-Reflection](#21-self-reflection)
22. [Memory Management](#22-memory-management)

### Real-World Examples
23. [Customer Support Bot](#23-customer-support-bot)
24. [Code Review Assistant](#24-code-review-assistant)
25. [Research Agent](#25-research-agent)

---

## Fundamentals

### 1. Hello World

**Difficulty:** Beginner
**Time:** 1 minute
**What you'll learn:** Basic program structure, the `emit` statement

```neam
// hello.neam
// The simplest Neam program

{
  emit "Hello, World!";
}
```

**Explanation:**

| Line | Code | What it does |
|------|------|--------------|
| 1-2 | `// comment` | Comments start with `//` |
| 4 | `{` | Opens the main execution block |
| 5 | `emit "Hello, World!";` | Outputs text to the console |
| 6 | `}` | Closes the execution block |

**Key Concepts:**
- Every Neam program has at least one execution block `{ }`
- `emit` is how you output data (like `print` in other languages)
- Statements end with semicolons `;`

**Run it:**
```bash
neamc hello.neam -o hello.neamb
neam hello.neamb
```

**Output:**
```
Hello, World!
```

---

### 2. Variables & Types

**Difficulty:** Beginner
**Time:** 3 minutes
**What you'll learn:** Variable declaration, basic types, string concatenation

```neam
// variables.neam
// Working with variables and types in Neam

{
  // Declare variables with 'let'
  let name = "Alice";
  let age = 28;
  let score = 95.5;
  let is_active = true;

  // String concatenation with +
  emit "Name: " + name;
  emit "Age: " + age;
  emit "Score: " + score;

  // Conditional based on boolean
  if (is_active) {
    emit name + " is currently active";
  }

  // Arithmetic operations
  let doubled_age = age * 2;
  emit "In " + age + " years, " + name + " will be " + doubled_age;
}
```

**Explanation:**

| Line | Code | What it does |
|------|------|--------------|
| 5 | `let name = "Alice";` | Declares a string variable |
| 6 | `let age = 28;` | Declares an integer variable |
| 7 | `let score = 95.5;` | Declares a floating-point variable |
| 8 | `let is_active = true;` | Declares a boolean variable |
| 11 | `"Name: " + name` | Concatenates strings |
| 16 | `if (is_active) { }` | Conditional execution |
| 21 | `age * 2` | Arithmetic multiplication |

**Key Concepts:**
- `let` declares variables (type is inferred)
- Supported types: strings, integers, floats, booleans
- String concatenation uses `+`
- Variables are block-scoped

**Output:**
```
Name: Alice
Age: 28
Score: 95.5
Alice is currently active
In 28 years, Alice will be 56
```

---

### 3. Control Flow

**Difficulty:** Beginner
**Time:** 5 minutes
**What you'll learn:** if/else, while loops, comparison operators

```neam
// control_flow.neam
// Conditional logic and loops

{
  let temperature = 72;

  // If-else statement
  if (temperature > 80) {
    emit "It's hot outside!";
  } else if (temperature > 60) {
    emit "It's pleasant outside.";
  } else {
    emit "It's cold outside!";
  }

  // While loop
  emit "--- Countdown ---";
  let count = 5;
  while (count > 0) {
    emit "Count: " + count;
    count = count - 1;
  }
  emit "Liftoff!";

  // Comparison operators
  let a = 10;
  let b = 20;

  emit "a == b: " + (a == b);    // false
  emit "a != b: " + (a != b);    // true
  emit "a < b: " + (a < b);      // true
  emit "a >= b: " + (a >= b);    // false

  // Logical operators
  let x = true;
  let y = false;

  emit "x && y: " + (x && y);    // false (AND)
  emit "x || y: " + (x || y);    // true (OR)
  emit "!x: " + (!x);            // false (NOT)
}
```

**Explanation:**

| Construct | Syntax | Purpose |
|-----------|--------|---------|
| If-else | `if (cond) { } else { }` | Conditional branching |
| While | `while (cond) { }` | Repeat while condition is true |
| Equals | `==` | Check equality |
| Not equals | `!=` | Check inequality |
| AND | `&&` | Both conditions must be true |
| OR | `\|\|` | Either condition can be true |
| NOT | `!` | Inverts boolean |

**Output:**
```
It's pleasant outside.
--- Countdown ---
Count: 5
Count: 4
Count: 3
Count: 2
Count: 1
Liftoff!
a == b: false
a != b: true
a < b: true
a >= b: false
x && y: false
x || y: true
!x: false
```

---

### 4. Functions

**Difficulty:** Beginner
**Time:** 5 minutes
**What you'll learn:** Function declaration, parameters, return values

```neam
// functions.neam
// Defining and calling functions

// Simple function with no parameters
fun greet() {
  emit "Hello from a function!";
}

// Function with parameters
fun greet_person(name) {
  emit "Hello, " + name + "!";
}

// Function with return value
fun add(a, b) {
  return a + b;
}

// Function with conditional logic
fun get_grade(score) {
  if (score >= 90) {
    return "A";
  } else if (score >= 80) {
    return "B";
  } else if (score >= 70) {
    return "C";
  } else {
    return "F";
  }
}

// Main execution block
{
  // Call simple function
  greet();

  // Call function with argument
  greet_person("Bob");
  greet_person("Alice");

  // Use return value
  let sum = add(5, 3);
  emit "5 + 3 = " + sum;

  // Function in expression
  emit "10 + 20 = " + add(10, 20);

  // Conditional function
  let my_score = 85;
  let my_grade = get_grade(my_score);
  emit "Score " + my_score + " = Grade " + my_grade;
}
```

**Explanation:**

| Line | Code | What it does |
|------|------|--------------|
| 5-7 | `fun greet() { }` | Declares a function with no parameters |
| 10-12 | `fun greet_person(name)` | Function with one parameter |
| 15-17 | `fun add(a, b)` | Function with return value |
| 20-30 | `fun get_grade(score)` | Function with logic |
| 35 | `greet();` | Calls a function |
| 42 | `let sum = add(5, 3);` | Captures return value |

**Key Concepts:**
- Functions are declared with `fun`
- Parameters are listed in parentheses
- `return` sends a value back to the caller
- Functions must be declared before the main block

**Output:**
```
Hello from a function!
Hello, Bob!
Hello, Alice!
5 + 3 = 8
10 + 20 = 30
Score 85 = Grade B
```

---

## Agents

### 5. Your First Agent

**Difficulty:** Beginner
**Time:** 5 minutes
**What you'll learn:** Agent declaration, the `ask` method, LLM integration

```neam
// first_agent.neam
// Your first AI agent in Neam

// Define an agent
agent Assistant {
  provider: "openai"          // LLM provider (openai, ollama, etc.)
  model: "gpt-4o-mini"        // Model to use
  system: "You are a helpful assistant. Be concise."
}

// Main execution
{
  emit "=== Your First Agent ===";
  emit "";

  // Ask the agent a question
  let response = Assistant.ask("What is the capital of France?");

  emit "Question: What is the capital of France?";
  emit "Answer: " + response;
}
```

**Explanation:**

| Line | Code | What it does |
|------|------|--------------|
| 5 | `agent Assistant {` | Declares a new agent named "Assistant" |
| 6 | `provider: "openai"` | Specifies the LLM provider |
| 7 | `model: "gpt-4o-mini"` | Specifies which model to use |
| 8 | `system: "..."` | Sets the system prompt (agent's personality) |
| 17 | `Assistant.ask("...")` | Sends a query to the agent |

**Key Concepts:**
- Agents are first-class constructs in Neam
- Every agent needs a `provider` and `model`
- The `system` prompt defines the agent's behavior
- `.ask()` sends a message and returns the response

**Prerequisites:**
```bash
export OPENAI_API_KEY="sk-your-key-here"
```

**Output:**
```
=== Your First Agent ===

Question: What is the capital of France?
Answer: The capital of France is Paris.
```

---

### 6. Custom System Prompts

**Difficulty:** Beginner
**Time:** 5 minutes
**What you'll learn:** How system prompts shape agent behavior

```neam
// system_prompts.neam
// Different personalities through system prompts

// A formal, professional agent
agent FormalBot {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You are a formal business assistant. Use professional language, avoid contractions, and be precise."
}

// A casual, friendly agent
agent CasualBot {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You are a friendly, casual assistant. Use informal language, emojis occasionally, and be warm."
}

// A technical expert agent
agent TechExpert {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You are a senior software engineer. Provide technical, detailed answers with code examples when relevant."
}

// A creative writer agent
agent CreativeWriter {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You are a creative writer. Use vivid language, metaphors, and make your responses engaging and artistic."
}

{
  let question = "Explain what an API is";

  emit "=== Same Question, Different Personalities ===";
  emit "";

  emit "--- Formal Bot ---";
  emit FormalBot.ask(question);
  emit "";

  emit "--- Casual Bot ---";
  emit CasualBot.ask(question);
  emit "";

  emit "--- Tech Expert ---";
  emit TechExpert.ask(question);
  emit "";

  emit "--- Creative Writer ---";
  emit CreativeWriter.ask(question);
}
```

**Explanation:**

The system prompt is the most important part of agent configuration. It defines:
- **Tone**: Formal, casual, technical, creative
- **Constraints**: What to include/exclude
- **Format**: How to structure responses
- **Expertise**: What domain knowledge to apply

**Best Practices for System Prompts:**

| Do | Don't |
|-----|-------|
| Be specific about tone | Use vague instructions |
| Define output format | Leave format ambiguous |
| Set clear boundaries | Give conflicting rules |
| Include examples | Assume the model understands |

**Output Example:**
```
=== Same Question, Different Personalities ===

--- Formal Bot ---
An Application Programming Interface (API) is a set of protocols and
definitions that allows different software applications to communicate
with one another...

--- Casual Bot ---
Hey! 👋 So an API is basically like a waiter at a restaurant. You tell
the waiter what you want, they go to the kitchen (the server), and bring
back your food (the data)...

--- Tech Expert ---
An API (Application Programming Interface) is a contract that defines how
software components interact. It specifies endpoints, request/response
formats, and authentication methods...

--- Creative Writer ---
Imagine a universal translator that allows two beings who speak entirely
different languages to have a conversation. That's what an API does for
software...
```

---

### 7. Temperature & Creativity

**Difficulty:** Beginner
**Time:** 5 minutes
**What you'll learn:** Controlling randomness and creativity with temperature

```neam
// temperature.neam
// Understanding temperature settings

// Low temperature = deterministic, focused
agent PreciseBot {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You are a factual assistant."
  temperature: 0.1
}

// Medium temperature = balanced
agent BalancedBot {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You are a helpful assistant."
  temperature: 0.5
}

// High temperature = creative, varied
agent CreativeBot {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You are a creative assistant."
  temperature: 0.9
}

{
  let prompt = "Give me a name for a coffee shop";

  emit "=== Temperature Effects ===";
  emit "";
  emit "Prompt: " + prompt;
  emit "";

  // Low temperature - similar answers each time
  emit "--- Temperature 0.1 (Precise) ---";
  emit "Run 1: " + PreciseBot.ask(prompt);
  emit "Run 2: " + PreciseBot.ask(prompt);
  emit "Run 3: " + PreciseBot.ask(prompt);
  emit "";

  // High temperature - varied answers each time
  emit "--- Temperature 0.9 (Creative) ---";
  emit "Run 1: " + CreativeBot.ask(prompt);
  emit "Run 2: " + CreativeBot.ask(prompt);
  emit "Run 3: " + CreativeBot.ask(prompt);
}
```

**Explanation:**

| Temperature | Behavior | Use Case |
|-------------|----------|----------|
| 0.0 - 0.3 | Very deterministic, consistent | Facts, calculations, code |
| 0.4 - 0.6 | Balanced | General assistance |
| 0.7 - 1.0 | Creative, varied | Brainstorming, writing |

**How Temperature Works:**
- Low temperature → Model picks the most likely tokens
- High temperature → Model considers less likely tokens
- Same prompt + low temp = similar outputs
- Same prompt + high temp = varied outputs

**When to Use Each:**

| Low (0.1-0.3) | Medium (0.5) | High (0.8-1.0) |
|---------------|--------------|----------------|
| Code generation | Chatbots | Creative writing |
| Data extraction | Q&A | Brainstorming |
| Classification | Summarization | Poetry |
| Math problems | Translation | Story generation |

---

### 8. Local LLMs with Ollama

**Difficulty:** Intermediate
**Time:** 10 minutes
**What you'll learn:** Running agents with local models for privacy and offline use

```neam
// local_llm.neam
// Using Ollama for local, private AI

// Local agent using Ollama
agent LocalAssistant {
  provider: "ollama"
  model: "llama3:8b"          // or "mistral", "codellama", "qwen2.5"
  system: "You are a helpful local assistant. You run entirely on the user's machine."
}

// Local code expert
agent LocalCoder {
  provider: "ollama"
  model: "codellama:13b"
  system: "You are an expert programmer. Provide code solutions with explanations."
}

// Local reasoning model
agent LocalReasoner {
  provider: "ollama"
  model: "qwen2.5:14b"
  system: "You are a logical reasoning assistant. Think step by step."
}

{
  emit "=== Local LLM Examples ===";
  emit "All processing happens on YOUR machine. No data leaves your computer.";
  emit "";

  // General assistance
  emit "--- Local Assistant ---";
  let response = LocalAssistant.ask("What are the benefits of local AI?");
  emit response;
  emit "";

  // Code generation
  emit "--- Local Coder ---";
  let code = LocalCoder.ask("Write a function to check if a number is prime");
  emit code;
  emit "";

  // Reasoning task
  emit "--- Local Reasoner ---";
  let reasoning = LocalReasoner.ask("If all A are B, and all B are C, what can we conclude about A and C?");
  emit reasoning;
}
```

**Prerequisites:**

```bash
# Install Ollama
curl -fsSL https://ollama.com/install.sh | sh

# Pull models
ollama pull llama3:8b
ollama pull codellama:13b
ollama pull qwen2.5:14b

# Start Ollama server (usually auto-starts)
ollama serve
```

**Explanation:**

| Provider | Use Case | Pros | Cons |
|----------|----------|------|------|
| `openai` | Cloud API | Best quality, fast | Costs money, data leaves machine |
| `ollama` | Local | Free, private, offline | Requires GPU, slower |

**Available Ollama Models:**

| Model | Size | Best For |
|-------|------|----------|
| `llama3:8b` | 4.7GB | General purpose |
| `codellama:13b` | 7GB | Code generation |
| `qwen2.5:14b` | 8GB | Reasoning, multilingual |
| `mistral:7b` | 4GB | Fast, efficient |
| `mixtral:8x7b` | 26GB | High quality (needs RAM) |

**Key Benefits of Local LLMs:**
1. **Privacy**: Data never leaves your machine
2. **Cost**: No API fees
3. **Offline**: Works without internet
4. **Compliance**: Meets data residency requirements

---

### 9. Agent with Goals

**Difficulty:** Intermediate
**Time:** 10 minutes
**What you'll learn:** Structured agents with explicit goals, memory, and tools

```neam
// agent_with_goals.neam
// The canonical Neam agent with full structure

agent ResearchAgent {
  // Explicit goal declaration
  goal: "Answer questions using verified knowledge"

  // Provider configuration
  provider: "openai"
  model: "gpt-4o"
  system: "You are a thorough research assistant. Always verify information and cite sources when possible."

  // Memory configuration
  memory {
    short_term    // For current conversation context
    long_term     // For persistent knowledge
  }

  // Available tools
  tools {
    search        // Web search capability
    summarize     // Text summarization
    calculate     // Mathematical operations
  }

  // Execution plan
  plan {
    think         // Analyze the query
    verify        // Check information accuracy
    respond       // Formulate response
  }

  // Behavior definitions
  behavior {
    think(input) {
      // Gather relevant information
      insights = search(input)
      memory.short_term.store(insights)
      return insights
    }

    verify() {
      // Filter for high-confidence information
      trusted = memory.short_term.filter(confidence > 0.8)
      return trusted
    }

    respond() {
      // Generate final answer from verified info
      answer = summarize(verify())
      return answer
    }
  }
}

{
  emit "=== Structured Agent Example ===";
  emit "";

  // The agent uses its defined behavior
  let answer = ResearchAgent.ask("What are the key principles of clean code?");

  emit "Question: What are the key principles of clean code?";
  emit "";
  emit "Answer:";
  emit answer;
}
```

**Explanation:**

| Block | Purpose |
|-------|---------|
| `goal` | Declares the agent's primary objective |
| `memory` | Defines memory scopes (short-term, long-term) |
| `tools` | Lists capabilities the agent can use |
| `plan` | Specifies execution order |
| `behavior` | Implements each plan step |

**Why Goals Matter:**
- Makes agent behavior explicit
- Enables observability and debugging
- Allows verification of behavior
- Documents intent clearly

**This is the Neam difference:**
- Traditional: Behavior hidden in prompts
- Neam: Behavior is structured and inspectable

---

## Knowledge (RAG)

### 10. Basic RAG

**Difficulty:** Beginner
**Time:** 10 minutes
**What you'll learn:** Adding knowledge sources to agents

```neam
// basic_rag.neam
// Retrieval-Augmented Generation basics

// Define a knowledge base
knowledge CompanyDocs {
  // Vector store for embeddings
  vector_store: "usearch"

  // Embedding model
  embedding_model: "nomic-embed-text"

  // Chunking configuration
  chunk_size: 500          // Characters per chunk
  chunk_overlap: 50        // Overlap between chunks

  // Data sources
  sources: [
    { "type": "file", "path": "./docs/handbook.md" },
    { "type": "file", "path": "./docs/policies.md" }
  ]

  // Retrieval settings
  retrieval_strategy: "basic"
  top_k: 3                 // Return top 3 relevant chunks
}

// Agent connected to knowledge base
agent HRAssistant {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You are an HR assistant. Answer questions based ONLY on the provided company documentation. If the answer is not in the documents, say so."

  // Connect to knowledge base
  connected_knowledge: [CompanyDocs]
}

{
  emit "=== Basic RAG Example ===";
  emit "";

  // Questions answered from documents
  emit "Q: What is the vacation policy?";
  let answer1 = HRAssistant.ask("What is our vacation policy?");
  emit "A: " + answer1;
  emit "";

  emit "Q: How do I submit expenses?";
  let answer2 = HRAssistant.ask("How do I submit expense reports?");
  emit "A: " + answer2;
}
```

**Explanation:**

| Property | Purpose |
|----------|---------|
| `vector_store` | Storage engine for embeddings (usearch is built-in) |
| `embedding_model` | Model to convert text to vectors |
| `chunk_size` | How to split documents |
| `chunk_overlap` | Overlap to maintain context |
| `sources` | Where to load documents from |
| `top_k` | How many chunks to retrieve |
| `connected_knowledge` | Links agent to knowledge base |

**How RAG Works:**

```
┌─────────────┐     ┌──────────────┐     ┌─────────────┐
│ User Query  │────▶│ Find Similar │────▶│ Add Context │
│             │     │   Chunks     │     │  to Prompt  │
└─────────────┘     └──────────────┘     └─────────────┘
                           │                    │
                           ▼                    ▼
                    ┌──────────────┐     ┌─────────────┐
                    │ Vector Store │     │  LLM Answer │
                    │  (embedded   │     │  (grounded) │
                    │   documents) │     │             │
                    └──────────────┘     └─────────────┘
```

**Key Benefits:**
- Answers grounded in actual documents
- Reduces hallucination
- Always up-to-date (re-index to update)
- Controllable knowledge scope

---

### 11. Multiple Sources

**Difficulty:** Intermediate
**Time:** 10 minutes
**What you'll learn:** Combining multiple knowledge sources

```neam
// multiple_sources.neam
// Knowledge base with multiple source types

knowledge ProjectKnowledge {
  vector_store: "usearch"
  embedding_model: "nomic-embed-text"
  chunk_size: 400
  chunk_overlap: 50

  sources: [
    // Markdown documentation
    { "type": "file", "path": "./docs/*.md" },

    // Code files
    { "type": "file", "path": "./src/**/*.neam" },

    // Configuration files
    { "type": "file", "path": "./config/*.toml" },

    // README
    { "type": "file", "path": "./README.md" }
  ]

  retrieval_strategy: "hybrid"  // Keyword + vector search
  top_k: 5
}

// Technical documentation agent
knowledge APIDocs {
  vector_store: "usearch"
  embedding_model: "nomic-embed-text"
  chunk_size: 300
  chunk_overlap: 30

  sources: [
    { "type": "file", "path": "./api/**/*.md" }
  ]

  retrieval_strategy: "basic"
  top_k: 3
}

// Agent with multiple knowledge bases
agent ProjectExpert {
  provider: "openai"
  model: "gpt-4o"
  system: "You are a project expert. Use the provided documentation to answer questions about the codebase and APIs."

  // Connect to MULTIPLE knowledge bases
  connected_knowledge: [ProjectKnowledge, APIDocs]
}

{
  emit "=== Multiple Knowledge Sources ===";
  emit "";

  // Will search across all knowledge bases
  emit "Q: How do I configure the project?";
  emit ProjectExpert.ask("How do I configure this project?");
  emit "";

  emit "Q: What API endpoints are available?";
  emit ProjectExpert.ask("What API endpoints are available?");
  emit "";

  emit "Q: Show me an example from the codebase";
  emit ProjectExpert.ask("Show me an example of how agents are defined in this codebase");
}
```

**Explanation:**

**File Pattern Syntax:**

| Pattern | Matches |
|---------|---------|
| `./docs/*.md` | All .md files in docs folder |
| `./src/**/*.neam` | All .neam files in src and subdirectories |
| `./README.md` | Specific file |
| `./config/*.toml` | All .toml files in config |

**Multiple Knowledge Bases:**
- Agent can connect to multiple `knowledge` blocks
- Search happens across all connected knowledge
- Results are merged and ranked
- Use different configs for different content types

**Best Practices:**
- Smaller chunks for code (300-400 chars)
- Larger chunks for prose (500-800 chars)
- Use `hybrid` strategy for mixed content
- Separate technical/non-technical content

---

### 12. Web Sources

**Difficulty:** Intermediate
**Time:** 10 minutes
**What you'll learn:** Ingesting web content into knowledge bases

```neam
// web_sources.neam
// Knowledge from web pages

knowledge WebDocs {
  vector_store: "usearch"
  embedding_model: "nomic-embed-text"
  chunk_size: 500
  chunk_overlap: 50

  sources: [
    // Web pages
    { "type": "web", "url": "https://docs.example.com/guide" },
    { "type": "web", "url": "https://docs.example.com/api" },

    // Mix of web and local
    { "type": "file", "path": "./local_docs/*.md" }
  ]

  retrieval_strategy: "basic"
  top_k: 4
}

agent WebExpert {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You answer questions based on the documentation. Be specific and cite sources when possible."

  connected_knowledge: [WebDocs]
}

{
  emit "=== Web Sources RAG ===";
  emit "";

  emit WebExpert.ask("What does the documentation say about authentication?");
}
```

**Explanation:**

**Source Types:**

| Type | Syntax | Use Case |
|------|--------|----------|
| `file` | `{ "type": "file", "path": "..." }` | Local files |
| `web` | `{ "type": "web", "url": "..." }` | Web pages |

**Web Ingestion:**
- Fetches HTML from URL
- Extracts text content
- Removes navigation, ads, etc.
- Chunks and embeds like local files

**Considerations:**
- Web sources are fetched at runtime
- Some sites may block scraping
- Consider caching for performance
- Respect robots.txt

---

### 13. Advanced Retrieval Strategies

**Difficulty:** Advanced
**Time:** 15 minutes
**What you'll learn:** Different RAG strategies for different use cases

```neam
// advanced_rag.neam
// Comparing retrieval strategies

// Strategy 1: Basic (default)
knowledge BasicKB {
  vector_store: "usearch"
  embedding_model: "nomic-embed-text"
  chunk_size: 200
  sources: [{ "type": "file", "path": "./docs/*.md" }]

  retrieval_strategy: "basic"
  top_k: 3
}

// Strategy 2: MMR (Maximal Marginal Relevance)
// Returns diverse results, not just most similar
knowledge DiverseKB {
  vector_store: "usearch"
  embedding_model: "nomic-embed-text"
  chunk_size: 200
  sources: [{ "type": "file", "path": "./docs/*.md" }]

  retrieval_strategy: "mmr"
  top_k: 5
  mmr_lambda: 0.7    // 0 = max diversity, 1 = max relevance
}

// Strategy 3: Hybrid (keyword + vector)
// Best for technical content with specific terms
knowledge HybridKB {
  vector_store: "usearch"
  embedding_model: "nomic-embed-text"
  chunk_size: 200
  sources: [{ "type": "file", "path": "./docs/*.md" }]

  retrieval_strategy: "hybrid"
  top_k: 4
}

// Strategy 4: HyDE (Hypothetical Document Embeddings)
// Generates hypothetical answer first, then finds similar docs
knowledge HyDEKB {
  vector_store: "usearch"
  embedding_model: "nomic-embed-text"
  chunk_size: 200
  sources: [{ "type": "file", "path": "./docs/*.md" }]

  retrieval_strategy: "hyde"
  top_k: 3
  num_hypothetical: 1
}

// Strategy 5: Self-RAG
// Retrieves, checks relevance, may re-retrieve
knowledge SelfRAGKB {
  vector_store: "usearch"
  embedding_model: "nomic-embed-text"
  chunk_size: 200
  sources: [{ "type": "file", "path": "./docs/*.md" }]

  retrieval_strategy: "self_rag"
  top_k: 5
  enable_relevance_check: true
  enable_support_check: true
}

// Agents for each strategy
agent BasicAgent {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "Answer based on provided context."
  connected_knowledge: [BasicKB]
}

agent DiverseAgent {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "Answer based on provided context. Consider multiple perspectives."
  connected_knowledge: [DiverseKB]
}

agent HybridAgent {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "Answer based on provided context. Be precise with technical terms."
  connected_knowledge: [HybridKB]
}

agent HyDEAgent {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "Answer based on provided context."
  connected_knowledge: [HyDEKB]
}

agent SelfRAGAgent {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "Answer based on provided context. Only use relevant information."
  connected_knowledge: [SelfRAGKB]
}

{
  let query = "What are the main features?";

  emit "=== RAG Strategy Comparison ===";
  emit "Query: " + query;
  emit "";

  emit "--- Basic ---";
  emit BasicAgent.ask(query);
  emit "";

  emit "--- MMR (Diverse) ---";
  emit DiverseAgent.ask(query);
  emit "";

  emit "--- Hybrid ---";
  emit HybridAgent.ask(query);
  emit "";

  emit "--- HyDE ---";
  emit HyDEAgent.ask(query);
  emit "";

  emit "--- Self-RAG ---";
  emit SelfRAGAgent.ask(query);
}
```

**Strategy Comparison:**

| Strategy | How It Works | Best For |
|----------|--------------|----------|
| `basic` | Simple vector similarity | General Q&A |
| `mmr` | Balances relevance + diversity | Comprehensive answers |
| `hybrid` | Keyword + vector search | Technical docs, code |
| `hyde` | Generates hypothetical answer first | Abstract questions |
| `self_rag` | Checks relevance, may re-retrieve | High accuracy needs |
| `crag` | Decomposes complex queries | Multi-part questions |
| `agentic` | Iterative refinement | Research tasks |

**When to Use Each:**

| Use Case | Recommended Strategy |
|----------|---------------------|
| Simple Q&A | `basic` |
| "Give me diverse perspectives" | `mmr` |
| Code/API documentation | `hybrid` |
| Abstract/conceptual questions | `hyde` |
| High-stakes accuracy | `self_rag` |
| Complex, multi-part questions | `crag` |
| Deep research | `agentic` |

---

## Multi-Agent Patterns

### 14. Multi-Agent Collaboration

**Difficulty:** Intermediate
**Time:** 15 minutes
**What you'll learn:** Multiple agents working together on a task

```neam
// collaboration.neam
// Multi-agent content creation pipeline

// Research specialist
agent Researcher {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You are a researcher. Gather key facts and information. Be thorough but concise. Output bullet points."
}

// Writing specialist
agent Writer {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You are a professional writer. Take research notes and create polished, engaging prose. Write in a clear, professional style."
}

// Editing specialist
agent Editor {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You are a senior editor. Review text for clarity, grammar, and flow. Make improvements and output the final version. Be concise."
}

{
  let topic = "The benefits of remote work";

  emit "=== Multi-Agent Collaboration ===";
  emit "Topic: " + topic;
  emit "";

  // Step 1: Research
  emit "--- Step 1: Research ---";
  let research = Researcher.ask("Research key facts about: " + topic);
  emit research;
  emit "";

  // Step 2: Write (using research output)
  emit "--- Step 2: Write ---";
  let draft = Writer.ask("Write a short article based on these notes:\n" + research);
  emit draft;
  emit "";

  // Step 3: Edit (using writer output)
  emit "--- Step 3: Edit ---";
  let final_article = Editor.ask("Edit and improve this article:\n" + draft);
  emit final_article;
}
```

**Flow Diagram:**

```
┌────────────┐     research      ┌────────────┐     draft       ┌────────────┐
│ Researcher │─────────────────▶│   Writer   │────────────────▶│   Editor   │
│            │    (bullet pts)   │            │    (prose)      │            │
└────────────┘                   └────────────┘                 └────────────┘
      │                                │                              │
      ▼                                ▼                              ▼
   Key facts                      First draft                   Final article
```

**Why This Pattern Works:**
- **Specialization**: Each agent excels at one task
- **Quality**: Multiple passes improve output
- **Modularity**: Easy to swap/modify individual agents
- **Debugging**: Clear checkpoints to inspect

---

### 15. Sequential Pipeline

**Difficulty:** Intermediate
**Time:** 10 minutes
**What you'll learn:** Data transformation chains

```neam
// pipeline.neam
// Sequential data transformation pipeline

agent Translator {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "Translate the given text to French. Output ONLY the translation, nothing else."
}

agent Summarizer {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "Summarize the given text in exactly one sentence. Output ONLY the summary."
}

agent SentimentAnalyzer {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "Analyze the sentiment of the text. Output only: POSITIVE, NEGATIVE, or NEUTRAL"
}

agent KeywordExtractor {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "Extract 3-5 keywords from the text. Output as comma-separated list."
}

{
  let original = "The new smartphone features an incredible camera system, long battery life, and a beautiful display. Users are loving the fast performance and seamless experience. However, some find the price a bit steep.";

  emit "=== Sequential Pipeline ===";
  emit "";
  emit "Original Text:";
  emit original;
  emit "";

  // Pipeline: Translate → Summarize → Analyze → Extract
  emit "--- Stage 1: Translate to French ---";
  let french = Translator.ask(original);
  emit french;
  emit "";

  emit "--- Stage 2: Summarize ---";
  let summary = Summarizer.ask(original);
  emit summary;
  emit "";

  emit "--- Stage 3: Sentiment Analysis ---";
  let sentiment = SentimentAnalyzer.ask(original);
  emit "Sentiment: " + sentiment;
  emit "";

  emit "--- Stage 4: Keyword Extraction ---";
  let keywords = KeywordExtractor.ask(original);
  emit "Keywords: " + keywords;
}
```

**Pipeline Visualization:**

```
Original Text
     │
     ▼
┌─────────────┐
│  Translator │ ──▶ French version
└─────────────┘
     │
     ▼
┌─────────────┐
│ Summarizer  │ ──▶ One-sentence summary
└─────────────┘
     │
     ▼
┌─────────────┐
│  Sentiment  │ ──▶ POSITIVE/NEGATIVE/NEUTRAL
└─────────────┘
     │
     ▼
┌─────────────┐
│  Keywords   │ ──▶ keyword1, keyword2, keyword3
└─────────────┘
```

**Use Cases:**
- Document processing workflows
- Content transformation
- Data enrichment pipelines
- Multi-stage analysis

---

### 16. Supervisor/Worker

**Difficulty:** Intermediate
**Time:** 15 minutes
**What you'll learn:** Quality control with supervisor validation

```neam
// supervisor_worker.neam
// Supervisor validates worker output

agent Worker {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You complete tasks thoroughly. Follow instructions precisely."
}

agent Supervisor {
  provider: "openai"
  model: "gpt-4o"  // Using stronger model for supervision
  system: "You are a quality supervisor. Evaluate work against requirements. Reply with APPROVED if the work meets all requirements, or NEEDS_REVISION with specific feedback if it doesn't."
}

{
  let task = "Write a haiku about programming";
  let requirements = "Must be exactly 3 lines with 5-7-5 syllable pattern";

  emit "=== Supervisor/Worker Pattern ===";
  emit "";
  emit "Task: " + task;
  emit "Requirements: " + requirements;
  emit "";

  let attempt = 1;
  let max_attempts = 3;
  let approved = false;
  let work = "";

  while (!approved && attempt <= max_attempts) {
    emit "--- Attempt " + attempt + " ---";

    // Worker does the task
    if (attempt == 1) {
      work = Worker.ask(task);
    } else {
      // Include previous feedback for revision
      work = Worker.ask("Revise based on feedback. Original task: " + task + "\nFeedback: " + feedback);
    }
    emit "Worker output:";
    emit work;
    emit "";

    // Supervisor evaluates
    let evaluation = Supervisor.ask("Task: " + task + "\nRequirements: " + requirements + "\n\nWork to evaluate:\n" + work);
    emit "Supervisor says: " + evaluation;
    emit "";

    // Check if approved
    if (evaluation.contains("APPROVED")) {
      approved = true;
      emit "✓ Work approved on attempt " + attempt;
    } else {
      feedback = evaluation;
      attempt = attempt + 1;
    }
  }

  if (!approved) {
    emit "✗ Max attempts reached. Final output:";
    emit work;
  }
}
```

**Pattern Diagram:**

```
┌──────────┐         work          ┌────────────┐
│  Worker  │ ─────────────────────▶│ Supervisor │
│          │                       │            │
│          │◀───────────────────── │            │
└──────────┘    feedback/approve   └────────────┘
     │
     │ (if not approved)
     ▼
   Revise
```

**When to Use:**
- Quality-critical outputs
- Complex tasks needing validation
- Learning/improvement loops
- Compliance requirements

---

### 17. Router/Dispatcher

**Difficulty:** Intermediate
**Time:** 15 minutes
**What you'll learn:** Routing queries to specialized agents

```neam
// router.neam
// Route queries to specialized agents

// Router agent - decides which specialist to use
agent Router {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You are a query router. Analyze the user's question and decide which specialist should handle it. Reply with ONLY one of: CODE, DATA, WRITING, GENERAL"
}

// Specialist agents
agent CodeExpert {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You are an expert programmer. Provide code solutions with clear explanations."
}

agent DataExpert {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You are a data analyst. Help with data analysis, statistics, and visualization."
}

agent WritingExpert {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You are a writing expert. Help with content creation, editing, and communication."
}

agent GeneralAssistant {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You are a helpful general assistant."
}

{
  // Test queries for different specialists
  let queries = [
    "How do I implement a binary search tree?",
    "What's the best way to visualize sales trends?",
    "Can you help me write an email to my team?",
    "What's the weather like today?"
  ];

  emit "=== Router/Dispatcher Pattern ===";
  emit "";

  // Process each query
  let i = 0;
  while (i < 4) {
    let query = queries[i];
    emit "Query: " + query;

    // Route the query
    let route = Router.ask(query);
    emit "Routed to: " + route;

    // Dispatch to appropriate specialist
    let response = "";
    if (route == "CODE") {
      response = CodeExpert.ask(query);
    } else if (route == "DATA") {
      response = DataExpert.ask(query);
    } else if (route == "WRITING") {
      response = WritingExpert.ask(query);
    } else {
      response = GeneralAssistant.ask(query);
    }

    emit "Response: " + response;
    emit "";
    emit "---";
    emit "";

    i = i + 1;
  }
}
```

**Pattern Diagram:**

```
                        ┌─────────────┐
                        │   Router    │
                        │  (classify) │
                        └──────┬──────┘
                               │
           ┌───────────┬───────┼───────┬───────────┐
           ▼           ▼       ▼       ▼           ▼
      ┌─────────┐ ┌─────────┐ ┌─────────┐ ┌─────────────┐
      │  Code   │ │  Data   │ │ Writing │ │   General   │
      │ Expert  │ │ Expert  │ │ Expert  │ │  Assistant  │
      └─────────┘ └─────────┘ └─────────┘ └─────────────┘
```

**Benefits:**
- Specialized handling for each query type
- Better accuracy than one generalist
- Easy to add new specialists
- Clear routing logic

---

### 18. Debate Pattern

**Difficulty:** Advanced
**Time:** 15 minutes
**What you'll learn:** Adversarial agents for balanced analysis

```neam
// debate.neam
// Pro vs Con debate with judge

agent ProArguer {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You argue IN FAVOR of the given topic. Be persuasive, use facts and logic. Keep arguments concise (2-3 points)."
}

agent ConArguer {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You argue AGAINST the given topic. Be persuasive, use facts and logic. Keep arguments concise (2-3 points)."
}

agent Judge {
  provider: "openai"
  model: "gpt-4o"  // Stronger model for fair judgment
  system: "You are an impartial judge. Evaluate both sides of the debate fairly. Summarize key points from each side and provide a balanced conclusion."
}

{
  let topic = "Remote work should be the default for knowledge workers";

  emit "=== Debate Pattern ===";
  emit "";
  emit "Topic: " + topic;
  emit "";

  // Round 1: Opening arguments
  emit "--- Round 1: Opening Arguments ---";
  emit "";

  emit "PRO:";
  let pro1 = ProArguer.ask("Argue in favor of: " + topic);
  emit pro1;
  emit "";

  emit "CON:";
  let con1 = ConArguer.ask("Argue against: " + topic);
  emit con1;
  emit "";

  // Round 2: Rebuttals
  emit "--- Round 2: Rebuttals ---";
  emit "";

  emit "PRO rebuttal:";
  let pro2 = ProArguer.ask("Respond to these counter-arguments: " + con1);
  emit pro2;
  emit "";

  emit "CON rebuttal:";
  let con2 = ConArguer.ask("Respond to these arguments: " + pro1);
  emit con2;
  emit "";

  // Judge's verdict
  emit "--- Judge's Verdict ---";
  let verdict = Judge.ask("Topic: " + topic + "\n\nPRO arguments:\n" + pro1 + "\n" + pro2 + "\n\nCON arguments:\n" + con1 + "\n" + con2);
  emit verdict;
}
```

**Debate Flow:**

```
Round 1:
┌──────────┐                    ┌──────────┐
│   PRO    │                    │   CON    │
│ (favor)  │                    │(against) │
└────┬─────┘                    └────┬─────┘
     │ opening                       │ opening
     ▼                               ▼

Round 2:
┌──────────┐   reads CON args   ┌──────────┐
│   PRO    │◀──────────────────▶│   CON    │
│ rebuttal │   reads PRO args   │ rebuttal │
└────┬─────┘                    └────┬─────┘
     │                               │
     └───────────┬───────────────────┘
                 ▼
          ┌────────────┐
          │   Judge    │
          │ (verdict)  │
          └────────────┘
```

**Use Cases:**
- Balanced analysis of complex issues
- Red team / blue team security
- Decision making support
- Content that needs multiple perspectives

---

## Advanced Patterns

### 19. DeepSearch Agent

**Difficulty:** Advanced
**Time:** 20 minutes
**What you'll learn:** Research agent with planning, searching, and synthesis

```neam
// deepsearch.neam
// Multi-stage research agent

knowledge ResearchDocs {
  vector_store: "usearch"
  embedding_model: "nomic-embed-text"
  chunk_size: 400
  sources: [{ "type": "file", "path": "./research_docs/*.md" }]
  retrieval_strategy: "agentic"
  top_k: 5
  max_iterations: 3
  enable_reflection: true
}

agent Planner {
  provider: "openai"
  model: "gpt-4o"
  system: "You are a research planner. Break down complex questions into specific sub-questions that can be researched. Output a numbered list of 3-5 sub-questions."
}

agent Researcher {
  provider: "openai"
  model: "gpt-4o"
  system: "You are a thorough researcher. Use the provided knowledge to answer questions. Be factual and cite sources."
  connected_knowledge: [ResearchDocs]
}

agent Synthesizer {
  provider: "openai"
  model: "gpt-4o"
  system: "You synthesize research findings into a coherent, comprehensive answer. Organize information logically and highlight key insights."
}

agent Reflector {
  provider: "openai"
  model: "gpt-4o"
  system: "You critically review research. Identify gaps, inconsistencies, or areas needing more investigation. Be constructive."
}

{
  let question = "What are the key factors affecting software project success?";

  emit "=== DeepSearch Pattern ===";
  emit "";
  emit "Main Question: " + question;
  emit "";

  // Stage 1: Plan
  emit "--- Stage 1: Planning ---";
  let plan = Planner.ask("Break down this question: " + question);
  emit plan;
  emit "";

  // Stage 2: Research (would iterate through sub-questions)
  emit "--- Stage 2: Research ---";
  let findings = Researcher.ask(question + "\n\nFocus areas:\n" + plan);
  emit findings;
  emit "";

  // Stage 3: Reflect
  emit "--- Stage 3: Reflection ---";
  let reflection = Reflector.ask("Review these findings for gaps:\n" + findings);
  emit reflection;
  emit "";

  // Stage 4: Additional research if needed
  emit "--- Stage 4: Additional Research ---";
  let additional = Researcher.ask("Address these gaps:\n" + reflection);
  emit additional;
  emit "";

  // Stage 5: Synthesize
  emit "--- Stage 5: Synthesis ---";
  let synthesis = Synthesizer.ask("Synthesize all findings:\n\nInitial findings:\n" + findings + "\n\nAdditional findings:\n" + additional);
  emit synthesis;
}
```

**DeepSearch Flow:**

```
┌────────────┐
│  Question  │
└─────┬──────┘
      ▼
┌────────────┐
│  Planner   │──▶ Sub-questions
└─────┬──────┘
      ▼
┌────────────┐
│ Researcher │──▶ Initial findings
└─────┬──────┘
      ▼
┌────────────┐
│ Reflector  │──▶ Gaps identified
└─────┬──────┘
      ▼
┌────────────┐
│ Researcher │──▶ Additional findings
│  (again)   │
└─────┬──────┘
      ▼
┌────────────┐
│Synthesizer │──▶ Final comprehensive answer
└────────────┘
```

---

### 20. ReAct Pattern

**Difficulty:** Advanced
**Time:** 20 minutes
**What you'll learn:** Thought → Action → Observation loop

```neam
// react.neam
// ReAct: Reasoning and Acting pattern

agent ReActAgent {
  provider: "openai"
  model: "gpt-4o"
  system: "You solve problems using the ReAct pattern. For each step:
1. THOUGHT: Reason about what to do next
2. ACTION: Specify an action (search, calculate, lookup)
3. Wait for OBSERVATION
4. Repeat until you have the answer

Format your response as:
THOUGHT: [your reasoning]
ACTION: [action to take]

When you have the final answer:
THOUGHT: I now have enough information
ANSWER: [your final answer]"

  tools {
    search
    calculate
  }
}

agent Observer {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You simulate tool outputs. Given an action, provide a realistic observation/result."
}

{
  let question = "What is the population of France multiplied by 2?";

  emit "=== ReAct Pattern ===";
  emit "";
  emit "Question: " + question;
  emit "";

  let context = "Question: " + question;
  let iteration = 1;
  let max_iterations = 5;
  let complete = false;

  while (!complete && iteration <= max_iterations) {
    emit "--- Iteration " + iteration + " ---";

    // Agent thinks and decides action
    let response = ReActAgent.ask(context);
    emit response;
    emit "";

    // Check if we have final answer
    if (response.contains("ANSWER:")) {
      complete = true;
      emit "✓ Solution found!";
    } else {
      // Simulate observation
      let observation = Observer.ask("Simulate result for: " + response);
      emit "OBSERVATION: " + observation;
      emit "";

      // Add to context for next iteration
      context = context + "\n\n" + response + "\nOBSERVATION: " + observation;
    }

    iteration = iteration + 1;
  }
}
```

**ReAct Loop:**

```
┌─────────────────────────────────────────────┐
│                                             │
│  ┌─────────┐    ┌─────────┐    ┌─────────┐ │
│  │ THOUGHT │───▶│ ACTION  │───▶│OBSERVAT.│ │
│  │         │    │         │    │         │ │
│  └─────────┘    └─────────┘    └────┬────┘ │
│       ▲                             │      │
│       └─────────────────────────────┘      │
│                                             │
│              (repeat until ANSWER)          │
│                                             │
└─────────────────────────────────────────────┘
```

**Key Insight:**
- Interleaves reasoning with action
- Makes thought process explicit
- Allows correction mid-stream
- Mirrors human problem-solving

---

### 21. Self-Reflection

**Difficulty:** Advanced
**Time:** 15 minutes
**What you'll learn:** Agents that critique and improve their own work

```neam
// self_reflection.neam
// Create → Critique → Refine loop

agent Creator {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You create content based on requirements. Be creative and thorough."
}

agent Critic {
  provider: "openai"
  model: "gpt-4o"
  system: "You critically evaluate content. Identify specific weaknesses, gaps, and areas for improvement. Be constructive and specific."
}

agent Refiner {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You improve content based on feedback. Address each criticism specifically while maintaining strengths."
}

{
  let task = "Write a product description for a smart water bottle that tracks hydration";

  emit "=== Self-Reflection Pattern ===";
  emit "";
  emit "Task: " + task;
  emit "";

  // Initial creation
  emit "--- Version 1: Initial Creation ---";
  let v1 = Creator.ask(task);
  emit v1;
  emit "";

  // Critique
  emit "--- Critique ---";
  let critique = Critic.ask("Evaluate this product description:\n" + v1);
  emit critique;
  emit "";

  // Refine
  emit "--- Version 2: Refined ---";
  let v2 = Refiner.ask("Improve this based on feedback:\n\nOriginal:\n" + v1 + "\n\nFeedback:\n" + critique);
  emit v2;
  emit "";

  // Second critique
  emit "--- Second Critique ---";
  let critique2 = Critic.ask("Evaluate this improved version:\n" + v2);
  emit critique2;
  emit "";

  // Final refinement
  emit "--- Version 3: Final ---";
  let v3 = Refiner.ask("Final improvements:\n\nCurrent version:\n" + v2 + "\n\nRemaining feedback:\n" + critique2);
  emit v3;
}
```

**Reflection Loop:**

```
┌──────────┐
│ Creator  │──▶ Version 1
└────┬─────┘
     │
     ▼
┌──────────┐
│  Critic  │──▶ Feedback
└────┬─────┘
     │
     ▼
┌──────────┐
│ Refiner  │──▶ Version 2
└────┬─────┘
     │
     ▼
   (repeat)
```

---

### 22. Memory Management

**Difficulty:** Advanced
**Time:** 20 minutes
**What you'll learn:** Explicit memory scopes and persistence

```neam
// memory.neam
// Agent with explicit memory management

agent MemoryAgent {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You are an assistant with memory. Use your memory to maintain context across conversations."

  // Memory configuration
  memory {
    // Short-term: current session
    short_term {
      max_items: 10
      ttl: 3600  // 1 hour
    }

    // Long-term: persistent
    long_term {
      storage: "file"
      path: "./agent_memory.json"
    }

    // Working: for current task
    working {
      max_items: 5
    }
  }
}

agent FactExtractor {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "Extract key facts from conversations. Output as JSON with 'fact' and 'confidence' fields."
}

{
  emit "=== Memory Management ===";
  emit "";

  // Conversation with memory
  emit "--- Conversation ---";
  emit "";

  emit "User: My name is Alex and I'm a software engineer.";
  let r1 = MemoryAgent.ask("My name is Alex and I'm a software engineer.");
  emit "Agent: " + r1;

  // Extract and store fact
  let fact1 = FactExtractor.ask("Extract fact: 'My name is Alex and I'm a software engineer'");
  MemoryAgent.memory.long_term.store(fact1);
  emit "";

  emit "User: I'm working on a machine learning project.";
  let r2 = MemoryAgent.ask("I'm working on a machine learning project.");
  emit "Agent: " + r2;

  let fact2 = FactExtractor.ask("Extract fact: 'Working on a machine learning project'");
  MemoryAgent.memory.long_term.store(fact2);
  emit "";

  // Test memory recall
  emit "--- Memory Recall ---";
  emit "";

  emit "User: What do you remember about me?";
  let r3 = MemoryAgent.ask("What do you remember about me?");
  emit "Agent: " + r3;
  emit "";

  // Show stored facts
  emit "--- Stored Facts ---";
  let facts = MemoryAgent.memory.long_term.retrieve_all();
  emit facts;
}
```

**Memory Architecture:**

```
┌─────────────────────────────────────────────────┐
│                   Agent Memory                   │
├─────────────────────────────────────────────────┤
│                                                  │
│  ┌─────────────────┐    ┌─────────────────────┐ │
│  │   Short-Term    │    │      Long-Term      │ │
│  │                 │    │                     │ │
│  │  • Current conv │    │  • Persistent facts │ │
│  │  • Session data │    │  • User preferences │ │
│  │  • Expires soon │    │  • Historical data  │ │
│  │                 │    │                     │ │
│  └─────────────────┘    └─────────────────────┘ │
│                                                  │
│  ┌─────────────────┐                            │
│  │     Working     │                            │
│  │                 │                            │
│  │  • Current task │                            │
│  │  • Temp storage │                            │
│  │  • Quick access │                            │
│  │                 │                            │
│  └─────────────────┘                            │
│                                                  │
└─────────────────────────────────────────────────┘
```

---

## Real-World Examples

### 23. Customer Support Bot

**Difficulty:** Intermediate
**Time:** 20 minutes
**What you'll learn:** Production-ready support bot with RAG

```neam
// support_bot.neam
// Complete customer support solution

// Knowledge base from support documentation
knowledge SupportKB {
  vector_store: "usearch"
  embedding_model: "nomic-embed-text"
  chunk_size: 400
  chunk_overlap: 50

  sources: [
    { "type": "file", "path": "./support_docs/faq.md" },
    { "type": "file", "path": "./support_docs/troubleshooting.md" },
    { "type": "file", "path": "./support_docs/policies.md" }
  ]

  retrieval_strategy: "hybrid"
  top_k: 4
}

// Intent classifier
agent IntentClassifier {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "Classify customer intent. Reply with ONLY one of: QUESTION, COMPLAINT, FEEDBACK, BILLING, TECHNICAL, ESCALATE"
}

// Support agent with knowledge
agent SupportAgent {
  provider: "openai"
  model: "gpt-4o"
  system: "You are a helpful customer support agent for TechCorp.

Guidelines:
- Be friendly, professional, and empathetic
- Use the provided documentation to answer questions
- If you don't know something, say so and offer to escalate
- Keep responses concise but complete
- End with asking if there's anything else you can help with

Company: TechCorp
Product: CloudSync Pro"

  connected_knowledge: [SupportKB]
}

// Escalation agent
agent EscalationHandler {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You handle escalated support requests. Acknowledge the issue, apologize for any inconvenience, and explain that a human agent will follow up within 24 hours."
}

{
  emit "=== Customer Support Bot ===";
  emit "";
  emit "Welcome to TechCorp Support. How can I help you today?";
  emit "";

  // Simulate customer queries
  let queries = [
    "How do I reset my password?",
    "I've been charged twice for my subscription!",
    "The app keeps crashing on my phone"
  ];

  let i = 0;
  while (i < 3) {
    let query = queries[i];

    emit "Customer: " + query;
    emit "";

    // Classify intent
    let intent = IntentClassifier.ask(query);

    if (intent == "ESCALATE" || intent == "COMPLAINT") {
      // Handle escalation
      let response = EscalationHandler.ask(query);
      emit "Agent: " + response;
    } else {
      // Normal support response
      let response = SupportAgent.ask(query);
      emit "Agent: " + response;
    }

    emit "";
    emit "---";
    emit "";

    i = i + 1;
  }
}
```

---

### 24. Code Review Assistant

**Difficulty:** Advanced
**Time:** 20 minutes
**What you'll learn:** Multi-aspect code review

```neam
// code_review.neam
// Comprehensive code review assistant

agent SecurityReviewer {
  provider: "openai"
  model: "gpt-4o"
  system: "You are a security expert. Review code for:
- SQL injection vulnerabilities
- XSS vulnerabilities
- Authentication/authorization issues
- Sensitive data exposure
- Input validation problems

Format: List each issue with severity (HIGH/MEDIUM/LOW) and suggested fix."
}

agent PerformanceReviewer {
  provider: "openai"
  model: "gpt-4o"
  system: "You are a performance expert. Review code for:
- Time complexity issues
- Memory leaks
- Unnecessary computations
- Database query optimization
- Caching opportunities

Format: List each issue with impact and suggested optimization."
}

agent StyleReviewer {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You review code style and maintainability:
- Naming conventions
- Code organization
- Documentation quality
- DRY violations
- SOLID principles

Format: List suggestions for improvement."
}

agent ReviewSummarizer {
  provider: "openai"
  model: "gpt-4o"
  system: "Summarize code review findings. Prioritize by impact and provide an overall quality score (1-10)."
}

{
  let code = "
function getUserData(userId) {
  const query = 'SELECT * FROM users WHERE id = ' + userId;
  const result = db.execute(query);
  const allData = [];
  for (let i = 0; i < result.length; i++) {
    for (let j = 0; j < result.length; j++) {
      allData.push(result[i]);
    }
  }
  return allData;
}
";

  emit "=== Code Review Assistant ===";
  emit "";
  emit "Code to review:";
  emit code;
  emit "";

  // Security review
  emit "--- Security Review ---";
  let security = SecurityReviewer.ask("Review this code:\n" + code);
  emit security;
  emit "";

  // Performance review
  emit "--- Performance Review ---";
  let performance = PerformanceReviewer.ask("Review this code:\n" + code);
  emit performance;
  emit "";

  // Style review
  emit "--- Style Review ---";
  let style = StyleReviewer.ask("Review this code:\n" + code);
  emit style;
  emit "";

  // Summary
  emit "--- Summary ---";
  let summary = ReviewSummarizer.ask("Summarize these reviews:\n\nSecurity:\n" + security + "\n\nPerformance:\n" + performance + "\n\nStyle:\n" + style);
  emit summary;
}
```

---

### 25. Research Agent

**Difficulty:** Advanced
**Time:** 25 minutes
**What you'll learn:** Complete research workflow with multiple agents

```neam
// research_agent.neam
// Comprehensive research assistant

knowledge ResearchPapers {
  vector_store: "usearch"
  embedding_model: "nomic-embed-text"
  chunk_size: 500
  chunk_overlap: 100

  sources: [
    { "type": "file", "path": "./papers/*.md" },
    { "type": "file", "path": "./papers/*.pdf" }
  ]

  retrieval_strategy: "agentic"
  top_k: 5
  max_iterations: 3
}

agent QueryDecomposer {
  provider: "openai"
  model: "gpt-4o"
  system: "Break complex research questions into specific, searchable sub-questions. Output numbered list of 3-5 focused questions."
}

agent LiteratureSearcher {
  provider: "openai"
  model: "gpt-4o"
  system: "You search and summarize academic literature. For each finding, note the source and key insights."
  connected_knowledge: [ResearchPapers]
}

agent AnalystAgent {
  provider: "openai"
  model: "gpt-4o"
  system: "You analyze research findings. Identify patterns, contradictions, and gaps. Be critical and thorough."
}

agent CitationAgent {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "Format findings with proper academic citations. Use APA style."
}

agent ReportWriter {
  provider: "openai"
  model: "gpt-4o"
  system: "Write comprehensive research reports. Structure: Executive Summary, Introduction, Methodology, Findings, Analysis, Conclusion, References."
}

{
  let research_question = "What are the current best practices for fine-tuning large language models?";

  emit "=== Research Agent ===";
  emit "";
  emit "Research Question: " + research_question;
  emit "";

  // Step 1: Decompose question
  emit "--- Step 1: Question Decomposition ---";
  let sub_questions = QueryDecomposer.ask(research_question);
  emit sub_questions;
  emit "";

  // Step 2: Literature search
  emit "--- Step 2: Literature Search ---";
  let findings = LiteratureSearcher.ask("Research: " + research_question + "\n\nSub-questions to address:\n" + sub_questions);
  emit findings;
  emit "";

  // Step 3: Analysis
  emit "--- Step 3: Analysis ---";
  let analysis = AnalystAgent.ask("Analyze these findings:\n" + findings);
  emit analysis;
  emit "";

  // Step 4: Format citations
  emit "--- Step 4: Citations ---";
  let citations = CitationAgent.ask("Format these findings with citations:\n" + findings);
  emit citations;
  emit "";

  // Step 5: Write report
  emit "--- Step 5: Final Report ---";
  let report = ReportWriter.ask("Write a research report on: " + research_question + "\n\nFindings:\n" + findings + "\n\nAnalysis:\n" + analysis + "\n\nCitations:\n" + citations);
  emit report;
}
```

---

## Quick Reference

### Agent Declaration

```neam
agent AgentName {
  provider: "openai"         // Required: openai, ollama
  model: "gpt-4o-mini"       // Required: model name
  system: "System prompt"    // Required: agent personality
  temperature: 0.7           // Optional: 0.0-1.0
  connected_knowledge: [KB]  // Optional: RAG connection
}
```

### Knowledge Declaration

```neam
knowledge KBName {
  vector_store: "usearch"
  embedding_model: "nomic-embed-text"
  chunk_size: 500
  chunk_overlap: 50
  sources: [{ "type": "file", "path": "./docs/*.md" }]
  retrieval_strategy: "basic"  // basic, mmr, hybrid, hyde, self_rag
  top_k: 3
}
```

### Common Patterns

| Pattern | Use Case |
|---------|----------|
| Single Agent | Simple Q&A |
| Multi-Agent | Content pipelines |
| Supervisor/Worker | Quality control |
| Router | Query classification |
| Debate | Balanced analysis |
| DeepSearch | Research tasks |
| ReAct | Complex reasoning |
| Self-Reflection | Quality improvement |

---

## Next Steps

1. **Try the examples**: Copy and run each example
2. **Modify them**: Change prompts, add agents, experiment
3. **Build something**: Create your own multi-agent application
4. **Share**: Publish your agents as packages

**Resources:**
- [Full Documentation](/docs)
- [API Reference](/docs/api)
- [Package Registry](https://registry.neam.dev)
- [Community Discord](https://discord.gg/neam)

---

*Happy coding with Neam!*
