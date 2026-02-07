// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2024-2026 Neam Language Contributors
//
// Neam v0.6.4 - Multi-Cloud Cost-Aware Router
// Intelligent routing across cloud providers for cost optimization

#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <optional>

namespace neamc::multicloud {

/**
 * Supported cloud providers
 */
enum class CloudProvider {
    AWS,
    GCP,
    Azure,
    Alibaba,
    OnPrem,
    Unknown
};

/**
 * Convert provider enum to string
 */
std::string provider_to_string(CloudProvider provider);
CloudProvider string_to_provider(const std::string& str);

/**
 * Region pricing information
 */
struct RegionPricing {
    CloudProvider provider;
    std::string region;
    std::string instance_type;
    double spot_price_per_hour;
    double ondemand_price_per_hour;
    double spot_availability;  // 0.0 - 1.0
    double egress_cost_per_gb;
    double gpu_spot_price_per_hour;
    double gpu_ondemand_price_per_hour;
    std::chrono::system_clock::time_point last_updated;
};

/**
 * Routing constraints
 */
struct RoutingConstraints {
    double max_latency_ms = 500.0;
    double max_cost_per_invocation = 1.0;
    bool require_spot = false;
    bool allow_cross_region = true;
    bool allow_cross_cloud = true;
    std::vector<std::string> preferred_regions;
    std::vector<std::string> excluded_regions;
    std::vector<CloudProvider> preferred_providers;
    std::vector<CloudProvider> excluded_providers;
    std::string data_residency_requirement;  // e.g., "EU", "APAC", "US"
    bool require_gpu = false;
    int min_memory_mb = 0;
    int min_cpu_cores = 0;
};

/**
 * Routing decision output
 */
struct RoutingDecision {
    enum class Target {
        Local,
        Remote,
        NotFound,
        Rejected  // Constraints cannot be satisfied
    };

    Target target;
    CloudProvider provider;
    std::string region;
    std::string instance_type;
    bool use_spot;
    double estimated_cost;
    double estimated_latency_ms;
    std::string reasoning;
    double confidence;  // 0.0 - 1.0

    // Fallback options if primary fails
    struct FallbackOption {
        CloudProvider provider;
        std::string region;
        bool use_spot;
        double estimated_cost;
    };
    std::vector<FallbackOption> fallback_options;
};

/**
 * Cost comparison result
 */
struct CostComparison {
    CloudProvider provider;
    std::string region;
    std::string instance_type;
    double hourly_cost_spot;
    double hourly_cost_ondemand;
    double monthly_cost_estimate;
    double savings_vs_baseline_pct;
    double spot_availability;
    double avg_latency_ms;
    int ranking;  // 1 = best
};

/**
 * Provider endpoint information
 */
struct ProviderEndpoint {
    CloudProvider provider;
    std::string region;
    std::string endpoint_url;
    std::vector<std::string> available_instance_types;
    std::vector<std::string> available_agents;
    bool supports_spot;
    bool supports_gpu;
    double current_load;  // 0.0 - 1.0
};

/**
 * Cost-Aware Router - Multi-cloud routing for cost optimization
 *
 * Features:
 * - Real-time spot price monitoring across clouds
 * - Latency-aware routing decisions
 * - Data residency compliance
 * - Automatic failover to cheaper alternatives
 * - Cross-cloud arbitrage
 */
class CostAwareRouter {
public:
    CostAwareRouter();
    ~CostAwareRouter();

    // Prevent copying
    CostAwareRouter(const CostAwareRouter&) = delete;
    CostAwareRouter& operator=(const CostAwareRouter&) = delete;

    /**
     * Register a provider endpoint
     */
    void register_endpoint(const ProviderEndpoint& endpoint);

    /**
     * Remove a provider endpoint
     */
    void remove_endpoint(CloudProvider provider, const std::string& region);

    /**
     * Update pricing data for a region
     */
    void update_pricing(const RegionPricing& pricing);

    /**
     * Bulk update pricing data
     */
    void update_pricing_batch(const std::vector<RegionPricing>& pricing);

    /**
     * Get optimal routing decision for an agent
     */
    RoutingDecision route(
        const std::string& agent_id,
        const std::string& source_region,
        const RoutingConstraints& constraints = {}
    );

    /**
     * Batch routing for multiple requests
     */
    std::vector<RoutingDecision> route_batch(
        const std::vector<std::string>& agent_ids,
        const std::string& source_region,
        const RoutingConstraints& constraints = {}
    );

    /**
     * Get cost comparison across all providers
     */
    std::vector<CostComparison> compare_costs(
        const std::string& agent_id,
        CloudProvider baseline_provider = CloudProvider::AWS
    );

    /**
     * Get cheapest option for a workload
     */
    std::optional<CostComparison> get_cheapest(
        const std::string& agent_id,
        const RoutingConstraints& constraints = {}
    );

    /**
     * Refresh spot prices from all providers
     */
    void refresh_spot_prices();

    /**
     * Set auto-refresh interval
     */
    void set_price_refresh_interval(std::chrono::seconds interval);

    /**
     * Get current pricing for a region
     */
    std::optional<RegionPricing> get_pricing(
        CloudProvider provider,
        const std::string& region
    );

    /**
     * List all registered endpoints
     */
    std::vector<ProviderEndpoint> list_endpoints() const;

    /**
     * Get endpoints by provider
     */
    std::vector<ProviderEndpoint> list_endpoints(CloudProvider provider) const;

    /**
     * Health check for endpoints
     */
    using HealthCheckCallback = std::function<bool(const ProviderEndpoint&)>;
    void set_health_check_callback(HealthCheckCallback callback);
    void run_health_check();

    /**
     * Mark endpoint as healthy/unhealthy
     */
    void mark_endpoint_healthy(CloudProvider provider, const std::string& region);
    void mark_endpoint_unhealthy(CloudProvider provider, const std::string& region);

    /**
     * Statistics
     */
    struct Stats {
        int total_routing_decisions;
        int local_routes;
        int remote_routes;
        int cross_cloud_routes;
        int spot_routes;
        int fallback_routes;
        int rejected_routes;
        double total_cost_routed;
        double estimated_savings;
        std::map<CloudProvider, int> routes_by_provider;
        std::map<std::string, int> routes_by_region;
    };
    Stats get_stats() const;

    /**
     * Reset statistics
     */
    void reset_stats();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * Create cost-aware router
 */
std::unique_ptr<CostAwareRouter> create_cost_router();

}  // namespace neamc::multicloud
