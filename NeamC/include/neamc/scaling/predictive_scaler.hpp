// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2024-2026 Neam Language Contributors
//
// Neam v0.6.4 - Intelligent Auto-Scaling Module
// Predictive Scaler with ML-based load forecasting

#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <map>

namespace neamc::scaling {

/**
 * Metrics collected for scaling decisions
 */
struct ScalingMetrics {
    double request_rate;           // Requests per second
    double queue_depth;            // Pending tasks
    double latency_p50_ms;
    double latency_p95_ms;
    double latency_p99_ms;
    double cpu_utilization;        // 0.0 - 1.0
    double memory_utilization;     // 0.0 - 1.0
    double gpu_utilization;        // 0.0 - 1.0 (if applicable)
    double cost_rate;              // $/hour current spend
    std::chrono::system_clock::time_point timestamp;
};

/**
 * Scaling policy configuration
 */
struct ScalingPolicy {
    double target_cpu_utilization = 0.70;
    double target_memory_utilization = 0.80;
    double target_latency_p95_ms = 100.0;
    double max_cost_per_hour = 100.0;
    int min_replicas = 1;
    int max_replicas = 100;
    int warm_pool_size = 2;
    std::chrono::seconds scale_up_cooldown{60};
    std::chrono::seconds scale_down_cooldown{300};
    bool prefer_spot_instances = true;
    double spot_fallback_threshold = 0.3;  // Fallback if <30% spot available
};

/**
 * Scaling decision output
 */
struct ScalingDecision {
    enum class Action {
        NoChange,
        ScaleUp,
        ScaleDown,
        SwitchToSpot,
        SwitchToOnDemand,
        SwitchToFaaS,
        SwitchToContainers
    };

    Action action;
    int current_replicas;
    int target_replicas;
    std::string instance_type;
    std::string reasoning;
    double estimated_cost_change;  // Positive = more expensive
    double estimated_latency_change;
    double confidence;  // 0.0 - 1.0
};

/**
 * Load prediction output
 */
struct LoadPrediction {
    double predicted_rps_5min;
    double predicted_rps_1hr;
    double confidence;  // 0.0 - 1.0
    std::vector<double> hourly_forecast_24h;
    std::string trend;  // "increasing", "decreasing", "stable", "spike"
};

/**
 * Scaling event for history tracking
 */
struct ScalingEvent {
    std::chrono::system_clock::time_point timestamp;
    ScalingDecision::Action action;
    int from_replicas;
    int to_replicas;
    std::string reason;
    double cost_impact;
};

/**
 * Predictive Scaler - ML-based auto-scaling for Neam agents
 *
 * Features:
 * - Time-series forecasting for load prediction
 * - Pattern detection (daily, weekly cycles)
 * - Anomaly detection for traffic spikes
 * - Cost-aware scaling decisions
 * - Kubernetes HPA/VPA integration
 */
class PredictiveScaler {
public:
    explicit PredictiveScaler(ScalingPolicy policy);
    ~PredictiveScaler();

    // Prevent copying
    PredictiveScaler(const PredictiveScaler&) = delete;
    PredictiveScaler& operator=(const PredictiveScaler&) = delete;

    // Allow moving
    PredictiveScaler(PredictiveScaler&&) noexcept;
    PredictiveScaler& operator=(PredictiveScaler&&) noexcept;

    /**
     * Record metrics for analysis
     */
    void record_metrics(const ScalingMetrics& metrics);

    /**
     * Record batch of metrics
     */
    void record_metrics_batch(const std::vector<ScalingMetrics>& metrics);

    /**
     * Get current load prediction
     */
    LoadPrediction predict_load() const;

    /**
     * Get scaling decision based on current state
     */
    ScalingDecision decide() const;

    /**
     * Get scaling decision with custom constraints
     */
    ScalingDecision decide_with_constraints(
        double max_cost_per_hour,
        double max_latency_ms
    ) const;

    /**
     * Apply scaling decision (interface with K8s/Cloud APIs)
     */
    using ApplyCallback = std::function<bool(const ScalingDecision&)>;
    void set_apply_callback(ApplyCallback callback);
    bool apply_decision(const ScalingDecision& decision);

    /**
     * Get recommendations for optimization
     */
    std::vector<std::string> get_recommendations() const;

    /**
     * Configuration management
     */
    void set_policy(ScalingPolicy policy);
    ScalingPolicy get_policy() const;

    /**
     * Get current replica count
     */
    int get_current_replicas() const;
    void set_current_replicas(int count);

    /**
     * Scaling history
     */
    std::vector<ScalingEvent> get_scaling_history(int limit = 100) const;

    /**
     * Statistics
     */
    struct Stats {
        int scale_up_events;
        int scale_down_events;
        int total_scaling_events;
        double avg_prediction_accuracy;
        double cost_savings_from_scaling;
        double avg_utilization;
        std::chrono::system_clock::time_point last_scale_up;
        std::chrono::system_clock::time_point last_scale_down;
    };
    Stats get_stats() const;

    /**
     * Reset statistics
     */
    void reset_stats();

    /**
     * Enable/disable auto-scaling
     */
    void set_enabled(bool enabled);
    bool is_enabled() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * Create a predictive scaler with default policy
 */
std::unique_ptr<PredictiveScaler> create_predictive_scaler();

/**
 * Create a predictive scaler with custom policy
 */
std::unique_ptr<PredictiveScaler> create_predictive_scaler(ScalingPolicy policy);

}  // namespace neamc::scaling
