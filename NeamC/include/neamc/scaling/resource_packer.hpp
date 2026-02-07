// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2024-2026 Neam Language Contributors
//
// Neam v0.6.4 - Resource Packer
// Bin-packing algorithm for optimal workload placement

#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <optional>

namespace neamc::scaling {

/**
 * Resource requirements for a workload
 */
struct ResourceRequirements {
    std::string workload_id;
    std::string workload_name;

    // CPU (millicores, 1000 = 1 core)
    int cpu_request_millicores = 100;
    int cpu_limit_millicores = 500;

    // Memory (MB)
    int memory_request_mb = 128;
    int memory_limit_mb = 512;

    // GPU (count)
    int gpu_request = 0;
    int gpu_limit = 0;
    std::string gpu_type;  // "nvidia-t4", "nvidia-a100", etc.

    // Storage (MB)
    int ephemeral_storage_mb = 100;
    int persistent_storage_mb = 0;

    // Network
    int network_bandwidth_mbps = 0;  // 0 = no specific requirement

    // Affinity/Anti-affinity
    std::vector<std::string> node_affinity_labels;
    std::vector<std::string> anti_affinity_workloads;
    std::vector<std::string> co_location_workloads;

    // Priority
    int priority = 0;  // Higher = more important
    bool preemptible = true;

    // Constraints
    bool requires_spot = false;
    bool requires_ondemand = false;
    std::vector<std::string> required_zones;
};

/**
 * Node/instance capacity
 */
struct NodeCapacity {
    std::string node_id;
    std::string node_name;
    std::string instance_type;
    std::string zone;
    std::string cloud_provider;

    // Total capacity
    int cpu_millicores;
    int memory_mb;
    int gpu_count;
    std::string gpu_type;
    int ephemeral_storage_mb;

    // Current usage
    int cpu_used_millicores = 0;
    int memory_used_mb = 0;
    int gpu_used = 0;

    // Pricing
    double hourly_cost;
    bool is_spot;

    // Status
    bool is_ready = true;
    bool is_schedulable = true;
    std::vector<std::string> labels;
    std::vector<std::string> taints;

    // Available capacity
    int available_cpu() const { return cpu_millicores - cpu_used_millicores; }
    int available_memory() const { return memory_mb - memory_used_mb; }
    int available_gpu() const { return gpu_count - gpu_used; }
};

/**
 * Packing result for a single workload
 */
struct PlacementDecision {
    std::string workload_id;
    std::string node_id;
    bool placed = false;
    std::string reason;  // Reason if not placed

    // Resource allocation
    int cpu_allocated;
    int memory_allocated;
    int gpu_allocated;

    // Cost attribution
    double estimated_hourly_cost;
};

/**
 * Overall packing result
 */
struct PackingResult {
    bool success;
    int workloads_placed;
    int workloads_unplaced;
    std::vector<PlacementDecision> placements;
    std::vector<std::string> unplaced_workloads;
    std::vector<std::string> unplaced_reasons;

    // Cost analysis
    double total_hourly_cost;
    double node_utilization_cpu;    // Average CPU utilization
    double node_utilization_memory; // Average memory utilization
    int nodes_used;
    int nodes_available;

    // Recommendations
    std::vector<std::string> optimization_recommendations;
};

/**
 * Packing algorithm type
 */
enum class PackingAlgorithm {
    FirstFit,           // Place on first node that fits
    BestFit,            // Place on node with least remaining space
    WorstFit,           // Place on node with most remaining space
    NextFit,            // Continue from last placement
    CostOptimized,      // Minimize cost
    SpreadBalanced,     // Balance across nodes
    BinPacking,         // Full bin-packing optimization
    Custom
};

std::string algorithm_to_string(PackingAlgorithm algo);

/**
 * Packing configuration
 */
struct PackingConfig {
    PackingAlgorithm algorithm = PackingAlgorithm::CostOptimized;

    // Resource buffer (extra headroom)
    double cpu_headroom_percent = 10.0;
    double memory_headroom_percent = 10.0;

    // Optimization weights
    double cost_weight = 0.5;
    double utilization_weight = 0.3;
    double spread_weight = 0.2;

    // Constraints
    bool allow_preemption = false;
    bool prefer_spot = true;
    double max_node_utilization = 0.85;

    // Timeout for optimization
    std::chrono::milliseconds optimization_timeout{5000};
};

/**
 * Resource Packer - Bin-packing for workload placement
 *
 * Features:
 * - Multiple packing algorithms
 * - Cost-aware placement
 * - GPU-aware scheduling
 * - Affinity/anti-affinity support
 * - Preemption handling
 * - Continuous re-packing
 */
class ResourcePacker {
public:
    ResourcePacker();
    explicit ResourcePacker(PackingConfig config);
    ~ResourcePacker();

    // Prevent copying
    ResourcePacker(const ResourcePacker&) = delete;
    ResourcePacker& operator=(const ResourcePacker&) = delete;

    // ==================== Node Management ====================

    /**
     * Add node to the cluster
     */
    void add_node(const NodeCapacity& node);

    /**
     * Update node capacity/usage
     */
    void update_node(const NodeCapacity& node);

    /**
     * Remove node from cluster
     */
    void remove_node(const std::string& node_id);

    /**
     * Get node information
     */
    std::optional<NodeCapacity> get_node(const std::string& node_id);

    /**
     * List all nodes
     */
    std::vector<NodeCapacity> list_nodes();

    /**
     * List available nodes (ready and schedulable)
     */
    std::vector<NodeCapacity> list_available_nodes();

    /**
     * Get cluster capacity summary
     */
    struct ClusterCapacity {
        int total_nodes;
        int available_nodes;
        int total_cpu_millicores;
        int used_cpu_millicores;
        int total_memory_mb;
        int used_memory_mb;
        int total_gpus;
        int used_gpus;
        double total_hourly_cost;
        double spot_percentage;
    };
    ClusterCapacity get_cluster_capacity();

    // ==================== Workload Management ====================

    /**
     * Add workload to be scheduled
     */
    void add_workload(const ResourceRequirements& workload);

    /**
     * Remove workload
     */
    void remove_workload(const std::string& workload_id);

    /**
     * Get workload requirements
     */
    std::optional<ResourceRequirements> get_workload(const std::string& workload_id);

    /**
     * List pending workloads
     */
    std::vector<ResourceRequirements> list_pending_workloads();

    /**
     * List placed workloads
     */
    std::vector<std::pair<ResourceRequirements, std::string>> list_placed_workloads();

    // ==================== Packing Operations ====================

    /**
     * Pack all pending workloads
     */
    PackingResult pack();

    /**
     * Pack specific workloads
     */
    PackingResult pack(const std::vector<std::string>& workload_ids);

    /**
     * Try to place a single workload
     */
    PlacementDecision place_workload(const ResourceRequirements& workload);

    /**
     * Find best node for workload
     */
    std::optional<std::string> find_best_node(const ResourceRequirements& workload);

    /**
     * Check if workload can be placed
     */
    bool can_place(const ResourceRequirements& workload);

    /**
     * Re-pack all workloads for optimization
     */
    PackingResult repack();

    /**
     * Compact workloads to fewer nodes
     */
    PackingResult compact();

    /**
     * Evict workloads from a node
     */
    std::vector<std::string> evict_from_node(const std::string& node_id);

    // ==================== Preemption ====================

    /**
     * Check if workload can preempt others
     */
    bool can_preempt(
        const ResourceRequirements& workload,
        std::vector<std::string>& victims
    );

    /**
     * Preempt lower priority workloads
     */
    PlacementDecision preempt_and_place(const ResourceRequirements& workload);

    // ==================== Scoring ====================

    /**
     * Score a placement (higher = better)
     */
    double score_placement(
        const ResourceRequirements& workload,
        const NodeCapacity& node
    );

    /**
     * Score entire cluster packing
     */
    double score_packing(const PackingResult& result);

    /**
     * Set custom scoring function
     */
    using ScoringFunction = std::function<double(
        const ResourceRequirements&,
        const NodeCapacity&,
        const PackingConfig&
    )>;
    void set_scoring_function(ScoringFunction func);

    // ==================== Configuration ====================

    void set_config(PackingConfig config);
    PackingConfig get_config() const;

    // ==================== Statistics ====================

    struct Stats {
        int total_pack_operations;
        int successful_placements;
        int failed_placements;
        int preemptions;
        int repack_operations;
        double avg_node_utilization;
        double avg_pack_time_ms;
        std::chrono::system_clock::time_point last_pack;
    };
    Stats get_stats() const;

    void reset_stats();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * Create resource packer with default config
 */
std::unique_ptr<ResourcePacker> create_resource_packer();

/**
 * Create resource packer with custom config
 */
std::unique_ptr<ResourcePacker> create_resource_packer(PackingConfig config);

/**
 * Calculate optimal instance type for workload
 */
std::string recommend_instance_type(
    const ResourceRequirements& workload,
    const std::string& cloud_provider
);

}  // namespace neamc::scaling
