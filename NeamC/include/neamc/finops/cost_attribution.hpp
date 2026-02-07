// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2024-2026 Neam Language Contributors
//
// Neam v0.6.4 - Cost Attribution System
// Real-time cost tracking and attribution for agent workloads

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
 * Cost category types
 */
enum class CostCategory {
    Compute,        // CPU/GPU compute time
    Memory,         // Memory allocation
    Storage,        // Persistent storage
    Network,        // Data transfer
    LLM,            // LLM API calls
    Embedding,      // Embedding API calls
    VectorDB,       // Vector database operations
    StateBackend,   // State persistence
    External,       // External API calls
    Other
};

/**
 * Convert cost category to string
 */
std::string category_to_string(CostCategory category);
CostCategory string_to_category(const std::string& str);

/**
 * Cloud provider for cost tracking
 */
enum class CloudProvider {
    AWS,
    GCP,
    Azure,
    Alibaba,
    OnPrem,
    Local,
    Unknown
};

std::string provider_to_string(CloudProvider provider);

/**
 * Individual cost entry
 */
struct CostEntry {
    std::string id;
    std::chrono::system_clock::time_point timestamp;
    std::string agent_id;
    std::string task_id;
    std::string operation;
    CostCategory category;
    CloudProvider provider;
    std::string region;

    // Cost details
    double cost_usd;
    std::string currency;
    double exchange_rate;

    // Resource usage
    double compute_seconds;
    double memory_gb_seconds;
    double storage_gb;
    double network_gb;
    int llm_tokens_input;
    int llm_tokens_output;
    std::string llm_model;

    // Attribution metadata
    std::string user_id;
    std::string project_id;
    std::string environment;  // prod, staging, dev
    std::map<std::string, std::string> tags;
};

/**
 * Aggregated cost summary
 */
struct CostSummary {
    std::chrono::system_clock::time_point period_start;
    std::chrono::system_clock::time_point period_end;

    double total_cost_usd;
    std::map<CostCategory, double> cost_by_category;
    std::map<std::string, double> cost_by_agent;
    std::map<CloudProvider, double> cost_by_provider;
    std::map<std::string, double> cost_by_region;
    std::map<std::string, double> cost_by_project;
    std::map<std::string, double> cost_by_user;

    // LLM-specific breakdown
    std::map<std::string, double> cost_by_llm_model;
    int total_llm_tokens;
    double avg_cost_per_1k_tokens;

    // Compute breakdown
    double total_compute_hours;
    double total_gpu_hours;
    double avg_cost_per_compute_hour;

    // Trends
    double cost_change_vs_previous_period_pct;
    std::string trend;  // "increasing", "decreasing", "stable"
};

/**
 * Budget configuration
 */
struct Budget {
    std::string id;
    std::string name;
    double limit_usd;
    std::string period;  // "daily", "weekly", "monthly"
    std::chrono::system_clock::time_point start_date;

    // Scope
    std::optional<std::string> agent_id;
    std::optional<std::string> project_id;
    std::optional<std::string> user_id;
    std::optional<CostCategory> category;

    // Alerts
    double warning_threshold_pct = 80.0;
    double critical_threshold_pct = 95.0;
    bool enforce_hard_limit = false;

    // Current state
    double current_spend_usd;
    double remaining_usd;
    double utilization_pct;
};

/**
 * Budget alert
 */
struct BudgetAlert {
    enum class Severity {
        Info,
        Warning,
        Critical,
        Exceeded
    };

    std::string budget_id;
    std::string budget_name;
    Severity severity;
    double current_spend_usd;
    double limit_usd;
    double utilization_pct;
    std::string message;
    std::chrono::system_clock::time_point timestamp;
};

/**
 * Cost allocation rule
 */
struct AllocationRule {
    std::string id;
    std::string name;
    std::string description;

    // Matching criteria
    std::optional<std::string> agent_pattern;  // Regex
    std::optional<CostCategory> category;
    std::optional<CloudProvider> provider;
    std::map<std::string, std::string> required_tags;

    // Allocation
    std::string target_project_id;
    std::string target_cost_center;
    double allocation_percentage = 100.0;

    int priority;  // Higher priority rules apply first
    bool enabled = true;
};

/**
 * Cost Attribution System - Real-time cost tracking
 *
 * Features:
 * - Real-time cost tracking per agent/task
 * - Multi-dimensional attribution (agent, project, user, region)
 * - Budget management with alerts
 * - Cost allocation rules
 * - Historical trend analysis
 * - Export to CSV/JSON for external FinOps tools
 */
class CostAttributionSystem {
public:
    CostAttributionSystem();
    ~CostAttributionSystem();

    // Prevent copying
    CostAttributionSystem(const CostAttributionSystem&) = delete;
    CostAttributionSystem& operator=(const CostAttributionSystem&) = delete;

    // ==================== Cost Recording ====================

    /**
     * Record a cost entry
     */
    void record_cost(const CostEntry& entry);

    /**
     * Record batch of cost entries
     */
    void record_costs(const std::vector<CostEntry>& entries);

    /**
     * Start cost tracking for an operation
     * Returns tracking ID
     */
    std::string start_tracking(
        const std::string& agent_id,
        const std::string& operation,
        const std::map<std::string, std::string>& tags = {}
    );

    /**
     * Stop cost tracking and record final cost
     */
    CostEntry stop_tracking(
        const std::string& tracking_id,
        double cost_usd = 0.0,
        CostCategory category = CostCategory::Compute
    );

    /**
     * Record LLM API call cost
     */
    void record_llm_cost(
        const std::string& agent_id,
        const std::string& model,
        int input_tokens,
        int output_tokens,
        double cost_usd
    );

    /**
     * Record compute cost
     */
    void record_compute_cost(
        const std::string& agent_id,
        double compute_seconds,
        double memory_gb,
        CloudProvider provider,
        const std::string& region,
        double cost_usd
    );

    // ==================== Cost Queries ====================

    /**
     * Get cost summary for a time period
     */
    CostSummary get_summary(
        std::chrono::system_clock::time_point start,
        std::chrono::system_clock::time_point end
    );

    /**
     * Get daily cost summary
     */
    CostSummary get_daily_summary(
        std::chrono::system_clock::time_point date
    );

    /**
     * Get weekly cost summary
     */
    CostSummary get_weekly_summary(
        std::chrono::system_clock::time_point week_start
    );

    /**
     * Get monthly cost summary
     */
    CostSummary get_monthly_summary(int year, int month);

    /**
     * Get cost by agent
     */
    double get_agent_cost(
        const std::string& agent_id,
        std::chrono::system_clock::time_point start,
        std::chrono::system_clock::time_point end
    );

    /**
     * Get cost by project
     */
    double get_project_cost(
        const std::string& project_id,
        std::chrono::system_clock::time_point start,
        std::chrono::system_clock::time_point end
    );

    /**
     * Get cost entries with filters
     */
    std::vector<CostEntry> query_costs(
        std::chrono::system_clock::time_point start,
        std::chrono::system_clock::time_point end,
        const std::optional<std::string>& agent_id = std::nullopt,
        const std::optional<CostCategory>& category = std::nullopt,
        const std::optional<CloudProvider>& provider = std::nullopt,
        int limit = 1000
    );

    /**
     * Get real-time cost rate ($/hour)
     */
    double get_current_cost_rate();

    /**
     * Get projected daily cost
     */
    double get_projected_daily_cost();

    // ==================== Budget Management ====================

    /**
     * Create a budget
     */
    void create_budget(const Budget& budget);

    /**
     * Update a budget
     */
    void update_budget(const Budget& budget);

    /**
     * Delete a budget
     */
    void delete_budget(const std::string& budget_id);

    /**
     * Get a budget
     */
    std::optional<Budget> get_budget(const std::string& budget_id);

    /**
     * List all budgets
     */
    std::vector<Budget> list_budgets();

    /**
     * Check budget status and return alerts
     */
    std::vector<BudgetAlert> check_budgets();

    /**
     * Set budget alert callback
     */
    using BudgetAlertCallback = std::function<void(const BudgetAlert&)>;
    void set_alert_callback(BudgetAlertCallback callback);

    /**
     * Check if operation is within budget
     */
    bool is_within_budget(
        const std::string& agent_id,
        double estimated_cost_usd
    );

    // ==================== Cost Allocation ====================

    /**
     * Add allocation rule
     */
    void add_allocation_rule(const AllocationRule& rule);

    /**
     * Update allocation rule
     */
    void update_allocation_rule(const AllocationRule& rule);

    /**
     * Remove allocation rule
     */
    void remove_allocation_rule(const std::string& rule_id);

    /**
     * List allocation rules
     */
    std::vector<AllocationRule> list_allocation_rules();

    /**
     * Apply allocation rules to costs
     */
    void apply_allocation_rules();

    // ==================== Export ====================

    /**
     * Export costs to CSV
     */
    std::string export_csv(
        std::chrono::system_clock::time_point start,
        std::chrono::system_clock::time_point end
    );

    /**
     * Export costs to JSON
     */
    std::string export_json(
        std::chrono::system_clock::time_point start,
        std::chrono::system_clock::time_point end
    );

    /**
     * Export summary to JSON
     */
    std::string export_summary_json(const CostSummary& summary);

    // ==================== Pricing Configuration ====================

    /**
     * Set LLM pricing (per 1K tokens)
     */
    void set_llm_pricing(
        const std::string& model,
        double input_price_per_1k,
        double output_price_per_1k
    );

    /**
     * Set compute pricing
     */
    void set_compute_pricing(
        CloudProvider provider,
        const std::string& instance_type,
        double price_per_hour
    );

    /**
     * Get LLM pricing
     */
    std::pair<double, double> get_llm_pricing(const std::string& model);

    // ==================== Statistics ====================

    struct Stats {
        size_t total_cost_entries;
        double total_cost_tracked_usd;
        size_t active_budgets;
        size_t budget_alerts_triggered;
        size_t allocation_rules;
        std::chrono::system_clock::time_point oldest_entry;
        std::chrono::system_clock::time_point newest_entry;
    };
    Stats get_stats() const;

    /**
     * Clear old cost entries
     */
    void cleanup_old_entries(std::chrono::hours retention_period);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * Create cost attribution system
 */
std::unique_ptr<CostAttributionSystem> create_cost_attribution_system();

/**
 * Global cost tracker instance (singleton)
 */
CostAttributionSystem& global_cost_tracker();

/**
 * RAII cost tracking helper
 */
class CostTracker {
public:
    CostTracker(
        const std::string& agent_id,
        const std::string& operation,
        CostCategory category = CostCategory::Compute
    );
    ~CostTracker();

    void set_cost(double cost_usd);
    void add_llm_tokens(int input, int output, const std::string& model);
    void set_tags(const std::map<std::string, std::string>& tags);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace neamc::finops
