// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2024-2026 Neam Language Contributors
//
// Neam v0.6.4 - Cost Efficiency & Cloud Management
// Main header for all v0.6.4 features

#pragma once

// Intelligent Auto-Scaling
#include "scaling/scaling.hpp"

// Multi-Cloud Orchestration
#include "multicloud/multicloud.hpp"

// GPU/SIMD Acceleration
#include "gpu/gpu.hpp"

// FinOps Dashboard & Cost Management
#include "finops/finops.hpp"

namespace neamc {

/**
 * Neam v0.6.4 Version Info
 */
constexpr int VERSION_MAJOR = 0;
constexpr int VERSION_MINOR = 6;
constexpr int VERSION_PATCH = 4;
constexpr const char* VERSION_STRING = "0.6.4";
constexpr const char* VERSION_CODENAME = "Cost Efficiency";

/**
 * v0.6.4 Feature Flags
 */
struct V064Features {
    bool intelligent_autoscaling = true;
    bool multicloud_orchestration = true;
    bool gpu_acceleration = true;
    bool simd_acceleration = true;
    bool finops_dashboard = true;
    bool predictive_scaling = true;
    bool warm_pool = true;
    bool cost_routing = true;
    bool alibaba_cloud = true;
    bool continuous_benchmarking = true;
};

/**
 * Get v0.6.4 feature flags
 */
V064Features get_v064_features();

/**
 * v0.6.4 Unified Configuration
 */
struct V064Config {
    scaling::ScalingModuleConfig scaling;
    multicloud::MultiCloudConfig multicloud;
    gpu::AccelerationConfig acceleration;
    finops::FinOpsConfig finops;
};

/**
 * v0.6.4 Runtime - Unified access to all v0.6.4 features
 */
class V064Runtime {
public:
    explicit V064Runtime(V064Config config = {});
    ~V064Runtime();

    // Prevent copying
    V064Runtime(const V064Runtime&) = delete;
    V064Runtime& operator=(const V064Runtime&) = delete;

    /**
     * Get scaling manager
     */
    scaling::ScalingManager& scaling();

    /**
     * Get multi-cloud manager
     */
    multicloud::MultiCloudManager& multicloud();

    /**
     * Get acceleration manager
     */
    gpu::AccelerationManager& acceleration();

    /**
     * Get FinOps manager
     */
    finops::FinOpsManager& finops();

    /**
     * Initialize all v0.6.4 features
     */
    void initialize();

    /**
     * Shutdown all v0.6.4 features
     */
    void shutdown();

    /**
     * Health check
     */
    struct HealthStatus {
        bool scaling_healthy;
        bool multicloud_healthy;
        bool acceleration_healthy;
        bool finops_healthy;
        bool overall_healthy;
        std::string message;
    };
    HealthStatus health_check();

    /**
     * Get configuration
     */
    V064Config get_config() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * Create v0.6.4 runtime with default config
 */
std::unique_ptr<V064Runtime> create_v064_runtime();

/**
 * Create v0.6.4 runtime with custom config
 */
std::unique_ptr<V064Runtime> create_v064_runtime(V064Config config);

}  // namespace neamc
