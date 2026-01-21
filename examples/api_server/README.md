# Neam Agent API Server

A native REST API server that exposes Neam agents as HTTP endpoints, enabling integration with any application.

## Quick Start

```bash
# Set OpenAI API key
export OPENAI_API_KEY="your-api-key"

# Build the server (from project root)
mkdir -p build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . --target neam-api --parallel

# Start the server
./neam-api --port 8080

# Test it
curl -X POST http://localhost:8080/api/v1/agent/ask \
  -H "Content-Type: application/json" \
  -d '{"agent_id": "assistant", "query": "Hello!"}'
```

## API Endpoints

### Health Check
```
GET /api/v1/health
```

**Response:**
```json
{
  "status": "healthy",
  "version": "1.0.0",
  "server": "neam-api"
}
```

### List Agents
```
GET /api/v1/agents
```

**Response:**
```json
{
  "agents": {
    "assistant": {
      "name": "Assistant",
      "provider": "openai",
      "model": "gpt-4o-mini",
      "description": "You are a helpful assistant...",
      "has_knowledge_base": false
    },
    ...
  }
}
```

### Query Agent
```
POST /api/v1/agent/ask
Content-Type: application/json

{
  "agent_id": "assistant",
  "query": "What is 2+2?"
}
```

**Response:**
```json
{
  "agent_id": "assistant",
  "query": "What is 2+2?",
  "response": "2 + 2 equals 4."
}
```

## Available Agents

| Agent ID | Name | Description | RAG |
|----------|------|-------------|-----|
| `assistant` | Assistant | General purpose helpful assistant | No |
| `coder` | Coder | Expert programmer, code solutions | No |
| `analyst` | Analyst | Data analysis and insights | No |
| `writer` | Writer | Creative writing | No |
| `researcher` | Researcher | Research with knowledge base | Yes |

## Examples

### Query the Assistant
```bash
curl -X POST http://localhost:8080/api/v1/agent/ask \
  -H "Content-Type: application/json" \
  -d '{"agent_id": "assistant", "query": "Explain quantum computing briefly"}'
```

### Get Code from Coder
```bash
curl -X POST http://localhost:8080/api/v1/agent/ask \
  -H "Content-Type: application/json" \
  -d '{"agent_id": "coder", "query": "Write a Python quicksort function"}'
```

### Research with RAG
```bash
curl -X POST http://localhost:8080/api/v1/agent/ask \
  -H "Content-Type: application/json" \
  -d '{"agent_id": "researcher", "query": "What are knowledge bases in Neam?"}'
```

### Creative Writing
```bash
curl -X POST http://localhost:8080/api/v1/agent/ask \
  -H "Content-Type: application/json" \
  -d '{"agent_id": "writer", "query": "Write a haiku about coding"}'
```

## Configuration

### Command Line Options
```
--host    Host to bind to (default: 0.0.0.0)
--port    Port to listen on (default: 8080)
--help    Show help message
```

### Environment Variables
```
OPENAI_API_KEY    Required for OpenAI agents
```

## Architecture

```
┌─────────────┐     ┌──────────────────┐     ┌─────────────┐
│   Client    │────▶│   neam-api       │────▶│   Neam VM   │
│  (curl/app) │     │  (C++ HTTP)      │     │  (native)   │
└─────────────┘     └──────────────────┘     └─────────────┘
                            │
                            ▼
                    ┌──────────────────┐
                    │  OpenAI API      │
                    │  (gpt-4o-mini)   │
                    └──────────────────┘
```

**Flow:**
1. Client sends POST request with agent_id and query
2. neam-api generates Neam program dynamically
3. Neam VM compiles and executes the program
4. Agent calls OpenAI API
5. Response returned to client as JSON

## Error Handling

**Invalid agent:**
```json
{
  "error": "Unknown agent: xyz. Available: ['assistant', 'coder', ...]"
}
```

**Missing query:**
```json
{
  "error": "Missing 'query' field"
}
```

**Compilation error:**
```json
{
  "error": "Compilation failed: ..."
}
```

## Integration Examples

### Python
```python
import requests

response = requests.post(
    "http://localhost:8080/api/v1/agent/ask",
    json={"agent_id": "assistant", "query": "Hello!"}
)
print(response.json()["response"])
```

### JavaScript/Node.js
```javascript
const response = await fetch("http://localhost:8080/api/v1/agent/ask", {
    method: "POST",
    headers: {"Content-Type": "application/json"},
    body: JSON.stringify({agent_id: "assistant", query: "Hello!"})
});
const data = await response.json();
console.log(data.response);
```

### cURL in Shell Script
```bash
#!/bin/bash
QUERY="$1"
curl -s -X POST http://localhost:8080/api/v1/agent/ask \
  -H "Content-Type: application/json" \
  -d "{\"agent_id\": \"assistant\", \"query\": \"$QUERY\"}" | jq -r '.response'
```

## Running in Production

### Using systemd (Linux)
```ini
[Unit]
Description=Neam Agent API Server
After=network.target

[Service]
Type=simple
User=neam
Environment=OPENAI_API_KEY=your-key
ExecStart=/path/to/neam-api --port 8080
Restart=always

[Install]
WantedBy=multi-user.target
```

### Using Docker
```dockerfile
FROM ubuntu:22.04
RUN apt-get update && apt-get install -y libcurl4 libssl3
COPY neam-api /usr/local/bin/
ENV OPENAI_API_KEY=""
EXPOSE 8080
CMD ["neam-api", "--port", "8080"]
```

## CORS Support

CORS is enabled by default, allowing requests from any origin. Headers:
```
Access-Control-Allow-Origin: *
Access-Control-Allow-Methods: GET, POST, OPTIONS
Access-Control-Allow-Headers: Content-Type, Authorization
```

## License

Same as Neam project license.
