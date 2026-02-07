#!/usr/bin/env python3
"""
Multi-Language Agent Benchmark — Python (boto3) agent.
Calls AWS Bedrock Claude 3.5 Sonnet with shared prompts and reports metrics.
"""

import json
import os
import sys
import time
import tracemalloc

import boto3

MODEL_ID = "anthropic.claude-3-5-sonnet-20241022-v2:0"
SYSTEM_PROMPT = "Answer concisely in one sentence."
MAX_TOKENS = 1024


def load_prompts(path: str) -> list[dict]:
    with open(path) as f:
        return [json.loads(line) for line in f if line.strip()]


def invoke_bedrock(client, prompt: str) -> str:
    body = json.dumps(
        {
            "anthropic_version": "bedrock-2023-10-31",
            "max_tokens": MAX_TOKENS,
            "system": SYSTEM_PROMPT,
            "messages": [{"role": "user", "content": prompt}],
        }
    )
    response = client.invoke_model(modelId=MODEL_ID, body=body)
    result = json.loads(response["body"].read())
    return result["content"][0]["text"]


def main():
    prompts_file = os.environ.get("PROMPTS_FILE", "../prompts.jsonl")
    if not os.path.exists(prompts_file):
        # Try relative to script dir
        script_dir = os.path.dirname(os.path.abspath(__file__))
        prompts_file = os.path.join(script_dir, "..", "prompts.jsonl")

    prompts = load_prompts(prompts_file)
    region = os.environ.get("AWS_REGION", "us-east-1")
    client = boto3.client("bedrock-runtime", region_name=region)

    tracemalloc.start()
    results = []
    total_start = time.perf_counter()

    for entry in prompts:
        t0 = time.perf_counter()
        answer = invoke_bedrock(client, entry["prompt"])
        t1 = time.perf_counter()
        latency_ms = (t1 - t0) * 1000
        results.append(
            {
                "id": entry["id"],
                "prompt": entry["prompt"],
                "response": answer,
                "latency_ms": round(latency_ms, 2),
            }
        )

    total_time_ms = (time.perf_counter() - total_start) * 1000
    current_mem, peak_mem = tracemalloc.get_traced_memory()
    tracemalloc.stop()

    latencies = [r["latency_ms"] for r in results]
    latencies.sort()
    n = len(latencies)

    output = {
        "language": "python",
        "version": f"{sys.version_info.major}.{sys.version_info.minor}.{sys.version_info.micro}",
        "total_requests": len(results),
        "total_time_ms": round(total_time_ms, 2),
        "avg_latency_ms": round(total_time_ms / len(results), 2) if results else 0,
        "p50_latency_ms": round(latencies[n // 2], 2) if n else 0,
        "p95_latency_ms": round(latencies[int(n * 0.95)], 2) if n else 0,
        "p99_latency_ms": round(latencies[int(n * 0.99)], 2) if n else 0,
        "peak_memory_bytes": peak_mem,
        "results": results,
    }
    print(json.dumps(output, indent=2))


if __name__ == "__main__":
    main()
