// Multi-Language Agent Benchmark — Go agent.
// Calls AWS Bedrock Claude 3.5 Sonnet with shared prompts and reports metrics.
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
	systemPrompt = "Answer concisely in one sentence."
	maxTokens    = 1024
)

type Prompt struct {
	ID             string `json:"id"`
	PromptText     string `json:"prompt"`
	ExpectedTokens int    `json:"expected_tokens"`
	Category       string `json:"category"`
}

type BedrockRequest struct {
	AnthropicVersion string           `json:"anthropic_version"`
	MaxTokens        int              `json:"max_tokens"`
	System           string           `json:"system,omitempty"`
	Messages         []BedrockMessage `json:"messages"`
}

type BedrockMessage struct {
	Role    string `json:"role"`
	Content string `json:"content"`
}

type BedrockResponse struct {
	Content []struct {
		Text string `json:"text"`
	} `json:"content"`
}

type Result struct {
	ID        string  `json:"id"`
	Prompt    string  `json:"prompt"`
	Response  string  `json:"response"`
	LatencyMs float64 `json:"latency_ms"`
}

type Output struct {
	Language       string   `json:"language"`
	Version        string   `json:"version"`
	TotalRequests  int      `json:"total_requests"`
	TotalTimeMs    float64  `json:"total_time_ms"`
	AvgLatencyMs   float64  `json:"avg_latency_ms"`
	P50LatencyMs   float64  `json:"p50_latency_ms"`
	P95LatencyMs   float64  `json:"p95_latency_ms"`
	P99LatencyMs   float64  `json:"p99_latency_ms"`
	PeakMemBytes   uint64   `json:"peak_memory_bytes"`
	Results        []Result `json:"results"`
}

func loadPrompts(path string) ([]Prompt, error) {
	f, err := os.Open(path)
	if err != nil {
		return nil, err
	}
	defer f.Close()

	var prompts []Prompt
	scanner := bufio.NewScanner(f)
	for scanner.Scan() {
		line := scanner.Text()
		if line == "" {
			continue
		}
		var p Prompt
		if err := json.Unmarshal([]byte(line), &p); err != nil {
			return nil, fmt.Errorf("parse prompt: %w", err)
		}
		prompts = append(prompts, p)
	}
	return prompts, scanner.Err()
}

func invokeBedrock(ctx context.Context, client *bedrockruntime.Client, prompt string) (string, error) {
	req := BedrockRequest{
		AnthropicVersion: "bedrock-2023-10-31",
		MaxTokens:        maxTokens,
		System:           systemPrompt,
		Messages: []BedrockMessage{
			{Role: "user", Content: prompt},
		},
	}
	body, err := json.Marshal(req)
	if err != nil {
		return "", err
	}

	resp, err := client.InvokeModel(ctx, &bedrockruntime.InvokeModelInput{
		ModelId:     strPtr(modelID),
		Body:        body,
		ContentType: strPtr("application/json"),
	})
	if err != nil {
		return "", err
	}

	var result BedrockResponse
	if err := json.Unmarshal(resp.Body, &result); err != nil {
		return "", err
	}
	if len(result.Content) == 0 {
		return "", fmt.Errorf("empty response from Bedrock")
	}
	return result.Content[0].Text, nil
}

func strPtr(s string) *string { return &s }

func resolvePromptsFile() string {
	if f := os.Getenv("PROMPTS_FILE"); f != "" {
		return f
	}
	exe, err := os.Executable()
	if err != nil {
		return "../prompts.jsonl"
	}
	return filepath.Join(filepath.Dir(exe), "..", "prompts.jsonl")
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

func main() {
	ctx := context.Background()

	cfg, err := config.LoadDefaultConfig(ctx)
	if err != nil {
		log.Fatalf("load AWS config: %v", err)
	}
	client := bedrockruntime.NewFromConfig(cfg)

	prompts, err := loadPrompts(resolvePromptsFile())
	if err != nil {
		log.Fatalf("load prompts: %v", err)
	}

	var results []Result
	totalStart := time.Now()

	for _, p := range prompts {
		t0 := time.Now()
		answer, err := invokeBedrock(ctx, client, p.PromptText)
		if err != nil {
			log.Printf("prompt %s failed: %v", p.ID, err)
			continue
		}
		latency := time.Since(t0).Seconds() * 1000
		results = append(results, Result{
			ID:        p.ID,
			Prompt:    p.PromptText,
			Response:  answer,
			LatencyMs: latency,
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
		Language:      "go",
		Version:       runtime.Version(),
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
	if err := enc.Encode(output); err != nil {
		log.Fatalf("encode output: %v", err)
	}
}
