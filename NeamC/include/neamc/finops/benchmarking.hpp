// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2024-2026 Neam Language Contributors
//
// Neam v0.6.4 - Continuous Benchmarking System
// Performance and cost benchmarking for agent workloads

#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <optional>

namespace neamc::finops {

/**
 * Benchmark metric types
 */
enum class MetricType {
    Latency,        // Response time
    Throughput,     // Requests per second
    TokensPerSec,   // LLM tokens per second
    CostPerRequest, // Cost per request
    CostPerToken,   // Cost per token
    MemoryUsage,    // Peak memory
    CPUUsage,       // CPU utilization
    GPUUsage,       // GPU utilization
    CacheHitRate,   // Cache effectiveness
    ErrorRate,      // Error percentage
    Custom
};

std::string metric_to_string(MetricType type);

/**
 * Benchmark scenario definition
 */
struct BenchmarkScenario {
    std::string id;
    std::string name;
    std::string description;

    // Workload definition
    std::string agent_id;
    std::vector<std::string> test_prompts;
    int concurrent_requests = 1;
    int total_requests = 100;
    std::chrono::seconds duration{0};  // 0 = run until total_requests

    // Configuration variants
    struct Variant {
        std::string name;
        std::string instance_type;
        std::string llm_model;
        int batch_size;
        bool use_cache;
        std::map<std::string, std::string> config;
    };
    std::vector<Variant> variants;

    // Metrics to collect
    std::vector<MetricType> metrics;

    // Tags for filtering
    std::map<std::string, std::string> tags;
};

/**
 * Single benchmark result
 */
struct BenchmarkResult {
    std::string scenario_id;
    std::string variant_name;
    std::chrono::system_clock::time_point timestamp;

    // Latency metrics (milliseconds)
    double latency_p50;
    double latency_p95;
    double latency_p99;
    double latency_min;
    double latency_max;
    double latency_avg;
    double latency_stddev;

    // Throughput
    double requests_per_second;
    double tokens_per_second;

    // Cost metrics (USD)
    double cost_per_request;
    double cost_per_1k_tokens;
    double total_cost;

    // Resource usage
    double avg_cpu_percent;
    double peak_cpu_percent;
    double avg_memory_mb;
    double peak_memory_mb;
    double avg_gpu_percent;
    double peak_gpu_percent;

    // Quality metrics
    double error_rate;
    double cache_hit_rate;
    int total_requests;
    int successful_requests;
    int failed_requests;

    // Custom metrics
    std::map<std::string, double> custom_metrics;

    // Comparison to baseline
    std::optional<std::string> baseline_id;
    std::optional<double> latency_change_pct;
    std::optional<double> throughput_change_pct;
    std::optional<double> cost_change_pct;
};

/**
 * Benchmark comparison
 */
struct BenchmarkComparison {
    std::string baseline_id;
    std::string compare_id;
    std::chrono::system_clock::time_point baseline_timestamp;
    std::chrono::system_clock::time_point compare_timestamp;

    // Percentage changes (positive = improvement for latency/cost, regression for throughput)
    double latency_p50_change_pct;
    double latency_p95_change_pct;
    double throughput_change_pct;
    double cost_change_pct;
    double error_rate_change_pct;

    // Statistical significance
    bool is_significant;
    double p_value;

    // Summary
    std::string summary;
    std::vector<std::string> regressions;
    std::vector<std::string> improvements;
};

/**
 * Benchmark schedule
 */
struct BenchmarkSchedule {
    std::string id;
    std::string scenario_id;
    std::string cron_expression;  // e.g., "0 0 * * *" for daily
    bool enabled = true;
    std::optional<std::chrono::system_clock::time_point> last_run;
    std::optional<std::chrono::system_clock::time_point> next_run;
};

/**
 * Regression detection configuration
 */
struct RegressionConfig {
    double latency_threshold_pct = 10.0;    // Flag if latency increases >10%
    double throughput_threshold_pct = 10.0; // Flag if throughput decreases >10%
    double cost_threshold_pct = 5.0;        // Flag if cost increases >5%
    double error_rate_threshold_pct = 1.0;  // Flag if error rate increases >1%
    int min_samples_for_comparison = 10;
    double significance_level = 0.05;       // p-value threshold
};

/**
 * Regression alert
 */
struct RegressionAlert {
    enum class Severity {
        Warning,
        Error,
        Critical
    };

    std::string scenario_id;
    std::string variant_name;
    Severity severity;
    MetricType metric;
    double baseline_value;
    double current_value;
    double change_pct;
    std::string message;
    std::chrono::system_clock::time_point detected_at;
    std::string comparison_id;
};

/**
 * Continuous Benchmarking System
 *
 * Features:
 * - Automated benchmark execution
 * - Historical trend tracking
 * - Regression detection
 * - Cost/performance comparison across configurations
 * - CI/CD integration
 * - Report generation
 */
class BenchmarkingSystem {
public:
    BenchmarkingSystem();
    ~BenchmarkingSystem();

    // Prevent copying
    BenchmarkingSystem(const BenchmarkingSystem&) = delete;
    BenchmarkingSystem& operator=(const BenchmarkingSystem&) = delete;

    // ==================== Scenario Management ====================

    /**
     * Register benchmark scenario
     */
    void register_scenario(const BenchmarkScenario& scenario);

    /**
     * Update scenario
     */
    void update_scenario(const BenchmarkScenario& scenario);

    /**
     * Remove scenario
     */
    void remove_scenario(const std::string& scenario_id);

    /**
     * Get scenario
     */
    std::optional<BenchmarkScenario> get_scenario(const std::string& scenario_id);

    /**
     * List all scenarios
     */
    std::vector<BenchmarkScenario> list_scenarios();

    // ==================== Benchmark Execution ====================

    /**
     * Run benchmark scenario
     */
    BenchmarkResult run_benchmark(const std::string& scenario_id);

    /**
     * Run benchmark with specific variant
     */
    BenchmarkResult run_benchmark(
        const std::string& scenario_id,
        const std::string& variant_name
    );

    /**
     * Run all variants of a scenario
     */
    std::vector<BenchmarkResult> run_all_variants(const std::string& scenario_id);

    /**
     * Run quick benchmark (reduced iterations for fast feedback)
     */
    BenchmarkResult run_quick_benchmark(
        const std::string& scenario_id,
        int iterations = 10
    );

    /**
     * Run custom benchmark
     */
    BenchmarkResult run_custom_benchmark(
        const std::string& agent_id,
        const std::vector<std::string>& prompts,
        int concurrent = 1
    );

    /**
     * Set benchmark executor callback
     * (Custom implementation for running the actual agent)
     */
    using ExecutorCallback = std::function<std::pair<double, bool>(
        const std::string& agent_id,
        const std::string& prompt
    )>;  // Returns (latency_ms, success)
    void set_executor(ExecutorCallback executor);

    // ==================== Results Management ====================

    /**
     * Get latest result for scenario
     */
    std::optional<BenchmarkResult> get_latest_result(const std::string& scenario_id);

    /**
     * Get result history
     */
    std::vector<BenchmarkResult> get_result_history(
        const std::string& scenario_id,
        int limit = 100
    );

    /**
     * Get results in time range
     */
    std::vector<BenchmarkResult> get_results_in_range(
        const std::string& scenario_id,
        std::chrono::system_clock::time_point start,
        std::chrono::system_clock::time_point end
    );

    /**
     * Set baseline result for comparison
     */
    void set_baseline(
        const std::string& scenario_id,
        const std::string& result_id
    );

    /**
     * Get baseline result
     */
    std::optional<BenchmarkResult> get_baseline(const std::string& scenario_id);

    // ==================== Comparison & Analysis ====================

    /**
     * Compare two results
     */
    BenchmarkComparison compare_results(
        const std::string& baseline_id,
        const std::string& compare_id
    );

    /**
     * Compare latest to baseline
     */
    std::optional<BenchmarkComparison> compare_to_baseline(
        const std::string& scenario_id
    );

    /**
     * Compare variants
     */
    std::vector<BenchmarkComparison> compare_variants(
        const std::string& scenario_id
    );

    /**
     * Get trend analysis
     */
    struct TrendAnalysis {
        std::string scenario_id;
        int data_points;
        double latency_trend;      // Slope of latency over time
        double throughput_trend;   // Slope of throughput over time
        double cost_trend;         // Slope of cost over time
        std::string trend_summary; // "improving", "degrading", "stable"
        std::vector<std::pair<std::chrono::system_clock::time_point, double>> latency_history;
    };
    TrendAnalysis analyze_trend(
        const std::string& scenario_id,
        std::chrono::hours window = std::chrono::hours{24 * 30}
    );

    // ==================== Regression Detection ====================

    /**
     * Set regression detection config
     */
    void set_regression_config(RegressionConfig config);
    RegressionConfig get_regression_config() const;

    /**
     * Check for regressions
     */
    std::vector<RegressionAlert> check_regressions(const std::string& scenario_id);

    /**
     * Check all scenarios for regressions
     */
    std::vector<RegressionAlert> check_all_regressions();

    /**
     * Set regression alert callback
     */
    using RegressionCallback = std::function<void(const RegressionAlert&)>;
    void set_regression_callback(RegressionCallback callback);

    // ==================== Scheduling ====================

    /**
     * Schedule recurring benchmark
     */
    void schedule_benchmark(const BenchmarkSchedule& schedule);

    /**
     * Remove schedule
     */
    void remove_schedule(const std::string& schedule_id);

    /**
     * List schedules
     */
    std::vector<BenchmarkSchedule> list_schedules();

    /**
     * Start scheduler
     */
    void start_scheduler();

    /**
     * Stop scheduler
     */
    void stop_scheduler();

    // ==================== CI/CD Integration ====================

    /**
     * Run CI benchmark (fails if regression detected)
     * Returns exit code (0 = pass, 1 = fail)
     */
    int run_ci_benchmark(
        const std::string& scenario_id,
        const std::string& commit_sha = ""
    );

    /**
     * Generate CI report (JUnit XML format)
     */
    std::string generate_junit_report(const BenchmarkResult& result);

    /**
     * Generate GitHub Actions annotation format
     */
    std::string generate_github_annotations(
        const std::vector<RegressionAlert>& alerts
    );

    // ==================== Export & Reporting ====================

    /**
     * Export results to JSON
     */
    std::string export_json(
        const std::string& scenario_id,
        int limit = 100
    );

    /**
     * Export results to CSV
     */
    std::string export_csv(
        const std::string& scenario_id,
        int limit = 100
    );

    /**
     * Generate markdown report
     */
    std::string generate_report(const std::string& scenario_id);

    /**
     * Generate comparison report
     */
    std::string generate_comparison_report(const BenchmarkComparison& comparison);

    /**
     * Generate trend report
     */
    std::string generate_trend_report(const std::string& scenario_id);

    // ==================== Statistics ====================

    struct Stats {
        int total_scenarios;
        int total_benchmarks_run;
        int regressions_detected;
        int scheduled_benchmarks;
        std::chrono::system_clock::time_point last_benchmark_run;
        double total_benchmark_time_hours;
    };
    Stats get_stats() const;

    /**
     * Cleanup old results
     */
    void cleanup_old_results(std::chrono::hours retention);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * Create benchmarking system
 */
std::unique_ptr<BenchmarkingSystem> create_benchmarking_system();

/**
 * Quick benchmark helper
 */
BenchmarkResult quick_benchmark(
    const std::string& agent_id,
    const std::string& prompt,
    int iterations = 10
);

}  // namespace neamc::finops
