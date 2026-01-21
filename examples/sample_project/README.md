# AI Assistant - Sample Neam Project

A demonstration project showing Neam's project structure, packaging, and TOML configuration.

## Project Structure

```
ai-assistant/
├── neam.toml           # Project manifest
├── README.md           # This file
├── src/
│   ├── main.neam       # Main entry point
│   └── api.neam        # API entry point
└── tests/
    └── test_agents.neam # Test suite
```

## Quick Start

```bash
# Set your API key
export OPENAI_API_KEY="your-key"

# Build the project
neamc src/main.neam -o build/main.neamb

# Run the project
neam build/main.neamb
```

## Using Scripts

The `neam.toml` defines several scripts:

```bash
# Build
neamc src/main.neam -o build/main.neamb

# Run
neam build/main.neamb

# Start API server
neam-api --port 8080

# Run tests
neamc tests/test_agents.neam -o /tmp/test.neamb && neam /tmp/test.neamb
```

## Features

This project demonstrates:

1. **Agent Definitions** - Multiple specialized agents
2. **Knowledge Bases** - RAG with document sources
3. **Multi-Agent Routing** - Query classification and routing
4. **API Entry Points** - Multiple entry points for different modes
5. **Test Suite** - Example test patterns

## Agents

| Agent | Purpose |
|-------|---------|
| `Assistant` | General-purpose helper |
| `CodeExpert` | Programming specialist |
| `DocHelper` | Documentation queries (RAG) |
| `Router` | Query classification |

## Configuration

See `neam.toml` for:
- Project metadata
- Dependencies
- Agent configuration
- Test settings
- Deployment targets

## License

MIT
