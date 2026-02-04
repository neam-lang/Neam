# Neam Quick Start Guide

Get up and running with Neam in under 5 minutes.

## Installation

### macOS / Linux

```bash
# One-line install
curl -fsSL https://raw.githubusercontent.com/neam-lang/Neam/main/install.sh | bash

# Restart terminal or run:
source ~/.zshrc  # or ~/.bashrc
```

**Manual install:**
```bash
# Download latest release
curl -LO https://github.com/neam-lang/Neam/releases/download/v0.6.2/neam-v0.6.2-macos-arm64.tar.gz

# Extract to ~/.neam
mkdir -p ~/.neam && tar -xzf neam-v0.6.2-macos-arm64.tar.gz -C ~/.neam

# Add to PATH (add to ~/.zshrc or ~/.bashrc)
export PATH="$HOME/.neam/bin:$PATH"
```

### Windows

**Option 1: PowerShell (Recommended)**
```powershell
# Download and run installer
Invoke-WebRequest -Uri "https://raw.githubusercontent.com/neam-lang/Neam/main/install.bat" -OutFile "$env:TEMP\install.bat"
& "$env:TEMP\install.bat"
```

**Option 2: Manual Download**
1. Download: https://github.com/neam-lang/Neam/releases/download/v0.6.2/neam-v0.6.2-windows-x64.zip
2. Extract to `C:\neam` or `%USERPROFILE%\.neam`
3. Add `bin` folder to PATH
4. Restart terminal

---

## Your First Program

### Hello World

```bash
# Create hello.neam
echo '{ emit "Hello from Neam!"; }' > hello.neam

# Compile
neamc hello.neam -o hello.neamb

# Run
neam hello.neamb
```

Output:
```
Hello from Neam!
```

### Interactive REPL

```bash
neam-cli
```

```
Neam v0.6.2 REPL
Type 'exit' to quit, 'help' for commands.

neam> emit "Hello!";
Hello!
neam> let x = 10 + 5;
neam> emit x;
15
neam> exit
```

---

## Create an AI Agent Project

```bash
# Initialize new project
neam-pkg init my-agent
cd my-agent

# Project structure:
# my-agent/
#   ├── neam.toml        # Project config
#   └── src/
#       └── main.neam    # Entry point
```

### Configure LLM Provider

**Option A: Local LLM with Ollama (Free)**
```bash
# Install Ollama
# macOS:
brew install ollama
# Windows: Download from https://ollama.com

# Pull a model
ollama pull llama3.2:3b

# Your agent will use provider: "ollama" by default
```

**Option B: OpenAI API**
```bash
# Set API key
export OPENAI_API_KEY="sk-..."   # macOS/Linux
set OPENAI_API_KEY=sk-...        # Windows
```

**Option C: Other Providers**
```bash
# Anthropic Claude
export ANTHROPIC_API_KEY="sk-ant-..."

# Google Gemini
export GEMINI_API_KEY="..."

# Azure OpenAI
export AZURE_OPENAI_API_KEY="..."
export AZURE_OPENAI_ENDPOINT="https://..."
```

---

## Example: Simple Chat Agent

Create `chat.neam`:
```neam
// Define an agent
agent Assistant {
  provider: "ollama"           // or "openai", "anthropic", "gemini"
  model: "llama3.2:3b"         // or "gpt-4o", "claude-sonnet-4-20250514", etc.
  temperature: 0.7
  system: "You are a helpful assistant."
}

// Main program
{
  emit "=== Neam Chat Agent ===";
  emit "";

  let response = Assistant.ask("What is the capital of France?");
  emit "Response: " + response;
}
```

Run:
```bash
neamc chat.neam -o chat.neamb
neam chat.neamb
```

---

## Example: Multi-Agent System

Create `multiagent.neam`:
```neam
agent Researcher {
  provider: "ollama"
  model: "llama3.2:3b"
  system: "You research topics and provide detailed information."
}

agent Writer {
  provider: "ollama"
  model: "llama3.2:3b"
  system: "You write clear, concise summaries."
}

{
  // Research phase
  let research = Researcher.ask("Key facts about renewable energy");
  emit "Research: " + research;
  emit "";

  // Writing phase
  let summary = Writer.ask("Summarize this in 2 sentences: " + research);
  emit "Summary: " + summary;
}
```

---

## Example: Cognitive Agent (v0.5.0+)

Create `cognitive.neam`:
```neam
agent Analyst {
  provider: "ollama"
  model: "llama3.2:3b"
  temperature: 0.3
  system: "You are a logical analyst."

  // Cognitive features
  reasoning: chain_of_thought   // Step-by-step reasoning
  goals: ["Analyze data", "Find patterns", "Make recommendations"]
}

{
  emit "=== Cognitive Agent Demo ===";

  // Get agent's goals
  let current_goals = agent_get_goals("Analyst");
  emit "Goals: " + str(current_goals);

  // Ask with chain-of-thought reasoning
  let analysis = Analyst.ask("What are 3 benefits of unit testing?");
  emit "Analysis: " + analysis;
}
```

---

## Quick Reference

### CLI Tools

| Command | Description |
|---------|-------------|
| `neamc <file.neam> -o <output.neamb>` | Compile source to bytecode |
| `neam <file.neamb>` | Run bytecode |
| `neam-cli` | Interactive REPL |
| `neam-cli <file.neam>` | Run source directly |
| `neam-pkg init <name>` | Create new project |
| `neam-pkg install <pkg>` | Install package |

### Language Basics

```neam
// Variables
let name = "Neam";
let count = 42;
let active = true;

// Functions
fun greet(name) {
  return "Hello, " + name + "!";
}

// Control flow
if (count > 10) {
  emit "Large";
} else {
  emit "Small";
}

// Loops
while (count > 0) {
  emit count;
  count = count - 1;
}

// Collections
let items = ["a", "b", "c"];
let config = {"key": "value", "num": 123};

// Output
emit "Message";
emit greet("World");
```

### Agent Properties

```neam
agent MyAgent {
  provider: "ollama"              // Required: ollama, openai, anthropic, gemini, azure, bedrock
  model: "llama3.2:3b"            // Required: model name
  temperature: 0.7                // Optional: 0.0-2.0
  system: "System prompt"         // Optional: system message
  reasoning: chain_of_thought     // Optional: reasoning strategy
  goals: ["goal1", "goal2"]       // Optional: autonomous goals
}
```

---

## Troubleshooting

### "neamc: command not found"
- Restart your terminal after installation
- Check PATH: `echo $PATH` (Unix) or `echo %PATH%` (Windows)
- Verify install: `ls ~/.neam/bin` or `dir %USERPROFILE%\.neam\bin`

### "Connection refused" (Ollama)
- Start Ollama: `ollama serve`
- Check it's running: `curl http://localhost:11434/api/tags`
- Pull model: `ollama pull llama3.2:3b`

### "Invalid API key" (OpenAI/Anthropic)
- Check key is set: `echo $OPENAI_API_KEY`
- Ensure no extra spaces or quotes
- Verify key at provider dashboard

---

## Next Steps

- **Examples**: See `examples/` folder for more demos
- **Documentation**: https://github.com/neam-lang/Neam/blob/main/README.md
- **Cognitive Features**: https://github.com/neam-lang/Neam/blob/main/docs/V0.5.0_COGNITIVE_FEATURES_README.md
- **API Reference**: https://github.com/neam-lang/Neam/blob/main/docs/

---

**Version**: Neam v0.6.2
**License**: Apache 2.0
