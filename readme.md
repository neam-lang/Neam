# Neam sample programs

Quick examples you can use to exercise the current `neamc` compiler + VM pipeline.

## Knowledge bases (RAG)

Neam supports first-class knowledge bases via the `knowledge` declaration. Knowledge bases ingest
sources at runtime and expose relevant context to connected agents through `connected_knowledge`.

```neam
knowledge NeamDocs {
  vector_store: "usearch",
  embedding_model: "nomic-embed-text",
  chunk_size: 128,
  chunk_overlap: 32,
  sources: [
    { type: "file", path: "./readme.md" }
  ]
}

agent SupportBot {
  provider: "ollama",
  model: "qwen2.5:14b",
  system: "You answer using the provided knowledge context.",
  connected_knowledge: [NeamDocs]
}

{
  emit "Starting Knowledge Query...";
  let answer = SupportBot.ask("How do I build NeamC?");
  emit answer;
}
```

### Knowledge configuration fields

* `vector_store`: The embedded vector backend (currently `usearch`).
* `embedding_model`: The embedding model identifier (used for future provider wiring).
* `chunk_size`: Sliding window token count per chunk.
* `chunk_overlap`: Number of overlapping tokens between chunks.
* `sources`: Array of `{ type, path }` source descriptors.

### Supported source types

* `file`: Ingest local files. Paths can include `*` wildcards.
* `web`: Fetch and ingest HTML content from a URL.

### Sample programs

* `examples/knowledge_simple.neam`: Basic RAG query pipeline using a local file source.
* `examples/rag_researcher.neam`: Research-focused agent connected to multiple docs.
* `tests/knowledge_basic.neam`: Minimal knowledge initialization and query for smoke tests.

## Simple arithmetic

```neam
{ 1 + 2; }
```

Compile and run:

```bash
neamc math.neam -o math.neamb
neam math.neamb
```

## Nested blocks and duplication

```neam
{
  3 * (4 + 5);
  {
    -1 + 2;
  }
}
```

This demonstrates block statements and unary negation.

## Chained expressions

```neam
{
  10 / 2 + 7;
  (8 - 3) * 4;
}
```

Multiple expressions in a block are compiled sequentially; each expression result is popped to keep the stack clean.

## Negative numbers

```neam
{
  -42;
  -(1 + 2 * 3);
}
```

Unary negation lowers to `OP_NEGATE` before arithmetic combines the values.

## Agentic AI patterns (conceptual)

The current parser focuses on arithmetic expressions, but the runtime model already includes `AgentRef` values. Below are forward-looking examples to illustrate intended usage once agent declarations and events are wired through the compiler:

```neam
agent Planner {
  // Future: plan tasks and emit structured intents
}

agent Worker {
  // Future: execute intents and emit receipts
}

{
  Planner.plan("summarize report");
  Worker.execute(Planner.last_plan);
}
```

```neam
{
  // Conceptual event emission pipeline
  let decision = Planner.decide("route inquiry");
  emit decision;
  emit "hand-off to worker";
}
```

These snippets are illustrative; parser and codegen support for agent declarations, method calls, and event emission will arrive in later phases.

### Multi-agent orchestration sketch

```neam
agent Router { }
agent Summarizer { }
agent Reviewer { }

{
  // Router inspects the request and chooses a path
  let route = Router.decide("summarize vs. translate");
  emit route;

  // Summarizer executes then emits a receipt
  let summary = Summarizer.summarize("input doc");
  emit summary;

  // Reviewer validates downstream
  let verdict = Reviewer.review(summary);
  emit verdict;
}
```

### Supervisor with retries

```neam
agent Supervisor { }
agent Worker { }

{
  let attempt = 0;
  let success = false;

  while (!success && attempt < 3) {
    attempt = attempt + 1;
    let result = Worker.execute("task payload");
    success = Supervisor.validate(result);
    emit "attempt " + attempt;
    emit result;
  }

  if (!success) {
    emit "fallback escalation";
  }
}
```

### Event-driven tool invocation

```neam
agent Planner { }
agent Toolbelt { }

{
  let plan = Planner.plan("extract key facts");
  emit plan;

  // Hypothetical tool call sequence
  let data = Toolbelt.call("search", "topic query");
  let notes = Toolbelt.call("summarize", data);
  emit notes;
}
```
