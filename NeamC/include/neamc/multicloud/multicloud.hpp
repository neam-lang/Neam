// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2024-2026 Neam Language Contributors
//
// Neam v0.6.4 - Multi-Cloud Module
// Unified header for all multi-cloud components

#pragma once

#include "cost_router.hpp"
#include "alibaba_adapter.hpp"

namespace neamc::multicloud {

// Forward declarations for other cloud adapters
class AWSAdapter;
class GCPAdapter;
class AzureAdapter;

/**
 * Multi-cloud module configuration
 */
struct MultiCloudConfig {
    // Cost routing
    bool enable_cost_routing = true;
    std::chrono::seconds price_refresh_interval{300};

    // Cloud providers
    bool enable_aws = true;
    bool enable_gcp = true;
    bool enable_azure = true;
    bool enable_alibaba = false;

    // Default routing constraints
    RoutingConstraints default_constraints;

    // Failover configuration
    bool enable_cross_cloud_failover = true;
    int failover_threshold_errors = 3;
    std::chrono::seconds failover_cooldown{60};
};

/**
 * Multi-Cloud Manager - Unified interface for multi-cloud operations
 */
class MultiCloudManager {
public:
    explicit MultiCloudManager(MultiCloudConfig config = {});
    ~MultiCloudManager();

    // Prevent copying
    MultiCloudManager(const MultiCloudManager&) = delete;
    MultiCloudManager& operator=(const MultiCloudManager&) = delete;

    /**
     * Get cost-aware router
     */
    CostAwareRouter& router();

    /**
     * Get Alibaba adapter (if enabled)
     */
    AlibabaAdapter* alibaba();

    /**
     * Initialize cloud adapters
     */
    void initialize();

    /**
     * Refresh pricing data from all providers
     */
    void refresh_pricing();

    /**
     * Get optimal deployment target
     */
    RoutingDecision get_optimal_target(
        const std::string& agent_id,
        const std::string& source_region,
        const RoutingConstraints& constraints = {}
    );

    /**
     * Compare costs across all providers
     */
    std::vector<CostComparison> compare_all_providers(
        const std::string& agent_id
    );

    /**
     * Get current best spot prices
     */
    struct SpotPriceSummary {
        CloudProvider provider;
        std::string region;
        std::string instance_type;
        double price_per_hour;
        double availability;
    };
    std::vector<SpotPriceSummary> get_best_spot_prices(
        int top_n = 10,
        bool require_gpu = false
    );

    /**
     * Health check across all providers
     */
    struct ProviderHealth {
        CloudProvider provider;
        bool is_healthy;
        double latency_ms;
        std::string message;
    };
    std::vector<ProviderHealth> health_check_all();

    /**
     * Get configuration
     */
    MultiCloudConfig get_config() const;

    /**
     * Statistics
     */
    struct Stats {
        int total_routing_decisions;
        std::map<CloudProvider, int> decisions_by_provider;
        double total_cost_routed;
        double estimated_savings;
        int cross_cloud_failovers;
    };
    Stats get_stats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * Create multi-cloud manager with default config
 */
std::unique_ptr<MultiCloudManager> create_multicloud_manager();

/**
 * Create multi-cloud manager with custom config
 */
std::unique_ptr<MultiCloudManager> create_multicloud_manager(MultiCloudConfig config);

}  // namespace neamc::multicloud
