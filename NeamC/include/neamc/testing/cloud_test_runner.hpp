// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2024-2026 Neam Language Contributors
//
// Neam v0.6.4 - Cloud Test Runner
// Multi-cloud testing and cost comparison framework

#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <optional>

namespace neamc::testing {

/**
 * Supported cloud providers
 */
enum class CloudProvider {
    AWS,
    GCP,
    Azure,
    Alibaba,
    All
};

std::string provider_to_string(CloudProvider provider);

/**
 * Compute type for testing
 */
enum class ComputeType {
    Serverless,     // Lambda, Functions, Cloud Functions
    Container,      // ECS, Cloud Run, Container Apps
    VM,             // EC2, Compute Engine, VMs
    VM_GPU,         // GPU instances
    Managed_LLM     // Bedrock, Vertex AI, Azure OpenAI
};

std::string compute_to_string(ComputeType type);

/**
 * Test prompt
 */
struct TestPrompt {
    std::string id;
    std::string prompt;
    int expected_tokens;
    std::string category;
};

/**
 * Single test result
 */
struct TestResult {
    std::string test_id;
    std::string prompt_id;
    CloudProvider provider;
    std::string region;
    ComputeType compute_type;
    std::string instance_type;

    // Timing
    std::chrono::system_clock::time_point started_at;
    std::chrono::system_clock::time_point completed_at;
    double latency_ms;
    double cold_start_ms;

    // Tokens
    int input_tokens;
    int output_tokens;

    // Cost
    double compute_cost_usd;
    double llm_cost_usd;
    double data_transfer_cost_usd;
    double total_cost_usd;

    // Quality
    bool success;
    std::string error_message;
    std::string response;

    // Derived
    double cost_per_request() const { return total_cost_usd; }
    double cost_per_1k_tokens() const {
        int total = input_tokens + output_tokens;
        return total > 0 ? (total_cost_usd / total) * 1000 : 0;
    }
};

/**
 * Aggregated test metrics
 */
struct TestMetrics {
    CloudProvider provider;
    std::string region;
    ComputeType compute_type;
    std::string instance_type;

    // Request counts
    int total_requests;
    int successful_requests;
    int failed_requests;
    double error_rate_percent;

    // Latency (ms)
    double latency_min;
    double latency_max;
    double latency_avg;
    double latency_p50;
    double latency_p95;
    double latency_p99;
    double latency_stddev;

    // Cold start (ms)
    double cold_start_avg;
    double cold_start_p99;

    // Throughput
    double requests_per_second;
    double tokens_per_second;

    // Cost
    double total_cost_usd;
    double cost_per_request_usd;
    double cost_per_1k_tokens_usd;
    double compute_cost_usd;
    double llm_cost_usd;

    // Tokens
    int total_input_tokens;
    int total_output_tokens;
};

/**
 * Cost comparison between providers
 */
struct CostComparison {
    std::string metric_name;
    std::string unit;

    struct ProviderValue {
        CloudProvider provider;
        std::string region;
        ComputeType compute_type;
        double value;
        double vs_baseline_percent;  // Negative = cheaper
        int rank;
    };

    std::vector<ProviderValue> values;
    CloudProvider cheapest_provider;
    double cheapest_value;
    double max_savings_percent;
};

/**
 * Cost comparison report
 */
struct CostComparisonReport {
    std::string report_id;
    std::string title;
    std::chrono::system_clock::time_point generated_at;

    // Test configuration
    std::string config_file;
    std::vector<std::string> agents_tested;
    std::vector<std::string> workloads_tested;
    std::vector<CloudProvider> providers_tested;
    std::vector<ComputeType> compute_types_tested;

    // Overall summary
    struct Summary {
        CloudProvider most_cost_effective_provider;
        CloudProvider lowest_latency_provider;
        CloudProvider highest_throughput_provider;
        double total_test_cost_usd;
        int total_requests;
        std::chrono::seconds total_test_duration;
    };
    Summary summary;

    // Comparisons
    std::vector<CostComparison> cost_comparisons;
    std::vector<CostComparison> latency_comparisons;
    std::vector<CostComparison> throughput_comparisons;

    // Per-provider metrics
    std::map<CloudProvider, TestMetrics> provider_metrics;

    // Recommendations
    struct Recommendation {
        std::string title;
        std::string description;
        CloudProvider recommended_provider;
        ComputeType recommended_compute;
        std::string region;
        double estimated_monthly_savings_usd;
        std::string use_case;  // "low_latency", "high_throughput", "cost_optimized"
    };
    std::vector<Recommendation> recommendations;

    // Detailed results
    std::vector<TestResult> all_results;
};

/**
 * Test configuration
 */
struct CloudTestConfig {
    std::string config_file;
    std::vector<std::string> enabled_providers;
    std::vector<std::string> enabled_compute_types;
    std::vector<std::string> enabled_workloads;
    int requests_per_workload;
    int concurrency;
    bool include_warmup;
    int warmup_requests;
    bool collect_traces;
    std::string output_dir;
};

/**
 * Cloud Test Runner - Multi-cloud testing framework
 *
 * Features:
 * - Parallel testing across AWS, GCP, Azure, Alibaba
 * - Support for serverless, containers, VMs, GPU
 * - Automated cost tracking and comparison
 * - Latency and throughput benchmarking
 * - Comprehensive reporting
 */
class CloudTestRunner {
public:
    CloudTestRunner();
    explicit CloudTestRunner(const std::string& config_file);
    ~CloudTestRunner();

    // Prevent copying
    CloudTestRunner(const CloudTestRunner&) = delete;
    CloudTestRunner& operator=(const CloudTestRunner&) = delete;

    // ==================== Configuration ====================

    /**
     * Load configuration from TOML file
     */
    void load_config(const std::string& config_file);

    /**
     * Set test configuration
     */
    void set_config(CloudTestConfig config);
    CloudTestConfig get_config() const;

    /**
     * Enable/disable specific provider
     */
    void enable_provider(CloudProvider provider, bool enabled = true);

    /**
     * Enable/disable specific compute type
     */
    void enable_compute_type(ComputeType type, bool enabled = true);

    /**
     * Set cloud credentials
     */
    void set_credentials(CloudProvider provider, const std::map<std::string, std::string>& credentials);

    // ==================== Test Execution ====================

    /**
     * Run all configured tests
     */
    CostComparisonReport run_all_tests();

    /**
     * Run tests for specific provider
     */
    std::vector<TestResult> run_provider_tests(CloudProvider provider);

    /**
     * Run tests for specific compute type
     */
    std::vector<TestResult> run_compute_tests(ComputeType type);

    /**
     * Run single workload
     */
    std::vector<TestResult> run_workload(
        const std::string& workload_id,
        CloudProvider provider,
        ComputeType compute_type
    );

    /**
     * Run single prompt
     */
    TestResult run_single_test(
        const TestPrompt& prompt,
        CloudProvider provider,
        ComputeType compute_type,
        const std::string& instance_type
    );

    /**
     * Run warmup
     */
    void run_warmup(CloudProvider provider, ComputeType compute_type);

    // ==================== Progress Callbacks ====================

    using ProgressCallback = std::function<void(
        int completed,
        int total,
        const std::string& current_test
    )>;
    void set_progress_callback(ProgressCallback callback);

    using ResultCallback = std::function<void(const TestResult& result)>;
    void set_result_callback(ResultCallback callback);

    // ==================== Cost Comparison ====================

    /**
     * Generate cost comparison report
     */
    CostComparisonReport generate_report(const std::vector<TestResult>& results);

    /**
     * Compare specific metric across providers
     */
    CostComparison compare_metric(
        const std::string& metric_name,
        const std::vector<TestResult>& results,
        CloudProvider baseline = CloudProvider::AWS
    );

    /**
     * Get recommendations based on results
     */
    std::vector<CostComparisonReport::Recommendation> get_recommendations(
        const std::vector<TestResult>& results
    );

    // ==================== Metrics Aggregation ====================

    /**
     * Calculate aggregated metrics
     */
    TestMetrics calculate_metrics(const std::vector<TestResult>& results);

    /**
     * Group results by provider
     */
    std::map<CloudProvider, std::vector<TestResult>> group_by_provider(
        const std::vector<TestResult>& results
    );

    /**
     * Group results by compute type
     */
    std::map<ComputeType, std::vector<TestResult>> group_by_compute(
        const std::vector<TestResult>& results
    );

    // ==================== Export ====================

    /**
     * Export report to JSON
     */
    std::string export_json(const CostComparisonReport& report);

    /**
     * Export report to CSV
     */
    std::string export_csv(const CostComparisonReport& report);

    /**
     * Export report to HTML
     */
    std::string export_html(const CostComparisonReport& report);

    /**
     * Export report to Markdown
     */
    std::string export_markdown(const CostComparisonReport& report);

    /**
     * Save report to file
     */
    void save_report(
        const CostComparisonReport& report,
        const std::string& output_dir,
        const std::vector<std::string>& formats = {"json", "html", "csv"}
    );

    // ==================== Statistics ====================

    struct Stats {
        int total_tests_run;
        int successful_tests;
        int failed_tests;
        double total_cost_usd;
        std::chrono::seconds total_duration;
        std::map<CloudProvider, int> tests_by_provider;
    };
    Stats get_stats() const;

    void reset_stats();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * Create cloud test runner
 */
std::unique_ptr<CloudTestRunner> create_cloud_test_runner();

/**
 * Create cloud test runner with config file
 */
std::unique_ptr<CloudTestRunner> create_cloud_test_runner(const std::string& config_file);

/**
 * Load test prompts from JSONL file
 */
std::vector<TestPrompt> load_prompts(const std::string& file_path);

}  // namespace neamc::testing
