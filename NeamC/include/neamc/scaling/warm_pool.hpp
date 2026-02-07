// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2024-2026 Neam Language Contributors
//
// Neam v0.6.4 - Warm Pool Manager
// Pre-warmed instances for near-zero cold starts

#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <optional>

namespace neamc::scaling {

/**
 * Warm instance information
 */
struct WarmInstance {
    std::string instance_id;
    std::string instance_type;
    std::string cloud_provider;
    std::string region;
    std::chrono::system_clock::time_point warmed_at;
    std::chrono::system_clock::time_point last_used;
    bool is_spot;
    double hourly_cost;

    enum class State {
        Warming,      // Being initialized
        Ready,        // Ready to accept work
        InUse,        // Currently processing
        Cooling,      // Being released
        Terminated    // No longer available
    };
    State state;

    // Pre-loaded agents
    std::vector<std::string> preloaded_agents;

    // Resource info
    int cpu_cores;
    int memory_mb;
    int gpu_count;
};

/**
 * Warm pool configuration
 */
struct WarmPoolConfig {
    int min_warm_instances = 2;
    int max_warm_instances = 10;
    std::chrono::seconds warm_timeout{300};  // How long to keep warm
    std::chrono::seconds health_check_interval{30};
    std::vector<std::string> preferred_instance_types;
    std::vector<std::string> preferred_regions;
    double max_warm_pool_cost_per_hour = 10.0;
    bool prefer_spot = true;
    bool auto_replenish = true;

    // Pre-warming configuration
    std::vector<std::string> preload_agents;
    bool preload_models = true;
};

/**
 * Warm Pool Manager - Maintains pre-warmed instances for fast scaling
 *
 * Features:
 * - Maintains pool of ready-to-use instances
 * - Pre-loads agent code and models
 * - Automatic replenishment when instances are consumed
 * - Cost-aware pool sizing
 * - Health monitoring and replacement
 */
class WarmPoolManager {
public:
    explicit WarmPoolManager(WarmPoolConfig config);
    ~WarmPoolManager();

    // Prevent copying
    WarmPoolManager(const WarmPoolManager&) = delete;
    WarmPoolManager& operator=(const WarmPoolManager&) = delete;

    // Allow moving
    WarmPoolManager(WarmPoolManager&&) noexcept;
    WarmPoolManager& operator=(WarmPoolManager&&) noexcept;

    /**
     * Ensure minimum warm instances are available
     */
    void ensure_warm_instances(int count);

    /**
     * Acquire a warm instance for use
     * Returns nullopt if no instances available
     */
    std::optional<WarmInstance> acquire_instance();

    /**
     * Acquire instance with specific requirements
     */
    std::optional<WarmInstance> acquire_instance(
        const std::string& instance_type,
        const std::string& region = ""
    );

    /**
     * Release instance back to pool or terminate
     */
    void release_instance(const std::string& instance_id, bool keep_warm = true);

    /**
     * Pre-warm instances with specific agent
     */
    void prewarm_with_agent(const std::string& agent_id);

    /**
     * Pre-warm with multiple agents
     */
    void prewarm_with_agents(const std::vector<std::string>& agent_ids);

    /**
     * Get current pool status
     */
    int warm_instance_count() const;
    int available_instance_count() const;
    int in_use_instance_count() const;

    /**
     * Get current warm pool cost
     */
    double current_warm_pool_cost_per_hour() const;

    /**
     * List all instances in pool
     */
    std::vector<WarmInstance> list_instances() const;

    /**
     * List available instances
     */
    std::vector<WarmInstance> list_available_instances() const;

    /**
     * Health check callback
     */
    using HealthCheckCallback = std::function<bool(const WarmInstance&)>;
    void set_health_check_callback(HealthCheckCallback callback);

    /**
     * Run health check on all instances
     */
    void run_health_check();

    /**
     * Configuration management
     */
    void set_config(WarmPoolConfig config);
    WarmPoolConfig get_config() const;

    /**
     * Start/stop the warm pool manager
     */
    void start();
    void stop();
    bool is_running() const;

    /**
     * Statistics
     */
    struct Stats {
        int total_instances_created;
        int total_instances_acquired;
        int total_instances_released;
        int total_instances_terminated;
        int current_warm_count;
        int current_in_use_count;
        double avg_warm_time_seconds;
        double avg_acquire_time_ms;
        double total_cost_incurred;
        int health_check_failures;
    };
    Stats get_stats() const;

    /**
     * Reset statistics
     */
    void reset_stats();

    /**
     * Force terminate all instances
     */
    void terminate_all();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * Create warm pool manager with default config
 */
std::unique_ptr<WarmPoolManager> create_warm_pool_manager();

/**
 * Create warm pool manager with custom config
 */
std::unique_ptr<WarmPoolManager> create_warm_pool_manager(WarmPoolConfig config);

}  // namespace neamc::scaling
