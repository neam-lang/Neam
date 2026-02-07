// Multi-Language Agent Benchmark — Rust agent.
// Calls AWS Bedrock Claude 3.5 Sonnet with shared prompts and reports metrics.

use aws_sdk_bedrockruntime::primitives::Blob;
use serde::{Deserialize, Serialize};
use std::fs::File;
use std::io::{BufRead, BufReader};
use std::path::PathBuf;
use std::time::Instant;

const MODEL_ID: &str = "anthropic.claude-3-5-sonnet-20241022-v2:0";
const SYSTEM_PROMPT: &str = "Answer concisely in one sentence.";
const MAX_TOKENS: u32 = 1024;

#[derive(Deserialize)]
struct PromptEntry {
    id: String,
    prompt: String,
    #[allow(dead_code)]
    expected_tokens: Option<u32>,
    #[allow(dead_code)]
    category: Option<String>,
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
    prompt: String,
    response: String,
    latency_ms: f64,
}

#[derive(Serialize)]
struct Output {
    language: String,
    version: String,
    total_requests: usize,
    total_time_ms: f64,
    avg_latency_ms: f64,
    p50_latency_ms: f64,
    p95_latency_ms: f64,
    p99_latency_ms: f64,
    peak_memory_bytes: u64,
    results: Vec<BenchResult>,
}

fn load_prompts(path: &str) -> Vec<PromptEntry> {
    let file = File::open(path).expect("failed to open prompts file");
    let reader = BufReader::new(file);
    reader
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
    // macOS: use rusage
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
    // Linux: read /proc/self/status
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

fn resolve_prompts_file() -> String {
    if let Ok(f) = std::env::var("PROMPTS_FILE") {
        return f;
    }
    let exe = std::env::current_exe().unwrap_or_default();
    let dir = exe.parent().unwrap_or(&PathBuf::from("."));
    dir.join("../prompts.jsonl").to_string_lossy().to_string()
}

#[tokio::main]
async fn main() {
    let prompts_file = resolve_prompts_file();
    let prompts = load_prompts(&prompts_file);

    let config = aws_config::load_defaults(aws_config::BehaviorVersion::latest()).await;
    let client = aws_sdk_bedrockruntime::Client::new(&config);

    let mut results = Vec::new();
    let total_start = Instant::now();

    for entry in &prompts {
        let request = BedrockRequest {
            anthropic_version: "bedrock-2023-10-31".to_string(),
            max_tokens: MAX_TOKENS,
            system: SYSTEM_PROMPT.to_string(),
            messages: vec![BedrockMessage {
                role: "user".to_string(),
                content: entry.prompt.clone(),
            }],
        };
        let body = serde_json::to_vec(&request).expect("serialize request");

        let t0 = Instant::now();
        let resp = client
            .invoke_model()
            .model_id(MODEL_ID)
            .content_type("application/json")
            .body(Blob::new(body))
            .send()
            .await;

        let latency_ms = t0.elapsed().as_secs_f64() * 1000.0;

        match resp {
            Ok(output) => {
                let bytes = output.body().as_ref();
                if let Ok(parsed) = serde_json::from_slice::<BedrockResponse>(bytes) {
                    let text = parsed
                        .content
                        .first()
                        .and_then(|b| b.text.clone())
                        .unwrap_or_default();
                    results.push(BenchResult {
                        id: entry.id.clone(),
                        prompt: entry.prompt.clone(),
                        response: text,
                        latency_ms,
                    });
                }
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
        language: "rust".to_string(),
        version: env!("CARGO_PKG_VERSION").to_string(),
        total_requests: n,
        total_time_ms,
        avg_latency_ms: if n > 0 { total_time_ms / n as f64 } else { 0.0 },
        p50_latency_ms: percentile(&latencies, 0.50),
        p95_latency_ms: percentile(&latencies, 0.95),
        p99_latency_ms: percentile(&latencies, 0.99),
        peak_memory_bytes: get_peak_memory(),
        results,
    };

    let json = serde_json::to_string_pretty(&output).expect("serialize output");
    println!("{json}");
}
