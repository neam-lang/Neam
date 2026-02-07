// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2024-2026 Neam Language Contributors
//
// Neam v0.6.4 - Cost Report Generator
// Generates detailed cost comparison reports across cloud providers

#pragma once

#include "cloud_test_runner.hpp"
#include <chrono>
#include <memory>
#include <string>
#include <vector>
#include <map>

namespace neamc::testing {

/**
 * Report format options
 */
enum class ReportFormat {
    JSON,
    CSV,
    HTML,
    Markdown,
    PDF,
    Excel
};

/**
 * Chart type for visualizations
 */
enum class ChartType {
    Bar,
    Line,
    Pie,
    Heatmap,
    Scatter,
    Table
};

/**
 * Report section
 */
struct ReportSection {
    std::string id;
    std::string title;
    std::string description;
    ChartType chart_type;
    std::string data_json;  // Chart data in JSON format
    std::string html_content;
};

/**
 * Cost breakdown by category
 */
struct CostBreakdown {
    double compute_cost_usd;
    double llm_api_cost_usd;
    double embedding_cost_usd;
    double storage_cost_usd;
    double data_transfer_cost_usd;
    double other_cost_usd;
    double total_cost_usd;

    // Percentages
    double compute_percent;
    double llm_percent;
    double embedding_percent;
    double storage_percent;
    double data_transfer_percent;
};

/**
 * Monthly cost projection
 */
struct MonthlyProjection {
    CloudProvider provider;
    ComputeType compute_type;
    std::string instance_type;
    std::string region;

    // Assumptions
    int requests_per_day;
    int avg_input_tokens;
    int avg_output_tokens;
    double utilization_percent;

    // Projected costs
    double daily_cost_usd;
    double monthly_cost_usd;
    double yearly_cost_usd;

    // Breakdown
    CostBreakdown breakdown;

    // Comparison
    double vs_baseline_monthly_usd;
    double savings_percent;
};

/**
 * Provider comparison matrix
 */
struct ProviderMatrix {
    std::vector<CloudProvider> providers;
    std::vector<std::string> metrics;
    std::vector<std::vector<double>> values;  // [provider][metric]
    std::vector<std::vector<int>> ranks;      // [provider][metric]
};

/**
 * Regional comparison
 */
struct RegionalComparison {
    CloudProvider provider;
    std::vector<std::string> regions;
    std::map<std::string, TestMetrics> metrics_by_region;
    std::string cheapest_region;
    std::string lowest_latency_region;
};

/**
 * Cost Report Generator Configuration
 */
struct ReportConfig {
    std::string report_title = "Neam Cloud Cost Comparison Report";
    std::string company_name = "";
    std::string logo_path = "";

    // Sections to include
    bool include_executive_summary = true;
    bool include_cost_breakdown = true;
    bool include_latency_analysis = true;
    bool include_throughput_analysis = true;
    bool include_regional_comparison = true;
    bool include_recommendations = true;
    bool include_monthly_projections = true;
    bool include_raw_data = false;

    // Projection assumptions
    int projected_daily_requests = 10000;
    int projected_avg_input_tokens = 500;
    int projected_avg_output_tokens = 1000;

    // Visualization
    bool include_charts = true;
    std::string chart_theme = "professional";  // "professional", "dark", "colorful"

    // Baseline for comparison
    CloudProvider baseline_provider = CloudProvider::AWS;
    ComputeType baseline_compute = ComputeType::Serverless;
};

/**
 * Cost Report Generator
 *
 * Features:
 * - Multi-format report generation (JSON, CSV, HTML, PDF)
 * - Interactive charts and visualizations
 * - Monthly cost projections
 * - Regional comparison analysis
 * - Executive summary with recommendations
 */
class CostReportGenerator {
public:
    CostReportGenerator();
    explicit CostReportGenerator(ReportConfig config);
    ~CostReportGenerator();

    // Prevent copying
    CostReportGenerator(const CostReportGenerator&) = delete;
    CostReportGenerator& operator=(const CostReportGenerator&) = delete;

    // ==================== Configuration ====================

    void set_config(ReportConfig config);
    ReportConfig get_config() const;

    // ==================== Report Generation ====================

    /**
     * Generate full report from test results
     */
    CostComparisonReport generate(const std::vector<TestResult>& results);

    /**
     * Generate executive summary
     */
    std::string generate_executive_summary(const CostComparisonReport& report);

    /**
     * Generate cost breakdown section
     */
    ReportSection generate_cost_breakdown_section(const CostComparisonReport& report);

    /**
     * Generate latency analysis section
     */
    ReportSection generate_latency_section(const CostComparisonReport& report);

    /**
     * Generate throughput analysis section
     */
    ReportSection generate_throughput_section(const CostComparisonReport& report);

    /**
     * Generate provider comparison matrix
     */
    ProviderMatrix generate_provider_matrix(const CostComparisonReport& report);

    /**
     * Generate regional comparison
     */
    std::vector<RegionalComparison> generate_regional_comparisons(
        const CostComparisonReport& report
    );

    /**
     * Generate monthly projections
     */
    std::vector<MonthlyProjection> generate_monthly_projections(
        const CostComparisonReport& report
    );

    /**
     * Generate recommendations
     */
    std::vector<CostComparisonReport::Recommendation> generate_recommendations(
        const CostComparisonReport& report
    );

    // ==================== Export ====================

    /**
     * Export to specific format
     */
    std::string export_report(
        const CostComparisonReport& report,
        ReportFormat format
    );

    /**
     * Export to JSON
     */
    std::string export_json(const CostComparisonReport& report);

    /**
     * Export to CSV (multiple files in zip)
     */
    std::vector<uint8_t> export_csv_zip(const CostComparisonReport& report);

    /**
     * Export to HTML with interactive charts
     */
    std::string export_html(const CostComparisonReport& report);

    /**
     * Export to Markdown
     */
    std::string export_markdown(const CostComparisonReport& report);

    /**
     * Export to PDF
     */
    std::vector<uint8_t> export_pdf(const CostComparisonReport& report);

    /**
     * Save report to directory
     */
    void save_report(
        const CostComparisonReport& report,
        const std::string& output_dir,
        const std::vector<ReportFormat>& formats
    );

    // ==================== Chart Generation ====================

    /**
     * Generate chart HTML
     */
    std::string generate_chart(
        ChartType type,
        const std::string& title,
        const std::string& data_json
    );

    /**
     * Generate cost comparison bar chart
     */
    std::string generate_cost_chart(const CostComparisonReport& report);

    /**
     * Generate latency comparison chart
     */
    std::string generate_latency_chart(const CostComparisonReport& report);

    /**
     * Generate provider heatmap
     */
    std::string generate_provider_heatmap(const ProviderMatrix& matrix);

    /**
     * Generate cost breakdown pie chart
     */
    std::string generate_breakdown_pie(const CostBreakdown& breakdown);

    // ==================== Templates ====================

    /**
     * Set custom HTML template
     */
    void set_html_template(const std::string& template_html);

    /**
     * Set custom CSS styles
     */
    void set_custom_css(const std::string& css);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * Create cost report generator
 */
std::unique_ptr<CostReportGenerator> create_cost_report_generator();

/**
 * Create cost report generator with config
 */
std::unique_ptr<CostReportGenerator> create_cost_report_generator(ReportConfig config);

}  // namespace neamc::testing
