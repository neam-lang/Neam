#!/usr/bin/env python3
"""
Python counterpart — Text Q&A Agent (boto3 + LangChain-style)

Compare with Neam: 5 LoC agent definition vs ~65 LoC Python
This file demonstrates the boilerplate required in Python to achieve
what Neam does in a few declarative lines.
"""

import json
import os
import sys
import time
import tracemalloc

import boto3

# -- Configuration (in Neam: 3 lines in agent{} block) ---------------------
MODEL_ID = "anthropic.claude-3-5-sonnet-20241022-v2:0"
SYSTEM_PROMPT = "Answer concisely and accurately."
MAX_TOKENS = 1024
REGION = os.environ.get("AWS_REGION", "us-east-1")


# -- Provider setup (in Neam: provider: "bedrock" — 1 line) ----------------
def create_bedrock_client():
    """Create and configure Bedrock client — Neam does this automatically."""
    return boto3.client("bedrock-runtime", region_name=REGION)


# -- Agent invocation (in Neam: QA.ask(prompt) — 1 line) -------------------
def invoke_agent(client, prompt: str, system: str = SYSTEM_PROMPT) -> str:
    """Invoke Bedrock model — Neam handles serialization internally."""
    body = json.dumps(
        {
            "anthropic_version": "bedrock-2023-10-31",
            "max_tokens": MAX_TOKENS,
            "system": system,
            "messages": [{"role": "user", "content": prompt}],
        }
    )
    response = client.invoke_model(modelId=MODEL_ID, body=body)
    result = json.loads(response["body"].read())
    return result["content"][0]["text"]


# -- Prompt loading (in Neam: io.read_lines() — 1 line) -------------------
def load_prompts(path: str) -> list[dict]:
    """Load JSONL prompts — Neam has built-in JSONL support."""
    with open(path) as f:
        return [json.loads(line) for line in f if line.strip()]


# -- Metrics collection (in Neam: built-in tracing) -----------------------
def collect_metrics(results: list, total_time_ms: float, peak_mem: int) -> dict:
    """Collect and format metrics — Neam traces automatically."""
    latencies = sorted(r["latency_ms"] for r in results)
    n = len(latencies)
    return {
        "agent_type": "text_qa",
        "language": "python",
        "version": f"{sys.version_info.major}.{sys.version_info.minor}",
        "total_loc": 65,
        "total_requests": n,
        "total_time_ms": round(total_time_ms, 2),
        "avg_latency_ms": round(total_time_ms / n, 2) if n else 0,
        "p50_latency_ms": round(latencies[n // 2], 2) if n else 0,
        "p95_latency_ms": round(latencies[int(n * 0.95)], 2) if n else 0,
        "p99_latency_ms": round(latencies[int(n * 0.99)], 2) if n else 0,
        "peak_memory_bytes": peak_mem,
        "results": results,
    }


# -- Main execution loop ---------------------------------------------------
def main():
    prompts_file = os.environ.get("PROMPTS_FILE", "../datasets/text_agents.jsonl")
    if not os.path.exists(prompts_file):
        script_dir = os.path.dirname(os.path.abspath(__file__))
        prompts_file = os.path.join(script_dir, "..", "datasets", "text_agents.jsonl")

    prompts = load_prompts(prompts_file)
    client = create_bedrock_client()

    tracemalloc.start()
    results = []
    total_start = time.perf_counter()

    for entry in prompts:
        if entry.get("category") in ("simple_qa", "reasoning", "math"):
            t0 = time.perf_counter()
            answer = invoke_agent(client, entry["prompt"])
            t1 = time.perf_counter()
            results.append(
                {
                    "id": entry["id"],
                    "latency_ms": round((t1 - t0) * 1000, 2),
                    "response": answer,
                    "tokens": len(answer),
                }
            )

    total_time_ms = (time.perf_counter() - total_start) * 1000
    _, peak_mem = tracemalloc.get_traced_memory()
    tracemalloc.stop()

    output = collect_metrics(results, total_time_ms, peak_mem)
    print(json.dumps(output, indent=2))


if __name__ == "__main__":
    main()
