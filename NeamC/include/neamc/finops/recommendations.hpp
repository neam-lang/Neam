// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2024-2026 Neam Language Contributors
//
// Neam v0.6.4 - FinOps Recommendations Engine
// AI-powered cost optimization recommendations

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
 * Recommendation category
 */
enum class RecommendationCategory {
    RightSizing,        // Instance/resource sizing
    SpotUsage,          // Spot/preemptible instances
    ReservedCapacity,   // Reserved instances/commitments
    ModelOptimization,  // LLM model selection
    CachingStrategy,    // Response/embedding caching
    BatchProcessing,    // Batch vs real-time
    RegionOptimization, // Cross-region optimization
    Scheduling,         // Time-based scheduling
    Consolidation,      // Workload consolidation
    Deprecation,        // Remove unused resources
    Other
};

std::string category_to_string(RecommendationCategory category);

/**
 * Recommendation priority
 */
enum class RecommendationPriority {
    Critical,   // Immediate action needed
    High,       // Significant savings
    Medium,     // Moderate savings
    Low,        // Nice to have
    Info        // Informational only
};

std::string priority_to_string(RecommendationPriority priority);

/**
 * Recommendation status
 */
enum class RecommendationStatus {
    New,
    Acknowledged,
    InProgress,
    Implemented,
    Dismissed,
    Expired
};

/**
 * Individual recommendation
 */
struct Recommendation {
    std::string id;
    std::string title;
    std::string description;
    RecommendationCategory category;
    RecommendationPriority priority;
    RecommendationStatus status;

    // Impact analysis
    double estimated_monthly_savings_usd;
    double confidence;  // 0.0 - 1.0
    std::string savings_calculation;

    // Affected resources
    std::vector<std::string> affected_agents;
    std::vector<std::string> affected_resources;

    // Implementation
    std::string implementation_steps;
    std::string automation_available;  // "auto", "semi-auto", "manual"
    int estimated_implementation_effort_hours;

    // Risk assessment
    std::string risk_level;  // "low", "medium", "high"
    std::string risk_description;
    std::vector<std::string> prerequisites;

    // Timing
    std::chrono::system_clock::time_point created_at;
    std::chrono::system_clock::time_point expires_at;
    std::optional<std::chrono::system_clock::time_point> acknowledged_at;
    std::optional<std::chrono::system_clock::time_point> implemented_at;

    // Metadata
    std::string source;  // "automated", "manual", "external"
    std::map<std::string, std::string> metadata;
};

/**
 * Right-sizing recommendation details
 */
struct RightSizingRecommendation {
    std::string resource_id;
    std::string current_instance_type;
    std::string recommended_instance_type;
    double current_cost_monthly;
    double recommended_cost_monthly;
    double avg_cpu_utilization;
    double avg_memory_utilization;
    double peak_cpu_utilization;
    double peak_memory_utilization;
    std::string reasoning;
};

/**
 * Spot usage recommendation details
 */
struct SpotRecommendation {
    std::string workload_id;
    std::string current_instance_type;
    double current_hourly_cost;
    double spot_hourly_cost;
    double spot_availability;
    int interruption_frequency_per_week;
    bool workload_is_fault_tolerant;
    std::string recommended_strategy;  // "spot-only", "spot-with-fallback", "mixed"
};

/**
 * Model optimization recommendation details
 */
struct ModelOptimizationRecommendation {
    std::string current_model;
    std::string recommended_model;
    double current_cost_per_1k_tokens;
    double recommended_cost_per_1k_tokens;
    double quality_score_difference;  // Negative = worse quality
    std::vector<std::string> suitable_use_cases;
    std::vector<std::string> unsuitable_use_cases;
    std::string reasoning;
};

/**
 * Caching recommendation details
 */
struct CachingRecommendation {
    std::string cache_type;  // "response", "embedding", "computation"
    double cache_hit_rate_current;
    double cache_hit_rate_projected;
    double current_cost_daily;
    double projected_cost_daily;
    int recommended_cache_size_mb;
    int recommended_ttl_seconds;
    std::string implementation_notes;
};

/**
 * Batch processing recommendation details
 */
struct BatchRecommendation {
    std::string workload_id;
    double current_cost_per_request;
    double batched_cost_per_request;
    int recommended_batch_size;
    int recommended_batch_window_seconds;
    double latency_increase_ms;
    std::vector<std::string> compatible_operations;
};

/**
 * Recommendations summary
 */
struct RecommendationsSummary {
    int total_recommendations;
    int new_recommendations;
    int implemented_recommendations;
    double total_potential_savings_monthly;
    double realized_savings_monthly;
    std::map<RecommendationCategory, int> by_category;
    std::map<RecommendationPriority, int> by_priority;
    std::vector<Recommendation> top_savings_opportunities;
};

/**
 * Recommendations Engine - AI-powered cost optimization
 *
 * Features:
 * - Automated recommendation generation
 * - Right-sizing analysis
 * - Spot/preemptible opportunity detection
 * - LLM model optimization suggestions
 * - Caching strategy recommendations
 * - Batch processing opportunities
 * - Implementation tracking
 */
class RecommendationsEngine {
public:
    RecommendationsEngine();
    ~RecommendationsEngine();

    // Prevent copying
    RecommendationsEngine(const RecommendationsEngine&) = delete;
    RecommendationsEngine& operator=(const RecommendationsEngine&) = delete;

    // ==================== Analysis ====================

    /**
     * Run full analysis and generate recommendations
     */
    std::vector<Recommendation> analyze();

    /**
     * Run analysis for specific category
     */
    std::vector<Recommendation> analyze_category(RecommendationCategory category);

    /**
     * Analyze right-sizing opportunities
     */
    std::vector<RightSizingRecommendation> analyze_right_sizing();

    /**
     * Analyze spot/preemptible opportunities
     */
    std::vector<SpotRecommendation> analyze_spot_opportunities();

    /**
     * Analyze LLM model optimization
     */
    std::vector<ModelOptimizationRecommendation> analyze_model_optimization();

    /**
     * Analyze caching opportunities
     */
    std::vector<CachingRecommendation> analyze_caching();

    /**
     * Analyze batch processing opportunities
     */
    std::vector<BatchRecommendation> analyze_batching();

    /**
     * Analyze unused resources
     */
    std::vector<Recommendation> analyze_unused_resources();

    /**
     * Analyze scheduling opportunities
     */
    std::vector<Recommendation> analyze_scheduling();

    // ==================== Recommendation Management ====================

    /**
     * Get all recommendations
     */
    std::vector<Recommendation> list_recommendations(
        std::optional<RecommendationStatus> status = std::nullopt,
        std::optional<RecommendationCategory> category = std::nullopt,
        std::optional<RecommendationPriority> min_priority = std::nullopt
    );

    /**
     * Get recommendation by ID
     */
    std::optional<Recommendation> get_recommendation(const std::string& id);

    /**
     * Acknowledge recommendation
     */
    void acknowledge_recommendation(const std::string& id);

    /**
     * Mark recommendation as in progress
     */
    void start_implementation(const std::string& id);

    /**
     * Mark recommendation as implemented
     */
    void complete_implementation(
        const std::string& id,
        double actual_savings_monthly = 0.0
    );

    /**
     * Dismiss recommendation
     */
    void dismiss_recommendation(
        const std::string& id,
        const std::string& reason = ""
    );

    /**
     * Add manual recommendation
     */
    void add_recommendation(const Recommendation& recommendation);

    // ==================== Auto-Implementation ====================

    /**
     * Check if recommendation can be auto-implemented
     */
    bool can_auto_implement(const std::string& id);

    /**
     * Auto-implement recommendation
     */
    bool auto_implement(const std::string& id);

    /**
     * Set auto-implementation callback
     */
    using AutoImplementCallback = std::function<bool(const Recommendation&)>;
    void set_auto_implement_callback(
        RecommendationCategory category,
        AutoImplementCallback callback
    );

    /**
     * Enable/disable auto-implementation for category
     */
    void set_auto_implement_enabled(
        RecommendationCategory category,
        bool enabled
    );

    // ==================== Summaries ====================

    /**
     * Get recommendations summary
     */
    RecommendationsSummary get_summary();

    /**
     * Get potential savings by category
     */
    std::map<RecommendationCategory, double> get_savings_by_category();

    /**
     * Get realized savings (from implemented recommendations)
     */
    double get_realized_savings_monthly();

    // ==================== Configuration ====================

    struct Config {
        // Analysis thresholds
        double min_savings_threshold_usd = 10.0;
        double min_confidence_threshold = 0.7;

        // Right-sizing thresholds
        double cpu_underutilization_threshold = 0.3;
        double memory_underutilization_threshold = 0.3;

        // Spot thresholds
        double min_spot_availability = 0.8;
        int max_interruptions_per_week = 5;

        // Caching thresholds
        double min_cache_hit_improvement = 0.2;

        // Analysis window
        int analysis_days = 14;

        // Auto-refresh
        bool auto_refresh = true;
        std::chrono::hours refresh_interval{24};
    };

    void set_config(Config config);
    Config get_config() const;

    // ==================== Data Input ====================

    /**
     * Set resource utilization data
     */
    void set_utilization_data(
        const std::string& resource_id,
        const std::vector<std::pair<std::chrono::system_clock::time_point, double>>& cpu_data,
        const std::vector<std::pair<std::chrono::system_clock::time_point, double>>& memory_data
    );

    /**
     * Set LLM usage data
     */
    void set_llm_usage_data(
        const std::string& model,
        int total_requests,
        int total_input_tokens,
        int total_output_tokens,
        double total_cost
    );

    /**
     * Set cache hit rate data
     */
    void set_cache_data(
        const std::string& cache_name,
        double hit_rate,
        int total_requests,
        double avg_response_time_ms
    );

    // ==================== Notifications ====================

    /**
     * Set callback for new recommendations
     */
    using NewRecommendationCallback = std::function<void(const Recommendation&)>;
    void set_new_recommendation_callback(NewRecommendationCallback callback);

    /**
     * Set callback for high-value recommendations
     */
    void set_high_value_callback(
        NewRecommendationCallback callback,
        double min_savings_threshold
    );

    // ==================== Export ====================

    /**
     * Export recommendations to JSON
     */
    std::string export_json();

    /**
     * Export recommendations to CSV
     */
    std::string export_csv();

    /**
     * Generate recommendations report (markdown)
     */
    std::string generate_report();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * Create recommendations engine
 */
std::unique_ptr<RecommendationsEngine> create_recommendations_engine();

}  // namespace neamc::finops
