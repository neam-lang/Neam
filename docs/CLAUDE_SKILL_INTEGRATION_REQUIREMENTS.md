# Requirements: Claude Tool Use Integration for Neam Skills

## Executive Summary

Neam skills already map almost 1:1 to Claude's tool use pattern. The skill declaration (`name`, `description`, `params` with JSON Schema, `impl`) is structurally identical to a Claude tool definition (`name`, `description`, `input_schema`). The primary gap is in the **LLM adapter layer** — Neam currently sends plain chat messages without tool definitions and parses tool calls from raw text. This document specifies how to wire Neam's existing skill system into Claude's native tool use protocol for reliable, schema-validated function calling.

---

## Current State Analysis

### What Neam Already Has

| Component | Status | Details |
|---|---|---|
| Skill declaration syntax | Done | `skill Name { description, params, impl }` |
| JSON Schema params | Done | `build_skill_schema()` generates `{type, properties, required}` |
| Skill compilation | Done | `OP_DEFINE_SKILL` creates `ObjSkill` in VM |
| Schema validation | Done | `validate_skill_args()` with nlohmann json-schema-validator |
| Sensitive param redaction | Done | `sensitive=true` annotation, redacted in logs |
| Agent skill binding | Partial | `skills: [Skill1]` parsed but skills not sent to LLM |
| ReAct loop | Done | Text-based `ACTION: name(args)` parsing (unreliable) |
| LLM provider interface | Minimal | `chat(messages)` only — no tool definitions |

### What's Missing

| Gap | Impact |
|---|---|
| Provider interface lacks tool parameter | Skills can't be sent to LLM |
| No tool_use response parsing | Can't detect when LLM wants to call a tool |
| No tool_result message format | Can't send skill results back to LLM |
| No tool call loop | Can't handle multi-turn tool conversations |
| Adapters don't format tools | OpenAI, Bedrock, Ollama all ignore skills |

---

## Neam Skill to Claude Tool Mapping

The mapping is direct:

```
Neam Skill                          Claude Tool
─────────────────────────────────   ─────────────────────────────────
skill Calculator {                  {
  description: "Adds numbers",        "name": "Calculator",
  params: [                            "description": "Adds numbers",
    {                                  "input_schema": {
      name: "a",                         "type": "object",
      schema: {                          "properties": {
        "type": "number",                  "a": {
        "description": "First"               "type": "number",
      }                                      "description": "First"
    },                                     },
    {                                      "b": {
      name: "b",                             "type": "number",
      schema: {                              "description": "Second"
        "type": "number",                  }
        "description": "Second"          },
      }                                  "required": ["a", "b"]
    }                                  }
  ],                                }
  impl: fun(a, b) {
    return a + b;
  }
}
```

The existing `build_skill_schema()` function in `vm.cpp` already generates the exact JSON Schema that Claude's `input_schema` expects. **No schema transformation needed.**

---

## Requirements

### R1. Extend LLM Provider Interface

**File:** `NeamC/include/neamc/llm/provider.hpp`

Add tool-aware types and methods to the provider interface:

```cpp
// New types
struct ToolDefinition {
  std::string name;
  std::string description;
  nlohmann::json input_schema;  // JSON Schema (from build_skill_schema)
};

struct ToolCall {
  std::string id;       // Provider-assigned ID (e.g., "toolu_01A09q...")
  std::string name;     // Tool/skill name
  nlohmann::json input; // Parsed arguments
};

struct ContentBlock {
  std::string type;     // "text" or "tool_use" or "tool_result"
  std::string text;     // For type="text"
  ToolCall tool_call;   // For type="tool_use"
};

struct ChatResponse {
  std::string text;                    // Convenience: concatenated text blocks
  std::vector<ContentBlock> content;   // Full content blocks
  std::string stop_reason;            // "end_turn", "tool_use", "max_tokens"
  bool has_tool_calls() const;
  std::vector<ToolCall> get_tool_calls() const;
};

struct ToolResult {
  std::string tool_use_id;  // Matches ToolCall.id
  std::string content;      // Serialized result
  bool is_error = false;
};

// Extended Message (backward compatible)
struct Message {
  std::string role;
  std::string content;
  std::vector<ContentBlock> content_blocks;  // New: for multi-block messages
  std::vector<ToolResult> tool_results;      // New: for tool result messages
};
```

Add a new virtual method (with default fallback to existing `chat`):

```cpp
class LLMProvider {
public:
  // Existing (unchanged)
  virtual std::string chat(const std::vector<Message>& messages) = 0;

  // New: tool-aware chat (default falls back to plain chat)
  virtual ChatResponse chat_with_tools(
    const std::vector<Message>& messages,
    const std::vector<ToolDefinition>& tools,
    const std::string& tool_choice = "auto"
  ) {
    // Default: ignore tools, wrap plain chat response
    ChatResponse resp;
    resp.text = chat(messages);
    resp.stop_reason = "end_turn";
    return resp;
  }
};
```

**Rationale:** Default implementation ensures backward compatibility — Ollama and other providers without tool support continue to work unchanged. Only adapters that override `chat_with_tools` get native tool calling.

---

### R2. Implement Claude Tool Use in Bedrock Adapter

**File:** `NeamC/src/vm/llm/bedrock_adapter.cpp`

The Bedrock adapter must format the Anthropic Messages API with tools:

**Request format:**
```json
{
  "anthropic_version": "bedrock-2023-05-31",
  "max_tokens": 4096,
  "system": "System prompt here",
  "messages": [
    {"role": "user", "content": "What is 2 + 3?"}
  ],
  "tools": [
    {
      "name": "Calculator",
      "description": "Adds two numbers",
      "input_schema": {
        "type": "object",
        "properties": {
          "a": {"type": "number", "description": "First number"},
          "b": {"type": "number", "description": "Second number"}
        },
        "required": ["a", "b"]
      }
    }
  ],
  "tool_choice": {"type": "auto"}
}
```

**Response parsing — detect tool_use content blocks:**
```json
{
  "content": [
    {"type": "text", "text": "I'll calculate that."},
    {
      "type": "tool_use",
      "id": "toolu_01XFDUDYJgAACzvnptvVer6z",
      "name": "Calculator",
      "input": {"a": 2, "b": 3}
    }
  ],
  "stop_reason": "tool_use"
}
```

**Tool result submission (next message):**
```json
{
  "role": "user",
  "content": [
    {
      "type": "tool_result",
      "tool_use_id": "toolu_01XFDUDYJgAACzvnptvVer6z",
      "content": "5"
    }
  ]
}
```

---

### R3. Implement OpenAI Function Calling in OpenAI Adapter

**File:** `NeamC/src/vm/llm/openai_adapter.cpp`

The OpenAI adapter translates the same `ToolDefinition` into OpenAI's format:

**Request format:**
```json
{
  "model": "gpt-4o",
  "messages": [...],
  "tools": [
    {
      "type": "function",
      "function": {
        "name": "Calculator",
        "description": "Adds two numbers",
        "parameters": {
          "type": "object",
          "properties": {
            "a": {"type": "number"},
            "b": {"type": "number"}
          },
          "required": ["a", "b"]
        }
      }
    }
  ],
  "tool_choice": "auto"
}
```

**Response parsing — detect tool_calls:**
```json
{
  "choices": [{
    "message": {
      "role": "assistant",
      "content": null,
      "tool_calls": [{
        "id": "call_abc123",
        "type": "function",
        "function": {
          "name": "Calculator",
          "arguments": "{\"a\": 2, \"b\": 3}"
        }
      }]
    },
    "finish_reason": "tool_calls"
  }]
}
```

**Tool result submission (separate message with role `tool`):**
```json
{
  "role": "tool",
  "tool_call_id": "call_abc123",
  "content": "5"
}
```

**Key difference from Claude:** OpenAI uses `role: "tool"` for results; Claude uses `role: "user"` with `tool_result` content blocks. The adapter must handle this translation internally so the VM sees a unified `ChatResponse`.

---

### R4. Implement Tool Call Loop in VM

**File:** `NeamC/src/vm/vm.cpp`

Replace the text-based ReAct loop with a native tool call loop when the agent has skills:

```
Agent.ask(prompt)
  │
  ├── 1. Collect ToolDefinitions from agent's skills[]
  │     └── For each ObjSkill: { name, description, build_skill_schema() }
  │
  ├── 2. Call provider.chat_with_tools(messages, tools, "auto")
  │
  ├── 3. Check response.stop_reason
  │     │
  │     ├── "end_turn" → return response.text (done)
  │     │
  │     └── "tool_use" → for each tool_call in response:
  │           │
  │           ├── 4a. Look up ObjSkill by tool_call.name
  │           ├── 4b. Convert tool_call.input JSON → Value args
  │           ├── 4c. validate_skill_args(skill, args)
  │           ├── 4d. Execute skill impl → get result Value
  │           ├── 4e. Serialize result → string
  │           ├── 4f. Build ToolResult { tool_call.id, result_string, is_error }
  │           │
  │           └── 5. Append assistant message + tool results to messages[]
  │                 └── Go to step 2 (loop)
  │
  └── 6. Safety: max iterations = NEAM_MAX_REACT_STEPS (default 100)
```

**Parallel tool calls:** When the LLM returns multiple tool_use blocks in one response, execute all skills and return all results in a single message. Neam's `vm::async::Executor` can parallelize independent skill executions.

**Fallback:** If the provider doesn't support tools (returns default `ChatResponse` with `stop_reason = "end_turn"`), fall back to the existing text-based ReAct loop for backward compatibility.

---

### R5. Wire Agent Skill Declarations to Tool Definitions

**File:** `NeamC/src/vm/vm.cpp` (agent `.ask()` handler)

Currently, `skills: [Calculator]` in an agent declaration stores references but doesn't use them during LLM calls. The agent `.ask()` handler must:

1. Resolve each skill name in the agent's `skills` list to its `ObjSkill*`
2. Build `std::vector<ToolDefinition>` from each skill's metadata
3. Pass tools to `chat_with_tools()`

```cpp
std::vector<ToolDefinition> collect_agent_tools(const ObjAgent* agent) {
  std::vector<ToolDefinition> tools;
  for (const auto& skill_name : agent->skills) {
    auto it = globals_.find(skill_name);
    if (it != globals_.end() && it->second.is_skill()) {
      const auto* skill = it->second.as_skill();
      tools.push_back({
        std::string(skill->name->chars, skill->name->length),
        std::string(skill->description->chars, skill->description->length),
        build_skill_schema(skill)
      });
    }
  }
  return tools;
}
```

---

### R6. JSON Argument Conversion (Tool Call Input → Neam Values)

**File:** `NeamC/src/vm/vm.cpp`

When the LLM returns a tool call, the `input` field is a JSON object. This must be converted to ordered Neam `Value` arguments matching the skill's `param_names`:

```cpp
std::vector<Value> json_args_to_values(const ObjSkill* skill,
                                        const nlohmann::json& input) {
  std::vector<Value> args;
  for (const auto& param_name : skill->param_names) {
    if (!input.contains(param_name)) {
      throw std::runtime_error(
        "Missing required argument '" + param_name + "' for skill '" +
        std::string(skill->name->chars, skill->name->length) + "'");
    }
    args.push_back(json_to_value(input[param_name]));
  }
  return args;
}

Value json_to_value(const nlohmann::json& j) {
  if (j.is_null())    return Value::Nil();
  if (j.is_boolean()) return Value::Bool(j.get<bool>());
  if (j.is_number())  return Value::Number(j.get<double>());
  if (j.is_string())  return Value::String(alloc_string(j.get<std::string>()));
  if (j.is_array())   { /* build ObjList */ }
  if (j.is_object())  { /* build ObjMap */ }
  return Value::Nil();
}
```

---

### R7. Result Serialization (Neam Value → Tool Result String)

**File:** `NeamC/src/vm/vm.cpp`

Skill return values must be serialized back to strings for the tool result. Use JSON for structured types:

```cpp
std::string value_to_tool_result(const Value& value) {
  if (value.is_nil())    return "null";
  if (value.is_bool())   return value.as_bool() ? "true" : "false";
  if (value.is_number()) return std::to_string(value.as_number());
  if (value.is_string()) return std::string(as_string(value)->chars,
                                             as_string(value)->length);
  if (value.is_list() || value.is_map()) return value_to_json(value).dump();
  return "null";
}
```

---

### R8. Error Handling

When a skill execution fails, the error should be sent back to the LLM as a tool result with `is_error = true`:

```cpp
ToolResult result;
result.tool_use_id = tool_call.id;
try {
  Value ret = call_function(skill->impl, args, true, skill_name);
  result.content = value_to_tool_result(ret);
  result.is_error = false;
} catch (const std::exception& e) {
  result.content = std::string("Error: ") + e.what();
  result.is_error = true;
}
```

This allows the LLM to recover gracefully — it can retry with corrected arguments or inform the user.

---

### R9. Budget and Guard Integration

Tool calls in the loop must respect existing budget and guard systems:

**Budget check before each skill execution:**
```
remaining_budget -= estimated_cost(skill)
if (remaining_budget <= 0) → return error to LLM: "Budget exceeded"
```

**Guard chain execution:**
```
for guard in agent.guardchains:
  if (!guard.on_tool_input(args)) → return error to LLM: "Input rejected by guard"
  // ... execute skill ...
  if (!guard.on_tool_output(result)) → return error to LLM: "Output rejected by guard"
```

**Capability check:**
```
for cap in skill.required_capabilities:
  if (!agent.has_capability(cap)) → return error to LLM: "Missing capability"
```

---

### R10. Ollama Tool Support (Optional, Lower Priority)

Ollama supports tool calling for compatible models (Llama 3.1+, Qwen 2.5+). The format follows OpenAI conventions:

```json
{
  "model": "qwen2.5:14b",
  "messages": [...],
  "tools": [
    {
      "type": "function",
      "function": {
        "name": "Calculator",
        "description": "...",
        "parameters": { ... }
      }
    }
  ]
}
```

This can reuse the OpenAI adapter's tool formatting logic. Implement after R2 and R3 are stable.

---

## Neam Syntax — No Changes Required

The existing Neam skill syntax already expresses everything Claude needs:

```neam
skill GetWeather {
  description: "Get current weather for a location",
  params: [
    {
      name: "location",
      schema: { "type": "string", "description": "City and state" }
    },
    {
      name: "unit",
      schema: { "type": "string", "enum": ["celsius", "fahrenheit"] }
    }
  ],
  impl: fun(location, unit) {
    let resp = http_get("https://weather.api/v1?q=" + location + "&units=" + unit);
    return json_parse(resp);
  }
}

agent WeatherBot {
  provider: "bedrock",
  model: "anthropic.claude-3-5-sonnet-20241022-v2:0",
  system: "Help users with weather questions.",
  skills: [GetWeather]
}

// This "just works" — the agent sends GetWeather as a Claude tool,
// Claude calls it natively, Neam executes the impl, returns the result
let answer = WeatherBot.ask("What's the weather in Tokyo?");
emit answer;
```

**The user writes zero boilerplate.** The Neam runtime handles:
- Converting `SkillDecl` → Claude `ToolDefinition`
- Detecting `tool_use` in Claude's response
- Executing the skill's `impl` function
- Returning the `tool_result` to Claude
- Looping until Claude produces a final `end_turn` response

---

## Implementation Priority

| Priority | Requirement | Effort | Impact |
|---|---|---|---|
| P0 | R1: Extend provider interface | Small | Enables everything else |
| P0 | R2: Bedrock adapter tool use | Medium | Claude native tool calling |
| P0 | R4: Tool call loop in VM | Medium | Core orchestration logic |
| P0 | R5: Wire agent skills to tools | Small | Connects existing pieces |
| P0 | R6: JSON → Value conversion | Small | Already partially exists |
| P0 | R7: Value → result serialization | Small | Already partially exists |
| P1 | R3: OpenAI function calling | Medium | OpenAI provider support |
| P1 | R8: Error handling | Small | Reliability |
| P1 | R9: Budget/guard integration | Small | Already exists, just wire in |
| P2 | R10: Ollama tool support | Small | Local dev convenience |

**Estimated total effort:** ~600 lines of new/modified C++ across 6 files.

---

## Files to Modify

| File | Changes |
|---|---|
| `NeamC/include/neamc/llm/provider.hpp` | Add ToolDefinition, ToolCall, ChatResponse, ToolResult types; add `chat_with_tools()` virtual method |
| `NeamC/src/vm/llm/bedrock_adapter.cpp` | Override `chat_with_tools()` with Claude Messages API tool format |
| `NeamC/src/vm/llm/openai_adapter.cpp` | Override `chat_with_tools()` with OpenAI function calling format |
| `NeamC/src/vm/vm.cpp` | Tool call loop, `collect_agent_tools()`, `json_args_to_values()`, `value_to_tool_result()` |
| `NeamC/src/vm/llm/ollama_adapter.cpp` | (P2) Override `chat_with_tools()` reusing OpenAI format |

## No Files to Create

All changes fit within existing files. No new headers, no new source files, no new build targets.

---

## Message Flow Diagram

```
User                    Neam VM                   AWS - Bedrock
 │                        │                            │
 │  WeatherBot.ask(       │                            │
 │    "Weather in Tokyo") │                            │
 │───────────────────────>│                            │
 │                        │                            │
 │                        │  chat_with_tools(          │
 │                        │    messages,               │
 │                        │    tools=[GetWeather])     │
 │                        │───────────────────────────>│
 │                        │                            │
 │                        │  ChatResponse:             │
 │                        │    stop_reason="tool_use"  │
 │                        │    tool_call: {            │
 │                        │      id: "toolu_01...",    │
 │                        │      name: "GetWeather",   │
 │                        │      input: {              │
 │                        │        location: "Tokyo",  │
 │                        │        unit: "celsius"     │
 │                        │      }                     │
 │                        │    }                       │
 │                        │<───────────────────────────│
 │                        │                            │
 │                        │  Execute GetWeather impl:  │
 │                        │  http_get(weather API)     │
 │                        │  result = "22°C, sunny"    │
 │                        │                            │
 │                        │  chat_with_tools(          │
 │                        │    messages + [            │
 │                        │      assistant: tool_use,  │
 │                        │      user: tool_result {   │
 │                        │        id: "toolu_01...",  │
 │                        │        content: "22°C..."  │
 │                        │      }                     │
 │                        │    ],                      │
 │                        │    tools=[GetWeather])     │
 │                        │───────────────────────────>│
 │                        │                            │
 │                        │  ChatResponse:             │
 │                        │    stop_reason="end_turn"  │
 │                        │    text: "The weather in   │
 │                        │    Tokyo is 22°C and       │
 │                        │    sunny."                 │
 │                        │<───────────────────────────│
 │                        │                            │
 │  "The weather in Tokyo │                            │
 │   is 22°C and sunny."  │                            │
 │<───────────────────────│                            │
```

---

## Validation Criteria

1. **Existing tests pass** — no regressions in the 85 test cases
2. **Skill-less agents unchanged** — agents without `skills:` behave exactly as before
3. **Bedrock tool calling works** — `WeatherBot.ask()` example above produces correct output
4. **OpenAI tool calling works** — same example with `provider: "openai"` works
5. **Parallel tool calls** — LLM requesting 2+ tools in one response works correctly
6. **Error recovery** — skill throwing exception returns `is_error: true`, LLM retries or explains
7. **Budget enforcement** — tool calls stop when budget exhausted
8. **Guard enforcement** — tool calls rejected when guard returns false
9. **Max iterations** — tool call loop terminates after `NEAM_MAX_REACT_STEPS`
10. **Fallback** — providers without tool support fall back to text-based ReAct
