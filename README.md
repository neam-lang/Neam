# Neam — Agentic AI Programming Language

**Version 0.6.2** | Built with C++17 | First-class AI Agent Support

Neam is a domain-specific language for building AI agent systems. Agents, knowledge bases, tools, and multi-agent orchestration are language-level constructs, not library APIs.

```
.neam source → Parser → AST → Compiler → .neamb bytecode → VM → Output
```

## Why Neam?

| Problem with Current Approaches | Neam's Solution |
|--------------------------------|-----------------|
| String-based prompts with no validation | Compile-time checked agent declarations |
| Provider lock-in (OpenAI, Anthropic, etc.) | Multi-provider abstraction - switch with one line |
| Runtime errors for invalid configs | Compiler catches errors before execution |
| Manual multi-agent orchestration | Built-in handoffs, runners, and guardrails |
| No standard observability | Tracing and cost tracking built into the VM |

---

## Quick Start

### macOS / Linux

```bash
# Install Neam
curl -fsSL https://github.com/neam-lang/Neam/releases/download/v0.6.2/install.sh | bash
source ~/.zshrc  # or ~/.bashrc

# Create your first agent
neam-pkg init my-agent
cd my-agent

# Set your API key
export OPENAI_API_KEY="sk-..."

# Build and run
neamc src/main.neam -o build/main.neamb
neam build/main.neamb
```

### Windows

```powershell
# Install Neam
Invoke-WebRequest -Uri "https://github.com/neam-lang/Neam/releases/download/v0.6.2/neam-v0.6.2-windows-x64.zip" -OutFile "$env:TEMP\neam.zip"
Expand-Archive -Path "$env:TEMP\neam.zip" -DestinationPath "$env:USERPROFILE\.neam" -Force
$env:PATH = "$env:USERPROFILE\.neam\bin;$env:PATH"

# Create your first agent
neam-pkg init my-agent
cd my-agent

# Set your API key
$env:OPENAI_API_KEY = "sk-..."

# Build and run
neamc src/main.neam -o build/main.neamb
neam build/main.neamb
```

### Other LLM Providers

```bash
# Anthropic Claude
export ANTHROPIC_API_KEY="sk-ant-..."

# Google Gemini
export GEMINI_API_KEY="..."

# Local Ollama (free, no API key needed)
ollama pull llama3.2:3b
```

### Build from Source

```bash
git clone https://github.com/neam-lang/Neam.git && cd Neam
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
sudo cmake --install build  # optional
```

---

## Examples

### Example 1: Simple AI Agent

Create your first AI agent that responds to queries.

**File: `simple_agent.neam`**
```neam
// Define an agent with OpenAI
agent Assistant {
    provider: "openai"
    model: "gpt-4"
    system: "You are a helpful assistant. Be concise."
}

// Ask the agent a question
let response = Assistant.ask("What is the capital of France?");
emit response;
```

**Run:**
```bash
neam-cli simple_agent.neam
```

**Expected Output:**
```
The capital of France is Paris.
```

---

### Example 2: Using Ollama (Local LLM)

Run AI agents locally without API costs.

**File: `local_agent.neam`**
```neam
// Use local Ollama model
agent LocalBot {
    provider: "ollama"
    model: "llama3.2:3b"
    system: "You are a friendly assistant."
}

let answer = LocalBot.ask("Explain recursion in one sentence.");
emit answer;
```

**Prerequisites:**
```bash
# Install and run Ollama
ollama pull llama3.2:3b
ollama serve
```

**Run:**
```bash
neam-cli local_agent.neam
```

---

### Example 3: Agent with Skills (Tools)

Give your agent capabilities to perform actions.

**File: `agent_with_skills.neam`**
```neam
// Define a skill (tool) for calculations
skill calculate {
    description: "Perform mathematical calculations"
    params: { expression: string }
    impl(expression) {
        // Simple calculator logic
        if (expression == "2+2") {
            return 4;
        }
        return "Calculated: " + expression;
    }
}

// Agent with the skill attached
agent MathBot {
    provider: "openai"
    model: "gpt-4"
    system: "You are a math tutor. Use the calculate skill when needed."
    skills: [calculate]
}

let result = MathBot.ask("What is 2+2?");
emit result;
```

**Run:**
```bash
neam-cli agent_with_skills.neam
```

---

### Example 4: Knowledge Base (RAG)

Build an agent with domain-specific knowledge.

**File: `rag_agent.neam`**
```neam
// Define a knowledge base
knowledge CompanyFAQ {
    vector_store: "memory"
    embedding_model: "text-embedding-3-small"
    chunk_size: 500
    chunk_overlap: 50
    sources: [
        { type: "text", content: "Our company was founded in 2020. We specialize in AI solutions." },
        { type: "text", content: "Our main product is NeamAI, an enterprise AI platform." },
        { type: "text", content: "Contact support at support@example.com or call 1-800-NEAM." }
    ]
}

// Agent with knowledge base attached
agent SupportBot {
    provider: "openai"
    model: "gpt-4"
    system: "You are a customer support agent. Answer questions using the company knowledge base."
    knowledge: [CompanyFAQ]
}

let answer = SupportBot.ask("When was the company founded?");
emit answer;
```

**Run:**
```bash
neam-cli rag_agent.neam
```

**Expected Output:**
```
Our company was founded in 2020.
```

---

### Example 5: Multi-Agent Handoff

Create multiple agents that can transfer conversations.

**File: `multi_agent.neam`**
```neam
// Sales agent
agent SalesAgent {
    provider: "openai"
    model: "gpt-4"
    system: "You are a sales representative. Help customers with purchases."
}

// Support agent
agent SupportAgent {
    provider: "openai"
    model: "gpt-4"
    system: "You are a technical support specialist. Help with technical issues."
}

// Define handoff from Sales to Support
handoff SalesToSupport {
    targets: [handoff_to(SupportAgent) {
        tool_name: "transfer_to_support"
        description: "Transfer to technical support for technical issues"
    }]
}

// Runner orchestrates the conversation
runner CustomerService {
    entry_agent: SalesAgent
    max_turns: 10
}

emit "Multi-agent system configured!";
```

---

### Example 6: Guardrails for Safety

Add safety checks to your agent responses.

**File: `guarded_agent.neam`**
```neam
// Define a guardrail
guard ContentFilter {
    description: "Filter inappropriate content"
    on_observation(message) {
        // Check and filter the message
        return message;
    }
}

// Agent with guardrail
agent SafeBot {
    provider: "openai"
    model: "gpt-4"
    system: "You are a helpful assistant for all ages."
    guards: [ContentFilter]
}

let response = SafeBot.ask("Tell me a joke.");
emit response;
```

---

### Example 7: Voice Pipeline

Build a voice-enabled agent (Speech-to-Text → Agent → Text-to-Speech).

**File: `voice_agent.neam`**
```neam
// Define the agent
agent VoiceAssistant {
    provider: "openai"
    model: "gpt-4"
    system: "You are a voice assistant. Keep responses brief and conversational."
}

// Voice pipeline configuration
voice VoiceBot {
    stt_provider: "whisper"
    tts_provider: "openai"
    agent: VoiceAssistant
}

emit "Voice pipeline configured!";
```

**Run with voice:**
```bash
neam-api voice_agent.neam --port 8080
# Voice endpoints available at http://localhost:8080
```

---

### Example 8: A2A Protocol (Agent-to-Agent)

Expose your agent as an A2A-compliant service.

**File: `a2a_agent.neam`**
```neam
// Define the agent
agent APIAgent {
    provider: "openai"
    model: "gpt-4"
    system: "You are an API assistant."
}

// A2A card for discoverability
card AgentCard {
    version: "1.0"
    description: "A helpful API agent accessible via A2A protocol"
}

// Task definition
task QueryTask {
    target_agent: APIAgent
}
```

**Run as A2A server:**
```bash
neam-api a2a_agent.neam --port 8080
```

**Access:**
- Agent card: `GET http://localhost:8080/.well-known/agent.json`
- Task API: `POST http://localhost:8080/a2a`

---

### Example 9: Functions and Control Flow

Neam supports standard programming constructs.

**File: `programming.neam`**
```neam
// Function definition
fun factorial(n) {
    if (n <= 1) {
        return 1;
    }
    return n * factorial(n - 1);
}

// Calculate factorial
let result = factorial(5);
emit "5! = " + result;

// Lists and iteration
let numbers = [1, 2, 3, 4, 5];
let sum = 0;
let i = 0;
while (i < 5) {
    sum = sum + numbers[i];
    i = i + 1;
}
emit "Sum of 1-5: " + sum;

// Maps (dictionaries)
let person = {
    "name": "Alice",
    "age": 30
};
emit "Name: " + person["name"];
```

**Run:**
```bash
neam-cli programming.neam
```

**Output:**
```
5! = 120
Sum of 1-5: 15
Name: Alice
```

---

### Example 10: Multi-Provider Switching

Switch between LLM providers with one line change.

**File: `multi_provider.neam`**
```neam
// OpenAI version
agent OpenAIBot {
    provider: "openai"
    model: "gpt-4"
    system: "You are helpful."
}

// Anthropic version - just change provider and model
agent AnthropicBot {
    provider: "anthropic"
    model: "claude-3-opus-20240229"
    system: "You are helpful."
}

// Gemini version
agent GeminiBot {
    provider: "gemini"
    model: "gemini-pro"
    system: "You are helpful."
}

// Local Ollama version
agent LocalBot {
    provider: "ollama"
    model: "llama3.2:3b"
    system: "You are helpful."
}

// Use any of them with the same .ask() interface
let response = LocalBot.ask("Hello!");
emit response;
```

---

## CLI Reference

| Command | Description |
|---------|-------------|
| `neamc <file.neam>` | Compile to bytecode (.neamb) |
| `neam <file.neamb>` | Run compiled bytecode |
| `neam-cli <file.neam>` | Compile and run in one step |
| `neam-api <file.neam>` | Start HTTP/A2A API server |
| `neam-lsp` | Language Server Protocol for IDE support |
| `neam-pkg install <package>` | Install a Neam package |

### Common Options

```bash
neam-cli program.neam              # Run program
neam-cli program.neam --trace      # Run with tracing enabled
neam-api program.neam --port 8080  # Start API server on port 8080
neamc program.neam -o output.neamb # Compile with custom output name
```

---

## Language Quick Reference

### Syntax Overview

```neam
// Variables
let x = 10;
let name = "Neam";
let active = true;
let items = [1, 2, 3];
let config = {"key": "value"};

// Control flow (no 'for' loops - use 'while')
if (x > 5) {
    emit "Large";
} else {
    emit "Small";
}

while (x > 0) {
    x = x - 1;
}

// Functions
fun greet(name) {
    return "Hello, " + name;
}

// Agent declaration
agent MyAgent {
    provider: "openai"      // Required: openai, anthropic, gemini, ollama
    model: "gpt-4"          // Required: model name
    system: "Instructions"  // Required: system prompt
    temperature: 0.7        // Optional: 0.0-1.0
    skills: [skill1]        // Optional: attached skills
    knowledge: [kb1]        // Optional: attached knowledge bases
}

// Skill (tool) declaration
skill my_skill {
    description: "What this skill does"
    params: { arg1: string, arg2: number }
    impl(arg1, arg2) {
        return "result";
    }
}

// Knowledge base declaration
knowledge MyKB {
    vector_store: "memory"
    embedding_model: "text-embedding-3-small"
    chunk_size: 500
    chunk_overlap: 50
    sources: [{ type: "text", content: "..." }]
}
```

### Key Differences from Other Languages

| Feature | Neam | Python/JS |
|---------|------|-----------|
| Logical AND | Nested `if` statements | `and` / `&&` |
| Logical OR | Sequential `if` statements | `or` / `||` |
| Logical NOT | `!expression` | `not` / `!` |
| For loops | Use `while` loops | `for` available |
| Agent | Language keyword | Library class |
| Emit output | `emit value;` | `print()` / `console.log()` |

---

## Supported LLM Providers

| Provider | Provider String | Example Models |
|----------|-----------------|----------------|
| OpenAI | `"openai"` | gpt-4, gpt-4-turbo, gpt-3.5-turbo |
| Anthropic | `"anthropic"` | claude-3-opus, claude-3-sonnet |
| Google Gemini | `"gemini"` | gemini-pro, gemini-pro-vision |
| Azure OpenAI | `"azure_openai"` | Deployed model names |
| AWS Bedrock | `"bedrock"` | anthropic.claude-v2 |
| Google Vertex | `"vertex"` | gemini-pro |
| Ollama (local) | `"ollama"` | llama3.2, mistral, codellama |

---

## Project Structure

For larger projects, organize your code:

```
my-project/
├── neam.toml           # Project manifest
├── src/
│   ├── main.neam       # Entry point
│   ├── agents/
│   │   └── support.neam
│   └── skills/
│       └── calculator.neam
├── knowledge/
│   └── faq.txt
└── tests/
    └── test_agent.neam
```

**neam.toml:**
```toml
[package]
name = "my-project"
version = "1.0.0"

[dependencies]
# Add package dependencies here
```

---

## Resources

- **GitHub**: https://github.com/neam-lang/Neam
- **Releases**: https://github.com/neam-lang/Neam/releases
- **Issues**: https://github.com/neam-lang/Neam/issues

---

## License

Apache License 2.0 - See [LICENSE](LICENSE) for details.
