// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2024-2026 Neam Language Contributors
//
// Neam v0.6.4 - Cost-Aware Router Implementation

#include "neamc/multicloud/cost_router.hpp"

#include <algorithm>
#include <atomic>
#include <fstream>
#include <mutex>
#include <nlohmann/json.hpp>
#include <unordered_map>

namespace neamc::multicloud {

std::string provider_to_string(CloudProvider provider) {
    switch (provider) {
        case CloudProvider::AWS: return "aws";
        case CloudProvider::GCP: return "gcp";
        case CloudProvider::Azure: return "azure";
        case CloudProvider::Alibaba: return "alibaba";
        case CloudProvider::OnPrem: return "onprem";
        default: return "unknown";
    }
}

CloudProvider string_to_provider(const std::string& str) {
    if (str == "aws") return CloudProvider::AWS;
    if (str == "gcp") return CloudProvider::GCP;
    if (str == "azure") return CloudProvider::Azure;
    if (str == "alibaba") return CloudProvider::Alibaba;
    if (str == "onprem") return CloudProvider::OnPrem;
    return CloudProvider::Unknown;
}

struct CostAwareRouter::Impl {
    std::unordered_map<std::string, ProviderEndpoint> endpoints;
    std::unordered_map<std::string, RegionPricing> pricing;
    std::unordered_map<std::string, bool> endpoint_health;
    HealthCheckCallback health_check_callback;
    Stats stats{};
    std::chrono::seconds price_refresh_interval{300};
    mutable std::mutex mutex;

    std::string make_key(CloudProvider provider, const std::string& region) const {
        return provider_to_string(provider) + ":" + region;
    }

    double estimate_latency(const std::string& source_region, CloudProvider provider, const std::string& target_region) const {
        // Simple latency estimation based on region proximity
        // In production, this would use actual latency measurements
        if (source_region == target_region) return 5.0;

        // Same provider, different region
        std::string target_prefix = target_region.substr(0, 2);
        std::string source_prefix = source_region.substr(0, 2);
        if (target_prefix == source_prefix) return 30.0;  // Same continent

        return 100.0;  // Cross-continent
    }

    bool matches_constraints(const ProviderEndpoint& endpoint, const RoutingConstraints& constraints) const {
        // Check excluded providers
        for (const auto& excluded : constraints.excluded_providers) {
            if (endpoint.provider == excluded) return false;
        }

        // Check excluded regions
        for (const auto& excluded : constraints.excluded_regions) {
            if (endpoint.region == excluded) return false;
        }

        // Check GPU requirement
        if (constraints.require_gpu && !endpoint.supports_gpu) return false;

        // Check spot requirement
        if (constraints.require_spot && !endpoint.supports_spot) return false;

        // Check data residency
        if (!constraints.data_residency_requirement.empty()) {
            // Simple check - in production would use proper region mapping
            if (constraints.data_residency_requirement == "EU") {
                if (endpoint.region.find("eu-") != 0 && endpoint.region.find("europe-") != 0) {
                    return false;
                }
            } else if (constraints.data_residency_requirement == "US") {
                if (endpoint.region.find("us-") != 0) {
                    return false;
                }
            }
        }

        return true;
    }
};

CostAwareRouter::CostAwareRouter()
    : impl_(std::make_unique<Impl>()) {}

CostAwareRouter::~CostAwareRouter() = default;

void CostAwareRouter::register_endpoint(const ProviderEndpoint& endpoint) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::string key = impl_->make_key(endpoint.provider, endpoint.region);
    impl_->endpoints[key] = endpoint;
    impl_->endpoint_health[key] = true;
}

void CostAwareRouter::remove_endpoint(CloudProvider provider, const std::string& region) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::string key = impl_->make_key(provider, region);
    impl_->endpoints.erase(key);
    impl_->endpoint_health.erase(key);
}

void CostAwareRouter::update_pricing(const RegionPricing& pricing) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::string key = impl_->make_key(pricing.provider, pricing.region);
    impl_->pricing[key] = pricing;
}

void CostAwareRouter::update_pricing_batch(const std::vector<RegionPricing>& pricing) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    for (const auto& p : pricing) {
        std::string key = impl_->make_key(p.provider, p.region);
        impl_->pricing[key] = p;
    }
}

RoutingDecision CostAwareRouter::route(
    const std::string& agent_id,
    const std::string& source_region,
    const RoutingConstraints& constraints
) {
    std::lock_guard<std::mutex> lock(impl_->mutex);

    RoutingDecision decision;
    decision.target = RoutingDecision::Target::NotFound;

    struct Candidate {
        std::string key;
        ProviderEndpoint endpoint;
        RegionPricing pricing;
        double score;
        double latency;
        double cost;
    };
    std::vector<Candidate> candidates;

    // Evaluate all endpoints
    for (const auto& [key, endpoint] : impl_->endpoints) {
        // Check health
        auto health_it = impl_->endpoint_health.find(key);
        if (health_it != impl_->endpoint_health.end() && !health_it->second) {
            continue;
        }

        // Check constraints
        if (!impl_->matches_constraints(endpoint, constraints)) {
            continue;
        }

        // Check if agent is available at this endpoint
        if (std::find(endpoint.available_agents.begin(),
                     endpoint.available_agents.end(),
                     agent_id) == endpoint.available_agents.end()) {
            continue;
        }

        Candidate candidate;
        candidate.key = key;
        candidate.endpoint = endpoint;

        // Get pricing
        auto pricing_it = impl_->pricing.find(key);
        if (pricing_it != impl_->pricing.end()) {
            candidate.pricing = pricing_it->second;
            candidate.cost = constraints.require_spot ?
                candidate.pricing.spot_price_per_hour :
                candidate.pricing.ondemand_price_per_hour;
        } else {
            candidate.cost = 1.0;  // Default cost
        }

        // Estimate latency
        candidate.latency = impl_->estimate_latency(source_region, endpoint.provider, endpoint.region);

        // Check latency constraint
        if (candidate.latency > constraints.max_latency_ms) {
            continue;
        }

        // Calculate score (lower is better)
        // Weight: 60% cost, 30% latency, 10% load
        candidate.score = (candidate.cost * 0.6) +
                         (candidate.latency / 100.0 * 0.3) +
                         (endpoint.current_load * 0.1);

        candidates.push_back(candidate);
    }

    if (candidates.empty()) {
        decision.target = RoutingDecision::Target::Rejected;
        decision.reasoning = "No endpoints match constraints";
        impl_->stats.rejected_routes++;
        return decision;
    }

    // Sort by score
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) {
                  return a.score < b.score;
              });

    // Best candidate
    const auto& best = candidates[0];
    decision.target = RoutingDecision::Target::Remote;
    decision.provider = best.endpoint.provider;
    decision.region = best.endpoint.region;
    decision.use_spot = constraints.require_spot || (best.pricing.spot_availability > 0.8);
    decision.estimated_cost = best.cost;
    decision.estimated_latency_ms = best.latency;
    decision.confidence = 0.8;
    decision.reasoning = "Best cost/latency trade-off";

    // Add fallback options
    for (size_t i = 1; i < std::min(candidates.size(), size_t(3)); ++i) {
        RoutingDecision::FallbackOption fallback;
        fallback.provider = candidates[i].endpoint.provider;
        fallback.region = candidates[i].endpoint.region;
        fallback.use_spot = candidates[i].pricing.spot_availability > 0.8;
        fallback.estimated_cost = candidates[i].cost;
        decision.fallback_options.push_back(fallback);
    }

    // Update stats
    impl_->stats.total_routing_decisions++;
    impl_->stats.remote_routes++;
    impl_->stats.routes_by_provider[best.endpoint.provider]++;
    impl_->stats.routes_by_region[best.endpoint.region]++;
    impl_->stats.total_cost_routed += best.cost;

    if (decision.use_spot) {
        impl_->stats.spot_routes++;
    }

    return decision;
}

std::vector<RoutingDecision> CostAwareRouter::route_batch(
    const std::vector<std::string>& agent_ids,
    const std::string& source_region,
    const RoutingConstraints& constraints
) {
    std::vector<RoutingDecision> decisions;
    decisions.reserve(agent_ids.size());

    for (const auto& agent_id : agent_ids) {
        decisions.push_back(route(agent_id, source_region, constraints));
    }

    return decisions;
}

std::vector<CostComparison> CostAwareRouter::compare_costs(
    const std::string& agent_id,
    CloudProvider baseline_provider
) {
    std::lock_guard<std::mutex> lock(impl_->mutex);

    std::vector<CostComparison> comparisons;
    double baseline_cost = 0;

    // First pass: find baseline
    for (const auto& [key, pricing] : impl_->pricing) {
        if (pricing.provider == baseline_provider) {
            baseline_cost = pricing.ondemand_price_per_hour;
            break;
        }
    }

    if (baseline_cost == 0) baseline_cost = 1.0;

    // Second pass: compare all
    int rank = 1;
    for (const auto& [key, pricing] : impl_->pricing) {
        CostComparison comparison;
        comparison.provider = pricing.provider;
        comparison.region = pricing.region;
        comparison.instance_type = pricing.instance_type;
        comparison.hourly_cost_spot = pricing.spot_price_per_hour;
        comparison.hourly_cost_ondemand = pricing.ondemand_price_per_hour;
        comparison.monthly_cost_estimate = pricing.ondemand_price_per_hour * 730;  // ~730 hours/month
        comparison.savings_vs_baseline_pct =
            ((baseline_cost - pricing.ondemand_price_per_hour) / baseline_cost) * 100;
        comparison.spot_availability = pricing.spot_availability;
        comparison.avg_latency_ms = 50.0;  // Placeholder
        comparison.ranking = rank++;
        comparisons.push_back(comparison);
    }

    // Sort by on-demand cost
    std::sort(comparisons.begin(), comparisons.end(),
              [](const CostComparison& a, const CostComparison& b) {
                  return a.hourly_cost_ondemand < b.hourly_cost_ondemand;
              });

    // Update rankings
    for (size_t i = 0; i < comparisons.size(); ++i) {
        comparisons[i].ranking = static_cast<int>(i + 1);
    }

    return comparisons;
}

std::optional<CostComparison> CostAwareRouter::get_cheapest(
    const std::string& agent_id,
    const RoutingConstraints& constraints
) {
    auto comparisons = compare_costs(agent_id, CloudProvider::AWS);
    if (comparisons.empty()) {
        return std::nullopt;
    }
    return comparisons[0];
}

void CostAwareRouter::refresh_spot_prices() {
    // NOTE: This does not call live cloud provider pricing APIs.
    // To use real pricing data, call load_pricing_from_file() or
    // update_pricing_batch() with data from your own pricing source.
    std::lock_guard<std::mutex> lock(impl_->mutex);
    auto now = std::chrono::system_clock::now();
    for (auto& [key, pricing] : impl_->pricing) {
        pricing.last_updated = now;
    }
}

// v0.6.8: Load pricing data from a JSON file.
// Expected format:
// [
//   {
//     "provider": "aws",
//     "region": "us-east-1",
//     "instance_type": "m5.xlarge",
//     "spot_price_per_hour": 0.05,
//     "ondemand_price_per_hour": 0.192,
//     "spot_availability": 0.95,
//     "egress_cost_per_gb": 0.09,
//     "gpu_spot_price_per_hour": 0.0,
//     "gpu_ondemand_price_per_hour": 0.0
//   }
// ]
bool CostAwareRouter::load_pricing_from_file(const std::string& path) {
    std::ifstream file(path);
    if (!file.is_open()) {
        return false;
    }

    try {
        auto json = nlohmann::json::parse(file);
        if (!json.is_array()) {
            return false;
        }

        std::vector<RegionPricing> entries;
        auto now = std::chrono::system_clock::now();

        for (const auto& item : json) {
            RegionPricing p;
            p.provider = string_to_provider(item.value("provider", "unknown"));
            p.region = item.value("region", "");
            p.instance_type = item.value("instance_type", "");
            p.spot_price_per_hour = item.value("spot_price_per_hour", 0.0);
            p.ondemand_price_per_hour = item.value("ondemand_price_per_hour", 0.0);
            p.spot_availability = item.value("spot_availability", 0.0);
            p.egress_cost_per_gb = item.value("egress_cost_per_gb", 0.0);
            p.gpu_spot_price_per_hour = item.value("gpu_spot_price_per_hour", 0.0);
            p.gpu_ondemand_price_per_hour = item.value("gpu_ondemand_price_per_hour", 0.0);
            p.last_updated = now;

            if (p.provider != CloudProvider::Unknown && !p.region.empty()) {
                entries.push_back(std::move(p));
            }
        }

        update_pricing_batch(entries);
        return true;
    } catch (const std::exception&) {
        return false;
    }
}

void CostAwareRouter::set_price_refresh_interval(std::chrono::seconds interval) {
    impl_->price_refresh_interval = interval;
}

std::optional<RegionPricing> CostAwareRouter::get_pricing(
    CloudProvider provider,
    const std::string& region
) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::string key = impl_->make_key(provider, region);
    auto it = impl_->pricing.find(key);
    if (it != impl_->pricing.end()) {
        return it->second;
    }
    return std::nullopt;
}

std::vector<ProviderEndpoint> CostAwareRouter::list_endpoints() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::vector<ProviderEndpoint> result;
    for (const auto& [key, endpoint] : impl_->endpoints) {
        result.push_back(endpoint);
    }
    return result;
}

std::vector<ProviderEndpoint> CostAwareRouter::list_endpoints(CloudProvider provider) const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::vector<ProviderEndpoint> result;
    for (const auto& [key, endpoint] : impl_->endpoints) {
        if (endpoint.provider == provider) {
            result.push_back(endpoint);
        }
    }
    return result;
}

void CostAwareRouter::set_health_check_callback(HealthCheckCallback callback) {
    impl_->health_check_callback = std::move(callback);
}

void CostAwareRouter::run_health_check() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    if (!impl_->health_check_callback) return;

    for (const auto& [key, endpoint] : impl_->endpoints) {
        impl_->endpoint_health[key] = impl_->health_check_callback(endpoint);
    }
}

void CostAwareRouter::mark_endpoint_healthy(CloudProvider provider, const std::string& region) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::string key = impl_->make_key(provider, region);
    impl_->endpoint_health[key] = true;
}

void CostAwareRouter::mark_endpoint_unhealthy(CloudProvider provider, const std::string& region) {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    std::string key = impl_->make_key(provider, region);
    impl_->endpoint_health[key] = false;
}

CostAwareRouter::Stats CostAwareRouter::get_stats() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->stats;
}

void CostAwareRouter::reset_stats() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->stats = Stats{};
}

std::unique_ptr<CostAwareRouter> create_cost_router() {
    return std::make_unique<CostAwareRouter>();
}

}  // namespace neamc::multicloud
