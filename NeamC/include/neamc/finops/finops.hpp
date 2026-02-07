// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2024-2026 Neam Language Contributors
//
// Neam v0.6.4 - FinOps Module
// Unified header for all FinOps components

#pragma once

#include "cost_attribution.hpp"
#include "recommendations.hpp"
#include "benchmarking.hpp"
#include "dashboard.hpp"

namespace neamc::finops {

/**
 * FinOps configuration
 */
struct FinOpsConfig {
    // Cost attribution
    bool enable_cost_tracking = true;
    std::string default_currency = "USD";

    // Recommendations
    bool enable_recommendations = true;
    std::chrono::hours recommendation_refresh_interval{24};

    // Benchmarking
    bool enable_benchmarking = true;
    bool enable_regression_detection = true;

    // Dashboard
    bool enable_dashboard = true;
    int dashboard_port = 8080;
    bool dashboard_auth_enabled = false;
    std::string dashboard_auth_token;

    // Storage
    std::string storage_backend = "sqlite";  // "sqlite", "postgres", "memory"
    std::string storage_path = "neam_finops.db";

    // Export
    bool export_to_cloud_provider = false;
    std::string cloud_export_bucket;
};

/**
 * FinOps Manager - Unified interface for all FinOps functionality
 */
class FinOpsManager {
public:
    explicit FinOpsManager(FinOpsConfig config = {});
    ~FinOpsManager();

    // Prevent copying
    FinOpsManager(const FinOpsManager&) = delete;
    FinOpsManager& operator=(const FinOpsManager&) = delete;

    /**
     * Get cost attribution system
     */
    CostAttributionSystem& costs();

    /**
     * Get recommendations engine
     */
    RecommendationsEngine& recommendations();

    /**
     * Get benchmarking system
     */
    BenchmarkingSystem& benchmarks();

    /**
     * Get dashboard
     */
    Dashboard& dashboard();

    /**
     * Start all FinOps services
     */
    void start();

    /**
     * Stop all FinOps services
     */
    void stop();

    /**
     * Check if services are running
     */
    bool is_running() const;

    /**
     * Get configuration
     */
    FinOpsConfig get_config() const;

    /**
     * Quick cost recording helpers
     */
    void track_llm_call(
        const std::string& agent_id,
        const std::string& model,
        int input_tokens,
        int output_tokens
    );

    void track_compute(
        const std::string& agent_id,
        double seconds,
        double memory_gb
    );

    /**
     * Quick summary helpers
     */
    double get_daily_spend();
    double get_monthly_spend();
    int get_open_recommendations();
    double get_potential_savings();

    /**
     * Health check
     */
    struct HealthStatus {
        bool cost_tracking_healthy;
        bool recommendations_healthy;
        bool benchmarking_healthy;
        bool dashboard_healthy;
        std::string message;
    };
    HealthStatus health_check();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * Create FinOps manager with default config
 */
std::unique_ptr<FinOpsManager> create_finops_manager();

/**
 * Create FinOps manager with custom config
 */
std::unique_ptr<FinOpsManager> create_finops_manager(FinOpsConfig config);

/**
 * Global FinOps manager instance (singleton)
 */
FinOpsManager& global_finops();

}  // namespace neamc::finops
