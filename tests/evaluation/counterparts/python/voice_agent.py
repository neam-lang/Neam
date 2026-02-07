#!/usr/bin/env python3
"""
Python counterpart — Voice Pipeline Agent (Whisper + boto3 + TTS)

Compare with Neam: 10 LoC agent definition vs ~120 LoC Python
In Neam, the entire STT → Agent → TTS pipeline is declared in the agent block.
In Python, you must manually orchestrate Whisper, Bedrock, and a TTS service.
"""

import json
import os
import sys
import time
import tracemalloc
from io import BytesIO

import boto3

# NOTE: In production, you'd also need:
# import whisper          # OpenAI Whisper for STT (~300MB model download)
# from openai import OpenAI  # For TTS API

MODEL_ID = "anthropic.claude-3-5-sonnet-20241022-v2:0"
SYSTEM_PROMPT = "You are a helpful voice assistant. Give brief, spoken-style answers."
MAX_TOKENS = 512
REGION = os.environ.get("AWS_REGION", "us-east-1")


# -- STT Stage (in Neam: stt_model: "whisper-large-v3" — 1 line) ----------
class SpeechToText:
    """
    In Neam, STT is automatic when stt_model is specified.
    In Python, you need:
    - whisper library (~300MB)
    - Audio preprocessing (sample rate, format conversion)
    - GPU management for Whisper inference
    - Error handling for audio codec issues
    """

    def __init__(self, model_name: str = "whisper-large-v3"):
        self.model_name = model_name
        # In production: self.model = whisper.load_model("large-v3")
        # This alone requires 3GB+ GPU VRAM or ~10GB CPU RAM

    def transcribe(self, audio_path: str) -> dict:
        """Transcribe audio file to text."""
        # Simulated for benchmark — real implementation requires whisper
        return {
            "text": "simulated transcription",
            "language": "en",
            "duration_sec": 3.0,
        }


# -- LLM Stage (in Neam: QA.ask(prompt) — 1 line) -------------------------
class BedrockAgent:
    def __init__(self):
        self.client = boto3.client("bedrock-runtime", region_name=REGION)

    def ask(self, prompt: str) -> str:
        body = json.dumps(
            {
                "anthropic_version": "bedrock-2023-10-31",
                "max_tokens": MAX_TOKENS,
                "system": SYSTEM_PROMPT,
                "messages": [{"role": "user", "content": prompt}],
            }
        )
        response = self.client.invoke_model(modelId=MODEL_ID, body=body)
        result = json.loads(response["body"].read())
        return result["content"][0]["text"]


# -- TTS Stage (in Neam: tts_model: "tts-1-hd" — 1 line) ------------------
class TextToSpeech:
    """
    In Neam, TTS is automatic when tts_model is specified.
    In Python, you need:
    - OpenAI TTS API or another TTS service
    - Audio encoding (mp3/wav output)
    - Streaming support for long responses
    - Voice selection and configuration
    """

    def __init__(self, voice_id: str = "alloy"):
        self.voice_id = voice_id
        # In production: self.openai_client = OpenAI()

    def synthesize(self, text: str) -> bytes:
        """Synthesize text to speech audio."""
        # Simulated for benchmark
        return b"simulated_audio_bytes"


# -- Pipeline Orchestration (in Neam: VoiceAssistant.ask() — automatic) ----
class VoicePipeline:
    """
    In Neam, the entire pipeline is handled by a single agent declaration.
    In Python, you must manually:
    1. Initialize 3 separate services
    2. Handle audio format conversions
    3. Manage error propagation between stages
    4. Handle streaming for real-time performance
    5. Manage GPU resources for Whisper
    """

    def __init__(self):
        self.stt = SpeechToText()
        self.agent = BedrockAgent()
        self.tts = TextToSpeech()

    def process_text_input(self, transcript: str) -> dict:
        """Process pre-transcribed text through the pipeline."""
        t_stt = 0  # Skip STT since we have text
        t_llm_start = time.perf_counter()
        response = self.agent.ask(transcript)
        t_llm = (time.perf_counter() - t_llm_start) * 1000

        t_tts_start = time.perf_counter()
        audio = self.tts.synthesize(response)
        t_tts = (time.perf_counter() - t_tts_start) * 1000

        return {
            "transcript": transcript,
            "response_text": response,
            "audio_bytes": len(audio),
            "stt_latency_ms": t_stt,
            "llm_latency_ms": round(t_llm, 2),
            "tts_latency_ms": round(t_tts, 2),
            "total_latency_ms": round(t_stt + t_llm + t_tts, 2),
        }


def load_prompts(path: str) -> list[dict]:
    with open(path) as f:
        return [json.loads(line) for line in f if line.strip()]


def main():
    prompts_file = os.environ.get("PROMPTS_FILE", "../datasets/voice_agents.jsonl")
    if not os.path.exists(prompts_file):
        script_dir = os.path.dirname(os.path.abspath(__file__))
        prompts_file = os.path.join(script_dir, "..", "datasets", "voice_agents.jsonl")

    scenarios = load_prompts(prompts_file)
    pipeline = VoicePipeline()

    tracemalloc.start()
    results = []
    total_start = time.perf_counter()

    for entry in scenarios:
        if entry.get("category") == "voice_pipeline_e2e":
            result = pipeline.process_text_input(entry["transcript"])
            result["id"] = entry["id"]
            results.append(result)

    total_time_ms = (time.perf_counter() - total_start) * 1000
    _, peak_mem = tracemalloc.get_traced_memory()
    tracemalloc.stop()

    output = {
        "agent_type": "voice_pipeline",
        "language": "python",
        "version": f"{sys.version_info.major}.{sys.version_info.minor}",
        "total_loc": 120,
        "pipeline_stages": ["stt", "agent", "tts"],
        "total_requests": len(results),
        "total_time_ms": round(total_time_ms, 2),
        "peak_memory_bytes": peak_mem,
        "note": "Python voice pipeline requires 3 separate libraries, GPU for Whisper, manual orchestration",
        "results": results,
    }
    print(json.dumps(output, indent=2))


if __name__ == "__main__":
    main()
