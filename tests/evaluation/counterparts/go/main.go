// Go counterpart — Text + Voice Agent
//
// Compare with Neam: 5-10 LoC agent definition vs ~180 LoC Go
// Go requires explicit struct definitions, error handling at every call site,
// manual JSON marshaling, and verbose AWS SDK setup.
package main

import (
	"bufio"
	"context"
	"encoding/json"
	"fmt"
	"log"
	"os"
	"path/filepath"
	"runtime"
	"sort"
	"time"

	"github.com/aws/aws-sdk-go-v2/config"
	"github.com/aws/aws-sdk-go-v2/service/bedrockruntime"
)

const (
	modelID      = "anthropic.claude-3-5-sonnet-20241022-v2:0"
	systemPrompt = "Answer concisely and accurately."
	maxTokens    = 1024
)

// --- Type definitions (Neam: zero boilerplate, dynamic typing) ---

type PromptEntry struct {
	ID             string `json:"id"`
	Modality       string `json:"modality"`
	Category       string `json:"category"`
	Prompt         string `json:"prompt"`
	Transcript     string `json:"transcript"`
	Expected       string `json:"expected"`
	ExpectedTokens int    `json:"expected_tokens"`
	Complexity     string `json:"complexity"`
}

type BedrockRequest struct {
	AnthropicVersion string    `json:"anthropic_version"`
	MaxTokens        int       `json:"max_tokens"`
	System           string    `json:"system,omitempty"`
	Messages         []Message `json:"messages"`
}

type Message struct {
	Role    string `json:"role"`
	Content string `json:"content"`
}

type BedrockResponse struct {
	Content []ContentBlock `json:"content"`
}

type ContentBlock struct {
	Text string `json:"text"`
}

type Result struct {
	ID        string  `json:"id"`
	Category  string  `json:"category"`
	LatencyMs float64 `json:"latency_ms"`
	Response  string  `json:"response"`
	Tokens    int     `json:"tokens"`
}

type Output struct {
	AgentType     string   `json:"agent_type"`
	Language      string   `json:"language"`
	Version       string   `json:"version"`
	TotalLoC      int      `json:"total_loc"`
	TotalRequests int      `json:"total_requests"`
	TotalTimeMs   float64  `json:"total_time_ms"`
	AvgLatencyMs  float64  `json:"avg_latency_ms"`
	P50LatencyMs  float64  `json:"p50_latency_ms"`
	P95LatencyMs  float64  `json:"p95_latency_ms"`
	P99LatencyMs  float64  `json:"p99_latency_ms"`
	PeakMemBytes  uint64   `json:"peak_memory_bytes"`
	Results       []Result `json:"results"`
}

// --- Provider setup (Neam: provider: "bedrock" — 1 line) ---

func createClient(ctx context.Context) *bedrockruntime.Client {
	cfg, err := config.LoadDefaultConfig(ctx)
	if err != nil {
		log.Fatalf("load AWS config: %v", err)
	}
	return bedrockruntime.NewFromConfig(cfg)
}

// --- Agent invocation (Neam: QA.ask(prompt) — 1 line) ---

func invokeAgent(ctx context.Context, client *bedrockruntime.Client, prompt string) (string, error) {
	req := BedrockRequest{
		AnthropicVersion: "bedrock-2023-10-31",
		MaxTokens:        maxTokens,
		System:           systemPrompt,
		Messages:         []Message{{Role: "user", Content: prompt}},
	}
	body, err := json.Marshal(req)
	if err != nil {
		return "", fmt.Errorf("marshal request: %w", err)
	}

	resp, err := client.InvokeModel(ctx, &bedrockruntime.InvokeModelInput{
		ModelId:     strPtr(modelID),
		Body:        body,
		ContentType: strPtr("application/json"),
	})
	if err != nil {
		return "", fmt.Errorf("invoke model: %w", err)
	}

	var result BedrockResponse
	if err := json.Unmarshal(resp.Body, &result); err != nil {
		return "", fmt.Errorf("unmarshal response: %w", err)
	}
	if len(result.Content) == 0 {
		return "", fmt.Errorf("empty response")
	}
	return result.Content[0].Text, nil
}

func strPtr(s string) *string { return &s }

// --- Prompt loading (Neam: io.read_lines() — 1 line) ---

func loadPrompts(path string) ([]PromptEntry, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()

	var prompts []PromptEntry
	scanner := bufio.NewScanner(f)
	for scanner.Scan() {
		line := scanner.Text()
		if line == "" {
			continue
		}
		var p PromptEntry
		if err := json.Unmarshal([]byte(line), &p); err != nil {
			return nil, fmt.Errorf("parse prompt: %w", err)
		}
		prompts = append(prompts, p)
	}
	return prompts, scanner.Err()
}

func percentile(sorted []float64, p float64) float64 {
	if len(sorted) == 0 {
		return 0
	}
	idx := int(float64(len(sorted)) * p)
	if idx >= len(sorted) {
		idx = len(sorted) - 1
	}
	return sorted[idx]
}

func resolveFile(envKey, fallback string) string {
	if f := os.Getenv(envKey); f != "" {
		return f
	}
	exe, err := os.Executable()
	if err != nil {
		return fallback
	}
	return filepath.Join(filepath.Dir(exe), "..", fallback)
}

func main() {
	ctx := context.Background()
	client := createClient(ctx)

	promptsFile := resolveFile("PROMPTS_FILE", "datasets/text_agents.jsonl")
	prompts, err := loadPrompts(promptsFile)
	if err != nil {
		log.Fatalf("load prompts: %v", err)
	}

	var results []Result
	totalStart := time.Now()

	for _, p := range prompts {
		query := p.Prompt
		if query == "" {
			query = p.Transcript
		}
		if query == "" {
			continue
		}

		cat := p.Category
		if cat != "simple_qa" && cat != "reasoning" && cat != "math" &&
			cat != "voice_pipeline_e2e" {
			continue
		}

		t0 := time.Now()
		answer, err := invokeAgent(ctx, client, query)
		if err != nil {
			log.Printf("prompt %s failed: %v", p.ID, err)
			continue
		}
		latency := time.Since(t0).Seconds() * 1000

		results = append(results, Result{
			ID:        p.ID,
			Category:  cat,
			LatencyMs: latency,
			Response:  answer,
			Tokens:    len(answer),
		})
	}

	totalTimeMs := time.Since(totalStart).Seconds() * 1000

	latencies := make([]float64, len(results))
	for i, r := range results {
		latencies[i] = r.LatencyMs
	}
	sort.Float64s(latencies)

	var memStats runtime.MemStats
	runtime.ReadMemStats(&memStats)

	output := Output{
		AgentType:     "text_qa",
		Language:      "go",
		Version:       runtime.Version(),
		TotalLoC:      180,
		TotalRequests: len(results),
		TotalTimeMs:   totalTimeMs,
		AvgLatencyMs:  totalTimeMs / float64(len(results)),
		P50LatencyMs:  percentile(latencies, 0.50),
		P95LatencyMs:  percentile(latencies, 0.95),
		P99LatencyMs:  percentile(latencies, 0.99),
		PeakMemBytes:  memStats.Sys,
		Results:       results,
	}

	enc := json.NewEncoder(os.Stdout)
	enc.SetIndent("", "  ")
	enc.Encode(output)
}
