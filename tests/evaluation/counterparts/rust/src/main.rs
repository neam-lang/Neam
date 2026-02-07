// Rust counterpart — Text Agent
//
// Compare with Neam: 5 LoC agent definition vs ~200 LoC Rust
// Rust requires explicit type definitions, lifetime annotations,
// manual serialization traits, verbose error handling, and async runtime setup.

use aws_sdk_bedrockruntime::primitives::Blob;
use serde::{Deserialize, Serialize};
use std::fs::File;
use std::io::{BufRead, BufReader};
use std::time::Instant;

const MODEL_ID: &str = "anthropic.claude-3-5-sonnet-20241022-v2:0";
const SYSTEM_PROMPT: &str = "Answer concisely and accurately.";
const MAX_TOKENS: u32 = 1024;

// --- Type definitions (Neam: zero boilerplate) ---
// In Neam, agents are declared with 5 lines. No structs needed.
// In Rust, every data shape requires explicit type definition.

#[derive(Deserialize)]
struct PromptEntry {
    id: String,
    #[serde(default)]
    modality: String,
    #[serde(default)]
    category: String,
    #[serde(default)]
    prompt: String,
    #[serde(default)]
    transcript: String,
    #[allow(dead_code)]
    #[serde(default)]
    expected: Option<String>,
    #[allow(dead_code)]
    #[serde(default)]
    expected_tokens: Option<u32>,
    #[allow(dead_code)]
    #[serde(default)]
    complexity: Option<String>,
}

#[derive(Serialize)]
struct BedrockRequest {
    anthropic_version: String,
    max_tokens: u32,
    system: String,
    messages: Vec<BedrockMessage>,
}

#[derive(Serialize)]
struct BedrockMessage {
    role: String,
    content: String,
}

#[derive(Deserialize)]
struct BedrockResponse {
    content: Vec<ContentBlock>,
}

#[derive(Deserialize)]
struct ContentBlock {
    text: Option<String>,
}

#[derive(Serialize)]
struct BenchResult {
    id: String,
    category: String,
    latency_ms: f64,
    response: String,
    tokens: usize,
}

#[derive(Serialize)]
struct Output {
    agent_type: String,
    language: String,
    version: String,
    total_loc: u32,
    total_requests: usize,
    total_time_ms: f64,
    avg_latency_ms: f64,
    p50_latency_ms: f64,
    p95_latency_ms: f64,
    p99_latency_ms: f64,
    peak_memory_bytes: u64,
    results: Vec<BenchResult>,
}

// --- Prompt loading (Neam: io.read_lines() — 1 line) ---

fn load_prompts(path: &str) -> Vec<PromptEntry> {
    let file = File::open(path).expect("failed to open prompts file");
    BufReader::new(file)
        .lines()
        .filter_map(|line| {
            let line = line.ok()?;
            if line.trim().is_empty() {
                return None;
            }
            serde_json::from_str(&line).ok()
        })
        .collect()
}

fn percentile(sorted: &[f64], p: f64) -> f64 {
    if sorted.is_empty() {
        return 0.0;
    }
    let idx = ((sorted.len() as f64) * p) as usize;
    sorted[idx.min(sorted.len() - 1)]
}

fn get_peak_memory() -> u64 {
    #[cfg(target_os = "macos")]
    {
        use std::mem::MaybeUninit;
        let mut usage = MaybeUninit::zeroed();
        unsafe {
            if libc::getrusage(libc::RUSAGE_SELF, usage.as_mut_ptr()) == 0 {
                return usage.assume_init().ru_maxrss as u64;
            }
        }
        0
    }
    #[cfg(target_os = "linux")]
    {
        if let Ok(content) = std::fs::read_to_string("/proc/self/status") {
            for line in content.lines() {
                if line.starts_with("VmHWM:") {
                    if let Some(val) = line.split_whitespace().nth(1) {
                        if let Ok(kb) = val.parse::<u64>() {
                            return kb * 1024;
                        }
                    }
                }
            }
        }
        0
    }
    #[cfg(not(any(target_os = "macos", target_os = "linux")))]
    {
        0
    }
}

// --- Agent invocation (Neam: QA.ask(prompt) — 1 line) ---
// In Rust, each API call requires explicit error handling,
// blob conversion, and response parsing.

async fn invoke_agent(
    client: &aws_sdk_bedrockruntime::Client,
    prompt: &str,
) -> Result<String, Box<dyn std::error::Error>> {
    let request = BedrockRequest {
        anthropic_version: "bedrock-2023-10-31".to_string(),
        max_tokens: MAX_TOKENS,
        system: SYSTEM_PROMPT.to_string(),
        messages: vec![BedrockMessage {
            role: "user".to_string(),
            content: prompt.to_string(),
        }],
    };
    let body = serde_json::to_vec(&request)?;

    let resp = client
        .invoke_model()
        .model_id(MODEL_ID)
        .content_type("application/json")
        .body(Blob::new(body))
        .send()
        .await?;

    let parsed: BedrockResponse = serde_json::from_slice(resp.body().as_ref())?;
    Ok(parsed
        .content
        .first()
        .and_then(|b| b.text.clone())
        .unwrap_or_default())
}

#[tokio::main]
async fn main() {
    let prompts_file =
        std::env::var("PROMPTS_FILE").unwrap_or_else(|_| "../datasets/text_agents.jsonl".into());
    let prompts = load_prompts(&prompts_file);

    let config = aws_config::load_defaults(aws_config::BehaviorVersion::latest()).await;
    let client = aws_sdk_bedrockruntime::Client::new(&config);

    let mut results = Vec::new();
    let total_start = Instant::now();

    for entry in &prompts {
        let query = if !entry.prompt.is_empty() {
            &entry.prompt
        } else if !entry.transcript.is_empty() {
            &entry.transcript
        } else {
            continue;
        };

        let cat = &entry.category;
        if cat != "simple_qa" && cat != "reasoning" && cat != "math" {
            continue;
        }

        let t0 = Instant::now();
        match invoke_agent(&client, query).await {
            Ok(answer) => {
                let latency_ms = t0.elapsed().as_secs_f64() * 1000.0;
                let tokens = answer.len();
                results.push(BenchResult {
                    id: entry.id.clone(),
                    category: cat.clone(),
                    latency_ms,
                    response: answer,
                    tokens,
                });
            }
            Err(e) => {
                eprintln!("prompt {} failed: {}", entry.id, e);
            }
        }
    }

    let total_time_ms = total_start.elapsed().as_secs_f64() * 1000.0;
    let mut latencies: Vec<f64> = results.iter().map(|r| r.latency_ms).collect();
    latencies.sort_by(|a, b| a.partial_cmp(b).unwrap());

    let n = results.len();
    let output = Output {
        agent_type: "text_qa".to_string(),
        language: "rust".to_string(),
        version: env!("CARGO_PKG_VERSION").to_string(),
        total_loc: 200,
        total_requests: n,
        total_time_ms,
        avg_latency_ms: if n > 0 {
            total_time_ms / n as f64
        } else {
            0.0
        },
        p50_latency_ms: percentile(&latencies, 0.50),
        p95_latency_ms: percentile(&latencies, 0.95),
        p99_latency_ms: percentile(&latencies, 0.99),
        peak_memory_bytes: get_peak_memory(),
        results,
    };

    println!(
        "{}",
        serde_json::to_string_pretty(&output).expect("serialize output")
    );
}
