// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2024-2026 Neam Language Contributors
//
// Neam v0.6.4 - Scaling Module
// Unified header for all scaling components

#pragma once

#include "predictive_scaler.hpp"
#include "warm_pool.hpp"
#include "resource_packer.hpp"

namespace neamc::scaling {

/**
 * Scaling module configuration
 */
struct ScalingModuleConfig {
    // Predictive scaling
    bool enable_predictive_scaling = true;
    ScalingPolicy default_policy;

    // Warm pool
    bool enable_warm_pool = true;
    WarmPoolConfig warm_pool_config;

    // Resource packing
    bool enable_resource_packing = true;
    PackingConfig packing_config;

    // General settings
    std::chrono::seconds evaluation_interval{30};
    bool auto_apply_decisions = false;
};

/**
 * Scaling Manager - Unified interface for all scaling functionality
 */
class ScalingManager {
public:
    explicit ScalingManager(ScalingModuleConfig config = {});
    ~ScalingManager();

    // Prevent copying
    ScalingManager(const ScalingManager&) = delete;
    ScalingManager& operator=(const ScalingManager&) = delete;

    /**
     * Get predictive scaler
     */
    PredictiveScaler& scaler();

    /**
     * Get warm pool manager
     */
    WarmPoolManager& warm_pool();

    /**
     * Get resource packer
     */
    ResourcePacker& packer();

    /**
     * Start all scaling services
     */
    void start();

    /**
     * Stop all scaling services
     */
    void stop();

    /**
     * Check if services are running
     */
    bool is_running() const;

    /**
     * Get configuration
     */
    ScalingModuleConfig get_config() const;

    /**
     * Unified scaling decision
     */
    struct UnifiedScalingDecision {
        ScalingDecision scaler_decision;
        int warm_instances_available;
        int warm_instances_needed;
        PackingResult packing_result;
        std::string summary;
        double estimated_cost_change;
    };
    UnifiedScalingDecision evaluate();

    /**
     * Apply unified scaling decision
     */
    bool apply(const UnifiedScalingDecision& decision);

    /**
     * Quick scaling helpers
     */
    void scale_to(int replicas);
    void scale_up(int count = 1);
    void scale_down(int count = 1);

    /**
     * Health check
     */
    struct HealthStatus {
        bool scaler_healthy;
        bool warm_pool_healthy;
        bool packer_healthy;
        std::string message;
    };
    HealthStatus health_check();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * Create scaling manager with default config
 */
std::unique_ptr<ScalingManager> create_scaling_manager();

/**
 * Create scaling manager with custom config
 */
std::unique_ptr<ScalingManager> create_scaling_manager(ScalingModuleConfig config);

}  // namespace neamc::scaling
