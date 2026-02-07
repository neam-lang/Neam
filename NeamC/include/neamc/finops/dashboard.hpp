// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2024-2026 Neam Language Contributors
//
// Neam v0.6.4 - FinOps Dashboard API
// Real-time cost and performance monitoring dashboard

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
 * Dashboard widget types
 */
enum class WidgetType {
    CostOverTime,       // Line chart of costs
    CostByCategory,     // Pie chart by category
    CostByAgent,        // Bar chart by agent
    CostByProvider,     // Pie chart by cloud provider
    BudgetGauge,        // Budget utilization gauge
    TopSpenders,        // Table of top spending agents
    CostRate,           // Real-time cost rate
    SavingsOpportunity, // Savings recommendations
    LatencyTrend,       // Latency over time
    ThroughputTrend,    // Throughput over time
    ResourceUtilization,// CPU/Memory/GPU gauges
    AlertsFeed,         // Live alerts feed
    Custom
};

/**
 * Time range for dashboard queries
 */
struct TimeRange {
    enum class Preset {
        Last1Hour,
        Last6Hours,
        Last24Hours,
        Last7Days,
        Last30Days,
        CurrentMonth,
        LastMonth,
        Custom
    };

    Preset preset = Preset::Last24Hours;
    std::optional<std::chrono::system_clock::time_point> custom_start;
    std::optional<std::chrono::system_clock::time_point> custom_end;
};

/**
 * Dashboard data point
 */
struct DataPoint {
    std::chrono::system_clock::time_point timestamp;
    double value;
    std::string label;
};

/**
 * Dashboard chart data
 */
struct ChartData {
    std::string title;
    std::string x_label;
    std::string y_label;
    std::string unit;  // "$", "ms", "req/s", etc.
    std::vector<DataPoint> series;
    std::map<std::string, std::vector<DataPoint>> multi_series;
};

/**
 * Dashboard gauge data
 */
struct GaugeData {
    std::string title;
    double value;
    double min_value = 0.0;
    double max_value = 100.0;
    std::string unit;
    std::vector<std::pair<double, std::string>> thresholds;  // (value, color)
};

/**
 * Dashboard table data
 */
struct TableData {
    std::string title;
    std::vector<std::string> columns;
    std::vector<std::vector<std::string>> rows;
};

/**
 * Dashboard alert for feed
 */
struct DashboardAlert {
    enum class Level {
        Info,
        Warning,
        Error,
        Critical
    };

    std::string id;
    Level level;
    std::string title;
    std::string message;
    std::string source;
    std::chrono::system_clock::time_point timestamp;
    bool acknowledged = false;
};

/**
 * Dashboard summary cards
 */
struct SummaryCards {
    // Cost summary
    double current_daily_spend;
    double projected_daily_spend;
    double month_to_date_spend;
    double budget_remaining;
    double budget_utilization_pct;

    // Performance summary
    double avg_latency_ms;
    double p99_latency_ms;
    double requests_per_second;
    double error_rate_pct;

    // Resource summary
    double avg_cpu_utilization;
    double avg_memory_utilization;
    double avg_gpu_utilization;

    // Savings summary
    double potential_savings_monthly;
    int open_recommendations;
    int critical_recommendations;

    // Comparisons
    double cost_change_vs_yesterday_pct;
    double cost_change_vs_last_week_pct;
    double latency_change_vs_yesterday_pct;
};

/**
 * Dashboard configuration
 */
struct DashboardConfig {
    std::string name = "Neam FinOps Dashboard";
    TimeRange default_time_range;
    std::chrono::seconds refresh_interval{30};
    std::vector<WidgetType> enabled_widgets;
    bool dark_mode = false;
    std::string timezone = "UTC";
};

/**
 * FinOps Dashboard - Real-time monitoring interface
 *
 * Features:
 * - Real-time cost and performance metrics
 * - Budget tracking and alerts
 * - Trend visualization
 * - Savings recommendations
 * - WebSocket streaming for live updates
 * - Embeddable widget API
 */
class Dashboard {
public:
    Dashboard();
    explicit Dashboard(DashboardConfig config);
    ~Dashboard();

    // Prevent copying
    Dashboard(const Dashboard&) = delete;
    Dashboard& operator=(const Dashboard&) = delete;

    // ==================== Summary ====================

    /**
     * Get summary cards for dashboard header
     */
    SummaryCards get_summary(TimeRange range = {});

    /**
     * Get real-time metrics snapshot
     */
    struct RealtimeSnapshot {
        double current_cost_rate_per_hour;
        double current_requests_per_second;
        double current_latency_ms;
        double current_error_rate;
        int active_agents;
        int active_tasks;
        std::chrono::system_clock::time_point timestamp;
    };
    RealtimeSnapshot get_realtime_snapshot();

    // ==================== Cost Widgets ====================

    /**
     * Get cost over time chart data
     */
    ChartData get_cost_over_time(
        TimeRange range,
        std::string granularity = "hour"  // "minute", "hour", "day"
    );

    /**
     * Get cost breakdown by category
     */
    ChartData get_cost_by_category(TimeRange range);

    /**
     * Get cost breakdown by agent
     */
    ChartData get_cost_by_agent(TimeRange range, int top_n = 10);

    /**
     * Get cost breakdown by provider
     */
    ChartData get_cost_by_provider(TimeRange range);

    /**
     * Get cost breakdown by region
     */
    ChartData get_cost_by_region(TimeRange range);

    /**
     * Get top spending agents table
     */
    TableData get_top_spenders(TimeRange range, int limit = 10);

    /**
     * Get budget gauges for all budgets
     */
    std::vector<GaugeData> get_budget_gauges();

    // ==================== Performance Widgets ====================

    /**
     * Get latency over time
     */
    ChartData get_latency_trend(
        TimeRange range,
        std::string percentile = "p50"  // "p50", "p95", "p99"
    );

    /**
     * Get throughput over time
     */
    ChartData get_throughput_trend(TimeRange range);

    /**
     * Get error rate over time
     */
    ChartData get_error_rate_trend(TimeRange range);

    /**
     * Get resource utilization gauges
     */
    struct ResourceGauges {
        GaugeData cpu;
        GaugeData memory;
        GaugeData gpu;
        GaugeData network;
    };
    ResourceGauges get_resource_gauges();

    // ==================== Recommendations ====================

    /**
     * Get savings opportunities table
     */
    TableData get_savings_opportunities(int limit = 10);

    /**
     * Get recommendations feed
     */
    struct RecommendationFeedItem {
        std::string id;
        std::string title;
        std::string category;
        std::string priority;
        double estimated_savings;
        std::chrono::system_clock::time_point created_at;
    };
    std::vector<RecommendationFeedItem> get_recommendations_feed(int limit = 5);

    // ==================== Alerts ====================

    /**
     * Get active alerts
     */
    std::vector<DashboardAlert> get_active_alerts(int limit = 20);

    /**
     * Acknowledge alert
     */
    void acknowledge_alert(const std::string& alert_id);

    /**
     * Get alert count by level
     */
    std::map<DashboardAlert::Level, int> get_alert_counts();

    // ==================== Agent Details ====================

    /**
     * Get agent-specific metrics
     */
    struct AgentMetrics {
        std::string agent_id;
        std::string agent_name;
        double total_cost;
        double cost_per_request;
        int total_requests;
        double avg_latency_ms;
        double error_rate;
        std::chrono::system_clock::time_point last_active;
    };
    std::vector<AgentMetrics> get_agent_metrics(TimeRange range);

    /**
     * Get detailed metrics for specific agent
     */
    AgentMetrics get_agent_detail(const std::string& agent_id, TimeRange range);

    /**
     * Get agent cost trend
     */
    ChartData get_agent_cost_trend(const std::string& agent_id, TimeRange range);

    // ==================== Comparison ====================

    /**
     * Get cost comparison between periods
     */
    struct PeriodComparison {
        double current_period_cost;
        double previous_period_cost;
        double change_pct;
        std::string trend;  // "up", "down", "stable"
        ChartData comparison_chart;
    };
    PeriodComparison compare_periods(TimeRange current, TimeRange previous);

    // ==================== Export ====================

    /**
     * Export dashboard data to JSON
     */
    std::string export_json(TimeRange range);

    /**
     * Export dashboard data to PDF report
     */
    std::vector<uint8_t> export_pdf(TimeRange range);

    /**
     * Generate shareable dashboard URL
     */
    std::string generate_share_url(TimeRange range, std::chrono::hours expiry = std::chrono::hours{24});

    // ==================== WebSocket Streaming ====================

    /**
     * Subscribe to real-time updates
     */
    using UpdateCallback = std::function<void(const std::string& event_type, const std::string& data_json)>;
    void subscribe(UpdateCallback callback);

    /**
     * Unsubscribe from updates
     */
    void unsubscribe();

    /**
     * Get WebSocket endpoint for external clients
     */
    std::string get_websocket_endpoint();

    // ==================== Embedding ====================

    /**
     * Generate embeddable widget HTML
     */
    std::string generate_widget_html(
        WidgetType type,
        int width = 400,
        int height = 300,
        TimeRange range = {}
    );

    /**
     * Generate widget iframe code
     */
    std::string generate_widget_iframe(
        WidgetType type,
        int width = 400,
        int height = 300
    );

    // ==================== Configuration ====================

    void set_config(DashboardConfig config);
    DashboardConfig get_config() const;

    /**
     * Set refresh callback (called on each refresh interval)
     */
    using RefreshCallback = std::function<void()>;
    void set_refresh_callback(RefreshCallback callback);

    /**
     * Force refresh all data
     */
    void refresh();

    // ==================== Server ====================

    /**
     * Start dashboard HTTP server
     */
    void start_server(int port = 8080);

    /**
     * Stop dashboard server
     */
    void stop_server();

    /**
     * Check if server is running
     */
    bool is_server_running() const;

    /**
     * Get server URL
     */
    std::string get_server_url() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * Create dashboard with default config
 */
std::unique_ptr<Dashboard> create_dashboard();

/**
 * Create dashboard with custom config
 */
std::unique_ptr<Dashboard> create_dashboard(DashboardConfig config);

}  // namespace neamc::finops
