# Neam.dev Landing Page - Implementation Specification

## Building with Lovable.dev

A comprehensive guide to creating the official Neam programming language website using Lovable.

---

## Table of Contents

1. [Design Philosophy](#design-philosophy)
2. [Site Structure](#site-structure)
3. [Page Specifications](#page-specifications)
4. [Component Library](#component-library)
5. [Content & Copy](#content--copy)
6. [Code Examples](#code-examples)
7. [Lovable Prompts](#lovable-prompts)
8. [Technical Setup](#technical-setup)

---

## 1. Why Neam Exists (Core Positioning)

> **"Modern AI systems can reason — but they cannot be programmed."**

### The Problem

Today we:
- Glue prompts together
- Wrap LLMs in frameworks
- Simulate agents using loops and callbacks

**This works for demos. It breaks for systems.**

Neam exists to make **agentic intelligence a programmable abstraction**.

### The Shift

| Today | With Neam |
|-------|-----------|
| Prompts as strings | Agents as first-class constructs |
| Implicit behavior | Explicit agent state & goals |
| Fragile orchestration | Structured execution model |
| Model-centric | Agent-centric |
| Tool chaos | Typed, governed capabilities |
| Hard to reason about | Designed for observability |

### What Neam Is NOT

- ❌ A framework
- ❌ A wrapper over Python
- ❌ A prompt library

### What Neam IS

- ✅ A **programming language**
- ✅ With an **agent-native runtime**
- ✅ Designed for **neural + symbolic computation**

**Neam programs behavior, not just outputs.**

### Who Neam Is For

| Audience | Use Case |
|----------|----------|
| **Engineers** | Building autonomous systems |
| **Teams** | Designing multi-agent workflows |
| **Edge developers** | Cyber-physical AI applications |
| **Researchers** | Real execution models |
| **Enterprises** | Governed autonomy |

### One-Line Definition

> **Neam is a Neural-Empowered Agentic Machine — a programming language for building autonomous, intelligent systems.**

---

## 2. Design Principles (The Rules)

These are **non-negotiable**.

| # | Principle | Meaning |
|---|-----------|---------|
| 1 | **Agents Are First-Class** | Everything starts with an agent |
| 2 | **Intelligence Is Pluggable** | LLMs, SLMs, symbolic engines — all swappable |
| 3 | **Execution Is Explicit** | Thinking ≠ Acting. Plans must be executable |
| 4 | **State Is Owned** | No hidden globals. Memory is scoped & inspectable |
| 5 | **Determinism Where Possible** | Randomness is opt-in, never accidental |
| 6 | **Tools Are Capabilities** | Agents cannot call what they're not allowed to |
| 7 | **Failure Is First-Class** | Errors, uncertainty, retries are modeled |
| 8 | **Human-Readable** | If humans can't understand it, it's a bug |
| 9 | **Runtime-Aware** | Edge, cloud, constrained environments are targets |
| 10 | **Ethics Are Declared** | Autonomy without boundaries is disallowed |

---

## 3. The Canonical Example (Hello Agent)

This is **the most important code example** — the one that makes people stop and look.

```neam
agent ResearchAgent {
    goal: "Answer questions using verified knowledge"

    memory {
        short_term
        long_term
    }

    tools {
        search
        summarize
    }

    plan {
        think
        verify
        respond
    }

    behavior {
        think(input) {
            insights = search(input)
            memory.short_term.store(insights)
        }

        verify() {
            trusted = memory.short_term.filter(confidence > 0.8)
            return trusted
        }

        respond() {
            answer = summarize(verify())
            return answer
        }
    }
}
```

### Why This Matters

| Aspect | Traditional | Neam |
|--------|-------------|------|
| Agent definition | Function with prompts | First-class construct |
| Behavior | Hidden in code | Structured & readable |
| Tools | Free-for-all | Governed & declared |
| Memory | Global state | Explicit & scoped |
| Execution | Black box | Observable & debuggable |

**This alone will make people stop and look.**

---

## 4. Visual Design Philosophy

### Inspiration from Popular Languages

| Language | Key Design Elements | What to Adopt |
|----------|---------------------|---------------|
| **Go** (go.dev) | Minimal, fast, playground | Simplicity, try-it feature |
| **Rust** (rust-lang.org) | Bold, confident, code-first | Strong value proposition |
| **Python** (python.org) | Friendly, accessible | Getting started focus |
| **Deno** (deno.land) | Modern, dark, clean | Dark mode, modern aesthetics |
| **Swift** (swift.org) | Elegant, Apple-style | Premium feel |

### Neam Design Principles

1. **AI-First Visual Identity** - Purple/blue gradients suggesting AI/neural networks
2. **Code is Hero** - Show code prominently, let it speak for itself
3. **Simplicity Sells** - Side-by-side comparisons with traditional approaches
4. **Interactive** - Playground to try Neam without installing
5. **Progressive Disclosure** - Simple first, advanced when needed

### Color Palette

```css
:root {
  /* Primary - AI Purple */
  --primary-50: #f5f3ff;
  --primary-100: #ede9fe;
  --primary-500: #8b5cf6;
  --primary-600: #7c3aed;
  --primary-700: #6d28d9;

  /* Secondary - Neural Blue */
  --secondary-500: #3b82f6;
  --secondary-600: #2563eb;

  /* Accent - Success Green */
  --accent-500: #10b981;

  /* Neutral - Slate */
  --slate-50: #f8fafc;
  --slate-900: #0f172a;
  --slate-950: #020617;

  /* Code Background */
  --code-bg: #1e1e2e;  /* Catppuccin Mocha */
}
```

### Typography

```css
/* Headings */
font-family: 'Inter', 'SF Pro Display', system-ui, sans-serif;

/* Code */
font-family: 'JetBrains Mono', 'Fira Code', 'Consolas', monospace;

/* Body */
font-family: 'Inter', system-ui, sans-serif;
```

---

## 2. Site Structure

### Navigation

```
neam.dev
├── / (Homepage)
├── /learn
│   ├── /learn/getting-started
│   ├── /learn/tutorial
│   ├── /learn/examples
│   └── /learn/by-example
├── /docs
│   ├── /docs/language
│   ├── /docs/agents
│   ├── /docs/knowledge
│   ├── /docs/patterns
│   └── /docs/api
├── /packages (→ registry.neam.dev)
├── /playground
├── /community
│   ├── /community/discord
│   ├── /community/github
│   └── /community/showcase
├── /blog
└── /about
```

### Primary Navigation Bar

```
┌──────────────────────────────────────────────────────────────────────┐
│  [Neam Logo]    Learn ▼    Docs ▼    Packages    Playground    Blog  │
│                                                    [GitHub] [Get Started] │
└──────────────────────────────────────────────────────────────────────┘
```

---

## 3. Page Specifications

### 3.1 Homepage (neam.dev)

#### Section 1: Hero (The Hook)

```
┌──────────────────────────────────────────────────────────────────────────┐
│                                                                          │
│                              [Neam Logo]                                 │
│                                                                          │
│         "Modern AI systems can reason — but they cannot be programmed."  │
│                                                                          │
│                    Until now.                                            │
│                                                                          │
│              NEAM — The Language for Agentic Intelligence                │
│                                                                          │
│     Program behavior, not just outputs. Agents as first-class citizens.  │
│                                                                          │
│         [Get Started]  [Try Playground]  [Star on GitHub ⭐]             │
│                                                                          │
└──────────────────────────────────────────────────────────────────────────┘
```

#### Section 2: The Canonical Example (Show Don't Tell)

```
┌──────────────────────────────────────────────────────────────────────────┐
│                                                                          │
│                    This is what programming agents looks like            │
│                                                                          │
│  ┌────────────────────────────────────────────────────────────────────┐  │
│  │  agent ResearchAgent {                                              │  │
│  │      goal: "Answer questions using verified knowledge"              │  │
│  │                                                                     │  │
│  │      memory {                                                       │  │
│  │          short_term                                                 │  │
│  │          long_term                                                  │  │
│  │      }                                                              │  │
│  │                                                                     │  │
│  │      tools {                                                        │  │
│  │          search                                                     │  │
│  │          summarize                                                  │  │
│  │      }                                                              │  │
│  │                                                                     │  │
│  │      behavior {                                                     │  │
│  │          think(input) {                                             │  │
│  │              insights = search(input)                               │  │
│  │              memory.short_term.store(insights)                      │  │
│  │          }                                                          │  │
│  │                                                                     │  │
│  │          verify() {                                                 │  │
│  │              trusted = memory.short_term.filter(confidence > 0.8)   │  │
│  │              return trusted                                         │  │
│  │          }                                                          │  │
│  │                                                                     │  │
│  │          respond() {                                                │  │
│  │              answer = summarize(verify())                           │  │
│  │              return answer                                          │  │
│  │          }                                                          │  │
│  │      }                                                              │  │
│  │  }                                                                  │  │
│  └────────────────────────────────────────────────────────────────────┘  │
│                                                                          │
│      ✓ Agent with explicit goal        ✓ Governed tool access           │
│      ✓ Structured memory               ✓ Observable behavior            │
│      ✓ Readable execution              ✓ No hidden state                │
│                                                                          │
└──────────────────────────────────────────────────────────────────────────┘
```

#### Section 3: The Problem (Why This Matters)

```
┌──────────────────────────────────────────────────────────────────────────┐
│                                                                          │
│                      The way we build AI today is broken                 │
│                                                                          │
│                                                                          │
│      Today we...                          This works for demos.          │
│      ───────────                          It breaks for systems.         │
│      • Glue prompts together                                             │
│      • Wrap LLMs in frameworks                                           │
│      • Simulate agents with loops                                        │
│                                                                          │
│  ─────────────────────────────────────────────────────────────────────── │
│                                                                          │
│         TODAY                                    WITH NEAM               │
│    ┌─────────────────────┐              ┌─────────────────────┐         │
│    │ Prompts as strings  │      →       │ Agents as constructs│         │
│    │ Implicit behavior   │      →       │ Explicit state/goals│         │
│    │ Fragile orchestrate │      →       │ Structured execution│         │
│    │ Model-centric       │      →       │ Agent-centric       │         │
│    │ Tool chaos          │      →       │ Governed capabilities│        │
│    │ Hard to debug       │      →       │ Observable by design│         │
│    └─────────────────────┘              └─────────────────────┘         │
│                                                                          │
│                       [Read the Manifesto →]                             │
│                                                                          │
└──────────────────────────────────────────────────────────────────────────┘
```

#### Section 4: What Neam Is (And Isn't)

```
┌──────────────────────────────────────────────────────────────────────────┐
│                                                                          │
│                            Neam is different.                            │
│                                                                          │
│      ┌─────────────────────────────────────────────────────────────┐    │
│      │                                                              │    │
│      │   Neam is NOT                    Neam IS                     │    │
│      │   ───────────                    ────────                    │    │
│      │   ❌ A framework                 ✅ A programming language   │    │
│      │   ❌ A Python wrapper            ✅ Agent-native runtime     │    │
│      │   ❌ A prompt library            ✅ Neural + symbolic engine │    │
│      │                                                              │    │
│      └─────────────────────────────────────────────────────────────┘    │
│                                                                          │
│              Neam programs behavior, not just outputs.                   │
│                                                                          │
└──────────────────────────────────────────────────────────────────────────┘
```

#### Section 5: Why Neam? (Value Proposition)

```
┌──────────────────────────────────────────────────────────────────────────┐
│                         Why developers choose Neam                        │
│                                                                          │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐          │
│  │  🚀 10x Faster  │  │  🧠 AI-Native   │  │  📦 Batteries   │          │
│  │                 │  │                 │  │    Included     │          │
│  │  Write AI apps  │  │  Agents, RAG,   │  │                 │          │
│  │  in 10 lines    │  │  tools built    │  │  Vector store,  │          │
│  │  not 100        │  │  into language  │  │  embeddings,    │          │
│  │                 │  │                 │  │  LLM adapters   │          │
│  └─────────────────┘  └─────────────────┘  └─────────────────┘          │
│                                                                          │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐          │
│  │  🔄 Multi-Agent │  │  🔌 Any LLM     │  │  ⚡ Compiled    │          │
│  │                 │  │                 │  │                 │          │
│  │  Orchestrate    │  │  OpenAI, Ollama │  │  Native C++     │          │
│  │  complex agent  │  │  Claude, local  │  │  performance    │          │
│  │  workflows      │  │  models         │  │  + WASM target  │          │
│  └─────────────────┘  └─────────────────┘  └─────────────────┘          │
│                                                                          │
└──────────────────────────────────────────────────────────────────────────┘
```

#### Section 3: Code Comparison (The Money Shot)

```
┌──────────────────────────────────────────────────────────────────────────┐
│                     See the difference for yourself                       │
│                                                                          │
│     [Traditional Approach]              [Neam]                             │
│                                                                          │
│  ┌───────────────────────────┐    ┌───────────────────────────┐         │
│  │ # Import dependencies     │    │ agent Coder {             │         │
│  │ from ai_framework import  │    │   provider: "openai"      │         │
│  │   LLM, Prompt, Chain      │    │   model: "gpt-4o"         │         │
│  │                           │    │   system: "Expert coder"  │         │
│  │ # Initialize model        │    │ }                         │         │
│  │ llm = LLM(                │    │                           │         │
│  │   model="gpt-4o",         │    │ {                         │         │
│  │   temperature=0           │    │   let code = Coder.ask(   │         │
│  │ )                         │    │     "Write quicksort"     │         │
│  │                           │    │   );                      │         │
│  │ # Build prompt template   │    │   emit code;              │         │
│  │ prompt = Prompt(          │    │ }                         │         │
│  │   system="Expert coder",  │    │                           │         │
│  │   human="{input}"         │    │                           │         │
│  │ )                         │    │                           │         │
│  │                           │    │                           │         │
│  │ # Create chain & run      │    │                           │         │
│  │ chain = prompt | llm      │    │                           │         │
│  │ result = chain.run(       │    │                           │         │
│  │   input="Write quicksort" │    │                           │         │
│  │ )                         │    │                           │         │
│  │ print(result)             │    │                           │         │
│  └───────────────────────────┘    └───────────────────────────┘         │
│                                                                          │
│      23 lines, 3 imports              8 lines, zero imports              │
│                                                                          │
└──────────────────────────────────────────────────────────────────────────┘
```

#### Section 4: Features Showcase (Tabs)

```
┌──────────────────────────────────────────────────────────────────────────┐
│                           What you can build                              │
│                                                                          │
│    [Agents]  [RAG/Knowledge]  [Multi-Agent]  [Tools]  [API Server]       │
│    ────────                                                              │
│                                                                          │
│  ┌────────────────────────────────────────────────────────────────────┐  │
│  │                                                                     │  │
│  │  // Define an agent in 5 lines                                      │  │
│  │  agent CodeReviewer {                                               │  │
│  │    provider: "openai"                                               │  │
│  │    model: "gpt-4o"                                                  │  │
│  │    system: "You review code for bugs, security issues, and style." │  │
│  │    temperature: 0.3                                                 │  │
│  │  }                                                                  │  │
│  │                                                                     │  │
│  │  {                                                                  │  │
│  │    let review = CodeReviewer.ask("Review this: function add(a,b)   │  │
│  │      { return a + b; }");                                           │  │
│  │    emit review;                                                     │  │
│  │  }                                                                  │  │
│  │                                                                     │  │
│  └────────────────────────────────────────────────────────────────────┘  │
│                                                                          │
│  Agents are first-class citizens in Neam. Define once, use anywhere.     │
│  Switch providers (OpenAI → Ollama) with one line change.                │
│                                                                          │
│                          [Learn more about Agents →]                      │
│                                                                          │
└──────────────────────────────────────────────────────────────────────────┘
```

#### Section 5: Quick Start

```
┌──────────────────────────────────────────────────────────────────────────┐
│                        Get started in 60 seconds                          │
│                                                                          │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │  # Install Neam                                                   │   │
│  │  curl -fsSL https://neam.dev/install.sh | sh                      │   │
│  │                                                                   │   │
│  │  # Create your first agent                                        │   │
│  │  neam-pkg init my-agent                                           │   │
│  │  cd my-agent                                                      │   │
│  │                                                                   │   │
│  │  # Set your API key                                               │   │
│  │  export OPENAI_API_KEY="sk-..."                                   │   │
│  │                                                                   │   │
│  │  # Build and run                                                  │   │
│  │  neamc src/main.neam -o build/main.neamb                          │   │
│  │  neam build/main.neamb                                            │   │
│  └──────────────────────────────────────────────────────────────────┘   │
│                                                                          │
│   [macOS]  [Linux]  [Windows]  [Docker]                                  │
│                                                                          │
│                    [Full Installation Guide →]                            │
│                                                                          │
└──────────────────────────────────────────────────────────────────────────┘
```

#### Section 6: Ecosystem

```
┌──────────────────────────────────────────────────────────────────────────┐
│                          The Neam Ecosystem                               │
│                                                                          │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐          │
│  │   📦 Packages   │  │   📖 Docs       │  │   🎮 Playground │          │
│  │                 │  │                 │  │                 │          │
│  │  Browse 200+    │  │  Comprehensive  │  │  Try Neam in    │          │
│  │  community      │  │  guides and     │  │  your browser   │          │
│  │  packages       │  │  API reference  │  │  instantly      │          │
│  │                 │  │                 │  │                 │          │
│  │  [Explore →]    │  │  [Read →]       │  │  [Launch →]     │          │
│  └─────────────────┘  └─────────────────┘  └─────────────────┘          │
│                                                                          │
│  ┌─────────────────┐  ┌─────────────────┐  ┌─────────────────┐          │
│  │   🔧 Tools      │  │   💬 Community  │  │   🎓 Learn      │          │
│  │                 │  │                 │  │                 │          │
│  │  VS Code, LSP,  │  │  Discord, GitHub│  │  Tutorials,     │          │
│  │  Debugger       │  │  Discussions    │  │  Examples       │          │
│  │                 │  │                 │  │                 │          │
│  │  [Get Tools →]  │  │  [Join →]       │  │  [Start →]      │          │
│  └─────────────────┘  └─────────────────┘  └─────────────────┘          │
│                                                                          │
└──────────────────────────────────────────────────────────────────────────┘
```

#### Section 7: Testimonials / Use Cases

```
┌──────────────────────────────────────────────────────────────────────────┐
│                        Built with Neam                                    │
│                                                                          │
│  ┌────────────────────────────────────────────────────────────────────┐  │
│  │  "We replaced 2000 lines of framework code with 150 lines of       │  │
│  │   Neam. Our AI pipeline went from taking 3 weeks to build to       │  │
│  │   just 2 days."                                                    │  │
│  │                                                                     │  │
│  │   — Sarah Chen, CTO @ AIStartup                                    │  │
│  └────────────────────────────────────────────────────────────────────┘  │
│                                                                          │
│  Use Case Cards:                                                         │
│  ┌───────────────┐ ┌───────────────┐ ┌───────────────┐ ┌──────────────┐ │
│  │ Customer      │ │ Code          │ │ Research      │ │ Data         │ │
│  │ Support Bot   │ │ Assistant     │ │ Agent         │ │ Pipeline     │ │
│  │               │ │               │ │               │ │              │ │
│  │ RAG + Agent   │ │ Multi-agent   │ │ DeepSearch    │ │ Sequential   │ │
│  └───────────────┘ └───────────────┘ └───────────────┘ └──────────────┘ │
│                                                                          │
└──────────────────────────────────────────────────────────────────────────┘
```

#### Section 8: Footer

```
┌──────────────────────────────────────────────────────────────────────────┐
│                                                                          │
│  [Neam Logo]                                                             │
│                                                                          │
│  Get Started        Documentation       Community        Company         │
│  ─────────────      ─────────────       ─────────        ────────        │
│  Installation       Language Guide      GitHub           About           │
│  Quick Start        Agent Reference     Discord          Blog            │
│  First Agent        Knowledge/RAG       Twitter          Careers         │
│  Tutorials          Patterns            YouTube          Contact         │
│                     API Server          Showcase                         │
│                                                                          │
│  ───────────────────────────────────────────────────────────────────────│
│                                                                          │
│  [GitHub]  [Discord]  [Twitter]  [YouTube]                               │
│                                                                          │
│  © 2024 Neam Project. Open source under MIT License.                     │
│                                                                          │
└──────────────────────────────────────────────────────────────────────────┘
```

---

### 3.2 Learn / Getting Started Page

```
┌──────────────────────────────────────────────────────────────────────────┐
│                                                                          │
│  Getting Started with Neam                                               │
│  ═══════════════════════════                                             │
│                                                                          │
│  Learn the basics and build your first AI agent in under 5 minutes.      │
│                                                                          │
│  ┌─────────────────────────────────────────────────────────────────────┐ │
│  │  Table of Contents                                                   │ │
│  │  1. Installation                                                     │ │
│  │  2. Your First Agent                                                 │ │
│  │  3. Adding Knowledge (RAG)                                           │ │
│  │  4. Multi-Agent Patterns                                             │ │
│  │  5. Deploying Your Agent                                             │ │
│  └─────────────────────────────────────────────────────────────────────┘ │
│                                                                          │
│  ─────────────────────────────────────────────────────────────────────── │
│                                                                          │
│  ## 1. Installation                                                      │
│                                                                          │
│  ### macOS / Linux                                                       │
│  ┌─────────────────────────────────────────────────────────────────────┐ │
│  │  curl -fsSL https://neam.dev/install.sh | sh                         │ │
│  └─────────────────────────────────────────────────────────────────────┘ │
│                                                                          │
│  ### From Source                                                         │
│  ┌─────────────────────────────────────────────────────────────────────┐ │
│  │  git clone https://github.com/neam-lang/neam                         │ │
│  │  cd neam && mkdir build && cd build                                  │ │
│  │  cmake .. -DCMAKE_BUILD_TYPE=Release                                 │ │
│  │  cmake --build . --parallel                                          │ │
│  └─────────────────────────────────────────────────────────────────────┘ │
│                                                                          │
│  ### Verify Installation                                                 │
│  ┌─────────────────────────────────────────────────────────────────────┐ │
│  │  $ neamc --version                                                   │ │
│  │  neamc 1.0.0                                                         │ │
│  │                                                                      │ │
│  │  $ neam --version                                                    │ │
│  │  neam 1.0.0                                                          │ │
│  └─────────────────────────────────────────────────────────────────────┘ │
│                                                                          │
│  ─────────────────────────────────────────────────────────────────────── │
│                                                                          │
│  ## 2. Your First Agent                                                  │
│                                                                          │
│  Create a file called `hello.neam`:                                      │
│                                                                          │
│  ┌─────────────────────────────────────────────────────────────────────┐ │
│  │  // hello.neam - Your first Neam agent                               │ │
│  │                                                                      │ │
│  │  agent Greeter {                                                     │ │
│  │    provider: "openai"                                                │ │
│  │    model: "gpt-4o-mini"                                              │ │
│  │    system: "You are a friendly greeter. Be warm and welcoming."      │ │
│  │  }                                                                   │ │
│  │                                                                      │ │
│  │  {                                                                   │ │
│  │    let greeting = Greeter.ask("Say hello to a new Neam developer!"); │ │
│  │    emit greeting;                                                    │ │
│  │  }                                                                   │ │
│  └─────────────────────────────────────────────────────────────────────┘ │
│                                                                          │
│  Set your API key and run:                                               │
│                                                                          │
│  ┌─────────────────────────────────────────────────────────────────────┐ │
│  │  export OPENAI_API_KEY="sk-..."                                      │ │
│  │  neamc hello.neam -o hello.neamb                                     │ │
│  │  neam hello.neamb                                                    │ │
│  └─────────────────────────────────────────────────────────────────────┘ │
│                                                                          │
│  Output:                                                                 │
│  ┌─────────────────────────────────────────────────────────────────────┐ │
│  │  Hello, and welcome to the wonderful world of Neam! 🎉               │ │
│  │  I'm thrilled to have you here as a new developer...                │ │
│  └─────────────────────────────────────────────────────────────────────┘ │
│                                                                          │
│                           [Next: Adding Knowledge →]                      │
│                                                                          │
└──────────────────────────────────────────────────────────────────────────┘
```

---

### 3.3 Examples Page (Learn by Example)

```
┌──────────────────────────────────────────────────────────────────────────┐
│                                                                          │
│  Neam by Example                                                         │
│  ═══════════════                                                         │
│                                                                          │
│  Learn Neam through annotated examples. Click any example to see         │
│  the full code and explanation.                                          │
│                                                                          │
│  ┌──────────────────────────────────────────────────────────────────┐   │
│  │  [Filter: All] [Agents] [RAG] [Multi-Agent] [Patterns] [Tools]    │   │
│  └──────────────────────────────────────────────────────────────────┘   │
│                                                                          │
│  ─────────────────────────────────────────────────────────────────────── │
│                                                                          │
│  BASICS                                                                  │
│                                                                          │
│  ┌─────────────────────┐  ┌─────────────────────┐  ┌─────────────────┐  │
│  │ Hello World         │  │ Variables           │  │ Control Flow    │  │
│  │ ───────────         │  │ ─────────           │  │ ────────────    │  │
│  │ emit "Hello!";      │  │ let x = 42;         │  │ if/else, while  │  │
│  │                     │  │ let s = "text";     │  │ for loops       │  │
│  │ [Beginner]          │  │ [Beginner]          │  │ [Beginner]      │  │
│  └─────────────────────┘  └─────────────────────┘  └─────────────────┘  │
│                                                                          │
│  AGENTS                                                                  │
│                                                                          │
│  ┌─────────────────────┐  ┌─────────────────────┐  ┌─────────────────┐  │
│  │ Simple Agent        │  │ Custom System       │  │ Temperature     │  │
│  │ ────────────        │  │ Prompt              │  │ Control         │  │
│  │ Basic Q&A agent     │  │ ──────────────      │  │ ───────────     │  │
│  │ with OpenAI         │  │ Customize agent     │  │ Creative vs     │  │
│  │                     │  │ personality         │  │ precise output  │  │
│  │ [Beginner]          │  │ [Beginner]          │  │ [Beginner]      │  │
│  └─────────────────────┘  └─────────────────────┘  └─────────────────┘  │
│                                                                          │
│  ┌─────────────────────┐  ┌─────────────────────┐  ┌─────────────────┐  │
│  │ Local LLM           │  │ Streaming           │  │ Error Handling  │  │
│  │ (Ollama)            │  │ Responses           │  │                 │  │
│  │ ─────────           │  │ ─────────           │  │ ─────────────   │  │
│  │ Use local models    │  │ Real-time output    │  │ Graceful        │  │
│  │ for privacy         │  │ from agents         │  │ failure modes   │  │
│  │                     │  │                     │  │                 │  │
│  │ [Intermediate]      │  │ [Intermediate]      │  │ [Intermediate]  │  │
│  └─────────────────────┘  └─────────────────────┘  └─────────────────┘  │
│                                                                          │
│  KNOWLEDGE (RAG)                                                         │
│                                                                          │
│  ┌─────────────────────┐  ┌─────────────────────┐  ┌─────────────────┐  │
│  │ Basic RAG           │  │ File Sources        │  │ Web Sources     │  │
│  │ ─────────           │  │ ────────────        │  │ ───────────     │  │
│  │ Add docs to agent   │  │ Index markdown,     │  │ Ingest web      │  │
│  │ context             │  │ PDFs, code          │  │ pages           │  │
│  │                     │  │                     │  │                 │  │
│  │ [Beginner]          │  │ [Beginner]          │  │ [Intermediate]  │  │
│  └─────────────────────┘  └─────────────────────┘  └─────────────────┘  │
│                                                                          │
│  ┌─────────────────────┐  ┌─────────────────────┐  ┌─────────────────┐  │
│  │ HyDE Strategy       │  │ Self-RAG            │  │ Agentic RAG     │  │
│  │ ─────────────       │  │ ────────            │  │ ───────────     │  │
│  │ Hypothetical doc    │  │ Self-reflective     │  │ Iterative       │  │
│  │ embeddings          │  │ retrieval           │  │ refinement      │  │
│  │                     │  │                     │  │                 │  │
│  │ [Advanced]          │  │ [Advanced]          │  │ [Advanced]      │  │
│  └─────────────────────┘  └─────────────────────┘  └─────────────────┘  │
│                                                                          │
│  MULTI-AGENT PATTERNS                                                    │
│                                                                          │
│  ┌─────────────────────┐  ┌─────────────────────┐  ┌─────────────────┐  │
│  │ Researcher →        │  │ Supervisor /        │  │ Router /        │  │
│  │ Writer → Editor     │  │ Worker              │  │ Dispatcher      │  │
│  │ ────────────────    │  │ ──────────────      │  │ ────────────    │  │
│  │ Content pipeline    │  │ Quality control     │  │ Query routing   │  │
│  │                     │  │ pattern             │  │ to experts      │  │
│  │ [Intermediate]      │  │ [Intermediate]      │  │ [Intermediate]  │  │
│  └─────────────────────┘  └─────────────────────┘  └─────────────────┘  │
│                                                                          │
│  ┌─────────────────────┐  ┌─────────────────────┐  ┌─────────────────┐  │
│  │ Debate Pattern      │  │ DeepSearch          │  │ ReAct Agent     │  │
│  │ ──────────────      │  │ ──────────          │  │ ───────────     │  │
│  │ Pro vs Con          │  │ Plan → Research     │  │ Thought →       │  │
│  │ with Judge          │  │ → Synthesize        │  │ Action → Obs    │  │
│  │                     │  │                     │  │                 │  │
│  │ [Advanced]          │  │ [Advanced]          │  │ [Advanced]      │  │
│  └─────────────────────┘  └─────────────────────┘  └─────────────────┘  │
│                                                                          │
└──────────────────────────────────────────────────────────────────────────┘
```

---

### 3.4 Playground Page

```
┌──────────────────────────────────────────────────────────────────────────┐
│                                                                          │
│  Neam Playground                                         [Share] [Reset] │
│  ═══════════════                                                         │
│                                                                          │
│  ┌────────────────────────────────┐  ┌────────────────────────────────┐ │
│  │  Examples ▼                     │  │  Provider: [OpenAI ▼]          │ │
│  │  ─────────                      │  │  API Key:  [••••••••••••]      │ │
│  │  • Hello World                  │  │                                │ │
│  │  • Simple Q&A                   │  │  [Use Demo Key] (limited)      │ │
│  │  • RAG Example                  │  │                                │ │
│  │  • Multi-Agent                  │  └────────────────────────────────┘ │
│  └────────────────────────────────┘                                      │
│                                                                          │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │  1 │ agent Helper {                                                │  │
│  │  2 │   provider: "openai"                                          │  │
│  │  3 │   model: "gpt-4o-mini"                                        │  │
│  │  4 │   system: "You are a helpful assistant."                      │  │
│  │  5 │ }                                                             │  │
│  │  6 │                                                               │  │
│  │  7 │ {                                                             │  │
│  │  8 │   let answer = Helper.ask("What is 2+2?");                    │  │
│  │  9 │   emit answer;                                                │  │
│  │ 10 │ }                                                             │  │
│  │    │                                                               │  │
│  └───────────────────────────────────────────────────────────────────┘  │
│                                                                          │
│                              [▶ Run]                                     │
│                                                                          │
│  Output                                                                  │
│  ┌───────────────────────────────────────────────────────────────────┐  │
│  │  Compiling...                                                      │  │
│  │  Running...                                                        │  │
│  │  ────────────────────────────────────────────────────              │  │
│  │  2 + 2 equals 4.                                                   │  │
│  │  ────────────────────────────────────────────────────              │  │
│  │  ✓ Completed in 1.2s                                               │  │
│  └───────────────────────────────────────────────────────────────────┘  │
│                                                                          │
└──────────────────────────────────────────────────────────────────────────┘
```

---

### 3.5 Packages Page (registry.neam.dev)

See `docs/REGISTRY_IMPLEMENTATION.md` for full specification.

---

### 3.6 Philosophy / Manifesto Page (/about/philosophy)

This is the **"why we exist"** page — for developers who want to understand the vision.

```
┌──────────────────────────────────────────────────────────────────────────┐
│                                                                          │
│  The Neam Manifesto                                                      │
│  ══════════════════                                                      │
│                                                                          │
│  ┌────────────────────────────────────────────────────────────────────┐  │
│  │                                                                     │  │
│  │  "Modern AI systems can reason — but they cannot be programmed."   │  │
│  │                                                                     │  │
│  │                                   — The Neam Project                │  │
│  │                                                                     │  │
│  └────────────────────────────────────────────────────────────────────┘  │
│                                                                          │
│  ─────────────────────────────────────────────────────────────────────── │
│                                                                          │
│  ## The Problem                                                          │
│                                                                          │
│  Today, building AI agents means:                                        │
│                                                                          │
│    • Gluing prompts together with string concatenation                   │
│    • Wrapping LLMs in frameworks that hide complexity                    │
│    • Simulating agents using loops, callbacks, and hope                  │
│                                                                          │
│  This approach works for demos and prototypes.                           │
│  It breaks when you need real systems.                                   │
│                                                                          │
│  The result?                                                             │
│    • Brittle orchestration that fails unpredictably                      │
│    • Implicit behavior that's impossible to debug                        │
│    • Tool chaos where anything can call anything                         │
│    • No observability into what agents are actually doing                │
│                                                                          │
│  ─────────────────────────────────────────────────────────────────────── │
│                                                                          │
│  ## The Vision                                                           │
│                                                                          │
│  Neam exists to make agentic intelligence a programmable abstraction.    │
│                                                                          │
│  We believe:                                                             │
│    • Agents should be first-class language constructs                    │
│    • Behavior should be explicit, not hidden in prompts                  │
│    • Memory should be scoped, inspectable, and governed                  │
│    • Tools should be capabilities, not free-for-all functions            │
│    • Failure should be a first-class outcome, not an exception           │
│                                                                          │
│  ─────────────────────────────────────────────────────────────────────── │
│                                                                          │
│  ## The 10 Principles                                                    │
│                                                                          │
│  ┌──────┬────────────────────────────┬─────────────────────────────────┐ │
│  │  #   │  Principle                 │  What It Means                  │ │
│  ├──────┼────────────────────────────┼─────────────────────────────────┤ │
│  │  1   │  Agents Are First-Class    │  Everything starts with agent   │ │
│  │  2   │  Intelligence Is Pluggable │  LLMs, SLMs are swappable       │ │
│  │  3   │  Execution Is Explicit     │  Thinking ≠ Acting              │ │
│  │  4   │  State Is Owned            │  No hidden globals              │ │
│  │  5   │  Determinism When Possible │  Randomness is opt-in           │ │
│  │  6   │  Tools Are Capabilities    │  Governed, not free-for-all     │ │
│  │  7   │  Failure Is First-Class    │  Errors are modeled             │ │
│  │  8   │  Human-Readable            │  If unreadable, it's a bug      │ │
│  │  9   │  Runtime-Aware             │  Edge, cloud are targets        │ │
│  │  10  │  Ethics Are Declared       │  No unbounded autonomy          │ │
│  └──────┴────────────────────────────┴─────────────────────────────────┘ │
│                                                                          │
│  ─────────────────────────────────────────────────────────────────────── │
│                                                                          │
│  ## One-Line Definition                                                  │
│                                                                          │
│  ┌────────────────────────────────────────────────────────────────────┐  │
│  │                                                                     │  │
│  │  Neam is a Neural-Empowered Agentic Machine — a programming        │  │
│  │  language for building autonomous, intelligent systems.            │  │
│  │                                                                     │  │
│  └────────────────────────────────────────────────────────────────────┘  │
│                                                                          │
│  ─────────────────────────────────────────────────────────────────────── │
│                                                                          │
│  ## Who Neam Is For                                                      │
│                                                                          │
│  ┌─────────────────────┐  ┌─────────────────────┐  ┌─────────────────┐  │
│  │  🔧 Engineers       │  │  👥 Teams           │  │  🏢 Enterprise  │  │
│  │                     │  │                     │  │                 │  │
│  │  Building           │  │  Designing          │  │  Needing        │  │
│  │  autonomous         │  │  multi-agent        │  │  governed       │  │
│  │  systems            │  │  workflows          │  │  autonomy       │  │
│  └─────────────────────┘  └─────────────────────┘  └─────────────────┘  │
│                                                                          │
│  ┌─────────────────────┐  ┌─────────────────────┐                       │
│  │  🔬 Researchers     │  │  📡 Edge Devs       │                       │
│  │                     │  │                     │                       │
│  │  Needing real       │  │  Cyber-physical     │                       │
│  │  execution          │  │  AI applications    │                       │
│  │  models             │  │                     │                       │
│  └─────────────────────┘  └─────────────────────┘                       │
│                                                                          │
│                          [Get Started →]                                 │
│                                                                          │
└──────────────────────────────────────────────────────────────────────────┘
```

---

## 4. Component Library

### Lovable + shadcn/ui Components

| Component | Usage |
|-----------|-------|
| `CodeBlock` | Syntax highlighted code with copy button |
| `TabGroup` | Feature showcase tabs |
| `Card` | Package cards, feature cards |
| `Badge` | Version badges, difficulty levels |
| `Button` | CTAs, actions |
| `Input` | Search, forms |
| `Dialog` | Modals |
| `Dropdown` | Navigation menus |
| `Toast` | Notifications |

### Custom Components to Build

```typescript
// components/CodeComparison.tsx
// Side-by-side code comparison with language labels

// components/FeatureCard.tsx
// Icon + title + description card

// components/ExampleCard.tsx
// Clickable example with preview

// components/InstallCommand.tsx
// Copy-able install command with OS tabs

// components/PlaygroundEditor.tsx
// Monaco-based code editor

// components/SearchBar.tsx
// Global search with keyboard shortcut
```

---

## 5. Content & Copy

### Taglines (A/B Test Options)

1. **"The AI-First Programming Language"** (Primary)
2. "Build AI Agents in Minutes, Not Months"
3. "Where Code Meets Intelligence"
4. "AI Development, Simplified"
5. "The Language AI Agents Deserve"

### Value Props (Keep Under 10 Words Each)

| Feature | Copy |
|---------|------|
| Speed | "10x faster AI development than Python frameworks" |
| Native AI | "Agents, RAG, and tools built into the language" |
| Multi-Agent | "Orchestrate complex AI workflows with ease" |
| Any LLM | "OpenAI, Claude, Ollama - switch with one line" |
| Performance | "Compiled to native code, not interpreted" |
| Batteries | "Vector store, embeddings, HTTP - all included" |

### SEO Meta Tags

```html
<title>Neam - The AI-First Programming Language</title>
<meta name="description" content="Build intelligent AI agents in minutes with Neam. First-class LLM support, built-in RAG, and native multi-agent orchestration. Open source.">
<meta name="keywords" content="AI programming language, LLM, agents, RAG, multi-agent, artificial intelligence, machine learning">
```

---

## 6. Code Examples

### Example 1: Hello World (Simplest)

```neam
// The simplest Neam program
{
  emit "Hello, World!";
}
```

### Example 2: First Agent (5 lines)

```neam
agent Assistant {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You are helpful and concise."
}

{ emit Assistant.ask("What is Neam?"); }
```

### Example 3: RAG in 15 Lines

```neam
// Add knowledge to your agent
knowledge Docs {
  vector_store: "usearch"
  embedding_model: "nomic-embed-text"
  sources: [{ "type": "file", "path": "./docs/*.md" }]
}

agent Expert {
  provider: "openai"
  model: "gpt-4o"
  system: "Answer using the provided documentation."
  connected_knowledge: [Docs]
}

{ emit Expert.ask("How do I deploy my app?"); }
```

### Example 4: Multi-Agent Pipeline

```neam
// Research → Write → Edit pipeline
agent Researcher {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "Research and provide key facts."
}

agent Writer {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "Write polished prose from notes."
}

agent Editor {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "Improve text clarity and style."
}

{
  let facts = Researcher.ask("Key facts about quantum computing");
  let draft = Writer.ask("Write about: " + facts);
  let final = Editor.ask("Edit: " + draft);
  emit final;
}
```

### Example 5: Local LLM (Privacy)

```neam
// Run entirely on your machine
agent LocalAssistant {
  provider: "ollama"
  model: "llama3:8b"
  system: "You are a private, local assistant."
}

{ emit LocalAssistant.ask("Summarize this contract..."); }
```

### Example 6: Comparison - Customer Support Bot

**Traditional Approach (45 lines):**

```python
from ai_framework import LLM, Embeddings, VectorStore
from ai_framework import TextSplitter, RAGChain
from ai_framework import DocumentLoader

# Load documents
loader = DocumentLoader('./docs', pattern="**/*.md")
documents = loader.load()

# Split into chunks
splitter = TextSplitter(
    chunk_size=1000,
    chunk_overlap=200
)
chunks = splitter.split(documents)

# Create embeddings and vector store
embeddings = Embeddings(model="text-embedding")
vectorstore = VectorStore.from_documents(chunks, embeddings)

# Create retriever
retriever = vectorstore.as_retriever(
    search_type="similarity",
    top_k=3
)

# Create LLM
llm = LLM(model="gpt-4o-mini", temperature=0)

# Create RAG chain
rag_chain = RAGChain(
    llm=llm,
    retriever=retriever,
    return_sources=True
)

# Query
result = rag_chain.run("How do I reset my password?")
print(result["answer"])
```

**Neam (15 lines):**

```neam
knowledge SupportDocs {
  vector_store: "usearch"
  embedding_model: "nomic-embed-text"
  chunk_size: 1000
  chunk_overlap: 200
  sources: [{ "type": "file", "path": "./docs/**/*.md" }]
  top_k: 3
}

agent SupportBot {
  provider: "openai"
  model: "gpt-4o-mini"
  system: "You are a helpful support agent. Answer based on documentation."
  connected_knowledge: [SupportDocs]
}

{ emit SupportBot.ask("How do I reset my password?"); }
```

**Result: 3x less code, same functionality.**

---

## 7. Lovable Prompts

### Master Prompt for Project Setup

```
Create a modern programming language website for "Neam" - an AI-first
programming language for building intelligent agents.

Tech stack:
- React + TypeScript
- Tailwind CSS + shadcn/ui
- Dark mode by default with light mode toggle
- Supabase for backend (auth, database)

Design guidelines:
- Modern, clean, professional
- Purple/blue gradient accents (AI theme)
- Code is the hero - show it prominently
- Mobile responsive

Color scheme:
- Primary: Purple (#8b5cf6 to #6d28d9)
- Secondary: Blue (#3b82f6)
- Background: Slate-950 (#020617) for dark, white for light
- Code blocks: Catppuccin Mocha theme

Typography:
- Headings: Inter font
- Code: JetBrains Mono
- Body: Inter

The site should feel like a mix of:
- go.dev (minimal, clean)
- rust-lang.org (bold, confident)
- deno.land (modern, dark)
```

### Homepage Hero Prompt (The Hook)

```
Create a powerful hero section for Neam programming language homepage.

The hook is this quote:
"Modern AI systems can reason — but they cannot be programmed. Until now."

Structure:
1. Neam logo/wordmark at top
2. The hook quote in large, impactful typography
3. "Until now." on its own line, slightly delayed animation
4. Tagline: "NEAM — The Language for Agentic Intelligence"
5. Subtitle: "Program behavior, not just outputs. Agents as first-class citizens."
6. Three CTA buttons:
   - "Get Started" (primary, purple gradient)
   - "Try Playground" (secondary, outline)
   - "Star on GitHub" (ghost with star icon)

Design:
- Dark background with subtle gradient mesh
- Quote text should feel bold and provocative
- "Until now" should have a dramatic pause effect
- Minimal but impactful

Do NOT show code in the hero - that comes in the next section.
The hero is about the PROBLEM and the PROMISE.
```

### Canonical Code Example Section Prompt

```
Create the "show don't tell" code section immediately after the hero.

Header: "This is what programming agents looks like"

Show the canonical ResearchAgent example in a large, beautiful code block:

```neam
agent ResearchAgent {
    goal: "Answer questions using verified knowledge"

    memory {
        short_term
        long_term
    }

    tools {
        search
        summarize
    }

    behavior {
        think(input) {
            insights = search(input)
            memory.short_term.store(insights)
        }

        verify() {
            trusted = memory.short_term.filter(confidence > 0.8)
            return trusted
        }

        respond() {
            answer = summarize(verify())
            return answer
        }
    }
}
```

Below the code, show 6 checkmarks in 2 rows of 3:
Row 1:
- ✓ Agent with explicit goal
- ✓ Structured memory
- ✓ Readable execution

Row 2:
- ✓ Governed tool access
- ✓ Observable behavior
- ✓ No hidden state

Use syntax highlighting with:
- Keywords (agent, goal, memory, tools, behavior): purple
- Strings: green
- Function names: blue
- Braces: gray

Add a subtle glow effect around the code block.
Make the code feel like the hero of this section.
```

### Problem/Solution Section Prompt

```
Create a "The way we build AI today is broken" section.

Left side - "Today we..."
Show 3 bullet points with strike-through or faded style:
• Glue prompts together
• Wrap LLMs in frameworks
• Simulate agents with loops and callbacks

Add text: "This works for demos. It breaks for systems."

Right side - Comparison table with two columns:
TODAY | WITH NEAM
- Prompts as strings → Agents as constructs
- Implicit behavior → Explicit state & goals
- Fragile orchestration → Structured execution
- Model-centric → Agent-centric
- Tool chaos → Governed capabilities
- Hard to debug → Observable by design

Use visual treatment:
- Left column (Today): faded, red-tinted, crossed out feel
- Right column (Neam): bright, green checkmarks, highlighted

Add a "Read the Manifesto →" link at bottom that goes to /about/philosophy.

Make this section emotionally impactful - developers should feel the pain
and see the solution clearly.
```

### What Neam Is Section Prompt

```
Create a "Neam is different" section showing what Neam is NOT and IS.

Layout: Two columns in a card/box

Left column - "Neam is NOT"
❌ A framework
❌ A Python wrapper
❌ A prompt library

Right column - "Neam IS"
✅ A programming language
✅ Agent-native runtime
✅ Neural + symbolic engine

Below the box, centered:
"Neam programs behavior, not just outputs."

Style:
- Card has subtle border and shadow
- NOT items are grayed/red
- IS items are bright/green
- The final statement is bold and impactful

This section should take 5 seconds to read and immediately clarify
what makes Neam fundamentally different.
```

### Design Principles Section Prompt

```
Create a "The 10 Principles" section for the philosophy page.

Show a numbered list or grid of the 10 non-negotiable principles:

1. Agents Are First-Class - Everything starts with an agent
2. Intelligence Is Pluggable - LLMs, SLMs, symbolic engines swappable
3. Execution Is Explicit - Thinking ≠ Acting
4. State Is Owned - No hidden globals, memory is scoped
5. Determinism Where Possible - Randomness is opt-in
6. Tools Are Capabilities - Governed, not free-for-all
7. Failure Is First-Class - Errors are modeled, not ignored
8. Human-Readable - If unreadable, it's a bug
9. Runtime-Aware - Edge, cloud, constrained environments
10. Ethics Are Declared - No unbounded autonomy

Design options:
A) Numbered cards in a 2x5 grid
B) Horizontal scrolling cards
C) Accordion with expand for details

Each principle should have:
- Number (large, purple)
- Name (bold)
- One-line explanation (regular)

Consider adding hover states that expand to show more detail.
```

### Who Neam Is For Section Prompt

```
Create a "Who Neam Is For" section with 5 audience cards.

Cards:
1. 🔧 Engineers - Building autonomous systems
2. 👥 Teams - Designing multi-agent workflows
3. 📡 Edge Developers - Cyber-physical AI applications
4. 🔬 Researchers - Real execution models
5. 🏢 Enterprises - Governed autonomy

Layout: 3 cards on top row, 2 centered on bottom row

Each card:
- Emoji icon (large)
- Audience name (bold)
- Use case (1 line)
- Subtle hover effect

Style: Clean cards with subtle borders, not too flashy.
The section should feel inclusive but specific.
```

### Original Hero Prompt (Alternative)

1. Large centered logo/wordmark "Neam"
2. Tagline: "The AI-First Programming Language"
3. Subtitle: "Build intelligent agents in minutes, not months. First-class
   LLM support. Built-in RAG. Native multi-agent."
4. Three buttons:
   - "Get Started" (primary, purple gradient)
   - "Try Playground" (secondary, outline)
   - "Star on GitHub ⭐ 1.2k" (ghost, links to GitHub)
5. Below buttons: animated code example showing a simple Neam agent
6. Background: subtle gradient mesh or neural network pattern

Code example to show:
```neam
agent Assistant {
  provider: "openai"
  model: "gpt-4o"
  system: "You are a helpful assistant."
}

{ emit Assistant.ask("Hello!"); }
```

Use syntax highlighting with purple/blue theme.
Add typing animation effect to the code.
```

### Code Comparison Section Prompt

```
Create a side-by-side code comparison component:

Left side: "Traditional Approach" (grayed out, verbose)
Right side: "Neam" (highlighted, clean)

Both showing the same functionality: creating an AI agent.

Below each code block show line count:
- Left: "23 lines, 3 imports"
- Right: "8 lines, zero imports"

Add a subtle glow effect around the Neam side.
Include a "See more comparisons" link below.

Style the Neam code with syntax highlighting:
- Keywords (agent, provider, emit): purple
- Strings: green
- Property names: blue
```

### Features Tabs Prompt

```
Create a tabbed feature showcase with 5 tabs:

1. "Agents" - Show agent declaration syntax
2. "RAG/Knowledge" - Show knowledge block with sources
3. "Multi-Agent" - Show Researcher→Writer→Editor pipeline
4. "Tools" - Show tool integration (coming soon badge)
5. "API Server" - Show REST API deployment

Each tab has:
- Code example with syntax highlighting
- 2-3 sentence explanation below
- "Learn more →" link

Default to "Agents" tab.
Add smooth transition between tabs.
Mobile: convert to accordion.
```

### Quick Start Section Prompt

```
Create a "Get Started in 60 seconds" section:

1. Terminal-style code block with:
   - Install command (curl)
   - Create project (neam-pkg init)
   - Set API key (export)
   - Build and run (neamc, neam)

2. OS selector tabs: [macOS] [Linux] [Windows] [Docker]
   - macOS/Linux show curl install
   - Windows shows winget/scoop
   - Docker shows docker run

3. Copy button for each command
4. "Full Installation Guide →" link at bottom

Style as a realistic terminal with:
- Dark background
- Green/white text
- Blinking cursor effect (optional)
```

### Ecosystem Cards Prompt

```
Create a 6-card grid showing the Neam ecosystem:

1. 📦 Packages - "Browse 200+ community packages" → registry.neam.dev
2. 📖 Docs - "Comprehensive guides and API reference" → /docs
3. 🎮 Playground - "Try Neam in your browser instantly" → /playground
4. 🔧 Tools - "VS Code, LSP, Debugger" → /tools
5. 💬 Community - "Discord, GitHub Discussions" → /community
6. 🎓 Learn - "Tutorials, Examples, Courses" → /learn

Each card:
- Icon (emoji or Lucide icon)
- Title
- One-line description
- Hover effect (lift + glow)
- Click navigates to page
```

### Playground Page Prompt

```
Create an interactive Neam playground page:

Left sidebar (collapsible):
- Example dropdown with presets
- Provider selector (OpenAI, Ollama)
- API key input (with show/hide toggle)
- "Use Demo Key" button (rate limited)

Main area:
- Monaco editor with Neam syntax highlighting
- Line numbers
- Error squiggles

Bottom area:
- "▶ Run" button (prominent, green)
- Output panel:
  - Shows "Compiling...", "Running...", then output
  - Error messages in red
  - Execution time

Top right:
- "Share" button (generates URL)
- "Reset" button
- "Copy" button

Mobile: stack vertically, hide sidebar in drawer.
```

### Navigation Prompt

```
Create a responsive navigation bar:

Desktop:
- Left: Neam logo (links to home)
- Center: Learn (dropdown), Docs (dropdown), Packages, Playground, Blog
- Right: GitHub icon, "Get Started" button

Learn dropdown:
- Getting Started
- Tutorial
- Examples
- By Example

Docs dropdown:
- Language Guide
- Agents
- Knowledge/RAG
- Patterns
- API Reference

Mobile:
- Hamburger menu
- Full-screen drawer with all links

Sticky on scroll with blur backdrop.
Add keyboard shortcut hint: "⌘K to search"
```

---

## 8. Technical Setup

### Domain Structure

```
neam.dev                    → Main website (Lovable/Vercel)
registry.neam.dev           → Package registry (Supabase)
docs.neam.dev               → Documentation (Docusaurus/GitBook)
play.neam.dev               → Playground (separate deployment)
api.neam.dev                → API for playground/registry
```

### Supabase Tables for Website

```sql
-- Newsletter subscribers
CREATE TABLE subscribers (
  id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
  email VARCHAR(255) UNIQUE NOT NULL,
  subscribed_at TIMESTAMP DEFAULT NOW(),
  confirmed BOOLEAN DEFAULT FALSE
);

-- Page analytics (simple)
CREATE TABLE page_views (
  id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
  path VARCHAR(512) NOT NULL,
  referrer VARCHAR(512),
  user_agent TEXT,
  country VARCHAR(2),
  viewed_at TIMESTAMP DEFAULT NOW()
);

-- Showcase projects
CREATE TABLE showcase (
  id UUID PRIMARY KEY DEFAULT uuid_generate_v4(),
  name VARCHAR(128) NOT NULL,
  description TEXT,
  url VARCHAR(512),
  github_url VARCHAR(512),
  image_url TEXT,
  author VARCHAR(128),
  featured BOOLEAN DEFAULT FALSE,
  approved BOOLEAN DEFAULT FALSE,
  submitted_at TIMESTAMP DEFAULT NOW()
);
```

### Environment Variables

```env
# Lovable/.env
VITE_SUPABASE_URL=https://xxx.supabase.co
VITE_SUPABASE_ANON_KEY=eyJ...
VITE_GITHUB_STARS_API=https://api.github.com/repos/neam-lang/neam
VITE_PLAYGROUND_API=https://api.neam.dev
```

### SEO & Analytics

```html
<!-- Google Analytics -->
<script async src="https://www.googletagmanager.com/gtag/js?id=G-XXXXXXX"></script>

<!-- Open Graph -->
<meta property="og:title" content="Neam - The AI-First Programming Language">
<meta property="og:description" content="Build intelligent AI agents in minutes...">
<meta property="og:image" content="https://neam.dev/og-image.png">
<meta property="og:url" content="https://neam.dev">
<meta property="og:type" content="website">

<!-- Twitter Card -->
<meta name="twitter:card" content="summary_large_image">
<meta name="twitter:site" content="@neamlang">
<meta name="twitter:title" content="Neam - The AI-First Programming Language">
<meta name="twitter:image" content="https://neam.dev/twitter-card.png">
```

---

## Implementation Checklist

### Phase 1: Core Pages (Week 1)
- [ ] Project setup in Lovable
- [ ] Navigation component
- [ ] Homepage hero section
- [ ] Homepage features section
- [ ] Footer component
- [ ] Mobile responsiveness

### Phase 2: Content Pages (Week 2)
- [ ] Getting Started page
- [ ] Examples page
- [ ] Documentation structure
- [ ] About page

### Phase 3: Interactive (Week 3)
- [ ] Playground (basic)
- [ ] Search functionality
- [ ] Newsletter signup
- [ ] Dark/light mode toggle

### Phase 4: Polish (Week 4)
- [ ] Animations and transitions
- [ ] SEO optimization
- [ ] Performance optimization
- [ ] Analytics integration
- [ ] Social sharing

### Phase 5: Launch
- [ ] Custom domain setup
- [ ] SSL certificate
- [ ] CDN configuration
- [ ] Launch blog post
- [ ] Social media announcement

---

## Resources

- **Lovable**: https://lovable.dev
- **shadcn/ui**: https://ui.shadcn.com
- **Tailwind CSS**: https://tailwindcss.com
- **Monaco Editor**: https://microsoft.github.io/monaco-editor/
- **Lucide Icons**: https://lucide.dev

---

*Last updated: January 2024*
