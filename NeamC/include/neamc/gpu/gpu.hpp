// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2024-2026 Neam Language Contributors
//
// Neam v0.6.4 - GPU/SIMD Acceleration Module
// Unified header for hardware acceleration components

#pragma once

#include "executor.hpp"
#include "simd_executor.hpp"

namespace neamc::gpu {

/**
 * Acceleration module configuration
 */
struct AccelerationConfig {
    // GPU settings
    bool enable_gpu = true;
    Backend preferred_gpu_backend = Backend::CPU_Fallback;
    int preferred_device_id = 0;

    // SIMD settings
    bool enable_simd = true;
    SIMDType preferred_simd_type = SIMDType::Unknown;  // Auto-detect

    // Batch processing
    int default_batch_size = 32;
    bool enable_dynamic_batching = true;
    std::chrono::milliseconds batch_timeout{10};

    // Memory management
    size_t memory_pool_size_mb = 512;
    bool enable_memory_pooling = true;
};

/**
 * Acceleration Manager - Unified interface for hardware acceleration
 */
class AccelerationManager {
public:
    explicit AccelerationManager(AccelerationConfig config = {});
    ~AccelerationManager();

    // Prevent copying
    AccelerationManager(const AccelerationManager&) = delete;
    AccelerationManager& operator=(const AccelerationManager&) = delete;

    /**
     * Get GPU executor
     */
    GPUExecutor& gpu();

    /**
     * Get SIMD executor
     */
    SIMDExecutor& simd();

    /**
     * Check if GPU is available
     */
    bool has_gpu() const;

    /**
     * Get best available acceleration
     */
    enum class AccelerationType {
        GPU_CUDA,
        GPU_Metal,
        GPU_OpenCL,
        GPU_Vulkan,
        SIMD_AVX512,
        SIMD_AVX2,
        SIMD_NEON,
        CPU_Only
    };
    AccelerationType get_best_acceleration() const;

    /**
     * Get hardware capabilities summary
     */
    struct Capabilities {
        bool has_cuda;
        bool has_metal;
        bool has_opencl;
        bool has_vulkan;
        bool has_avx512;
        bool has_avx2;
        bool has_neon;
        int gpu_count;
        std::string gpu_name;
        size_t gpu_memory_mb;
        std::string cpu_name;
        int cpu_cores;
        int simd_vector_width;
    };
    Capabilities get_capabilities() const;

    /**
     * Optimized embedding operations
     */
    std::vector<float> embedding_similarity(
        const std::vector<float>& query,
        const std::vector<std::vector<float>>& candidates
    );

    /**
     * Batch embedding similarity
     */
    std::vector<std::vector<float>> batch_embedding_similarity(
        const std::vector<std::vector<float>>& queries,
        const std::vector<std::vector<float>>& candidates
    );

    /**
     * Top-K similar embeddings
     */
    std::vector<std::pair<int, float>> top_k_similar(
        const std::vector<float>& query,
        const std::vector<std::vector<float>>& candidates,
        int k
    );

    /**
     * Normalize embeddings
     */
    void normalize_embeddings(std::vector<std::vector<float>>& embeddings);

    /**
     * Get configuration
     */
    AccelerationConfig get_config() const;

    /**
     * Statistics
     */
    struct Stats {
        size_t total_gpu_ops;
        size_t total_simd_ops;
        size_t total_cpu_ops;
        double gpu_utilization;
        double avg_speedup;
        size_t memory_used_mb;
        double total_compute_time_ms;
    };
    Stats get_stats() const;

    void reset_stats();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * Create acceleration manager with default config
 */
std::unique_ptr<AccelerationManager> create_acceleration_manager();

/**
 * Create acceleration manager with custom config
 */
std::unique_ptr<AccelerationManager> create_acceleration_manager(AccelerationConfig config);

/**
 * Global acceleration manager (singleton)
 */
AccelerationManager& global_acceleration();

/**
 * Quick helpers for common operations
 */
namespace accel {

/**
 * Fast dot product (uses best available acceleration)
 */
float dot_product(const float* a, const float* b, size_t length);

/**
 * Fast cosine similarity
 */
float cosine_similarity(const float* a, const float* b, size_t length);

/**
 * Fast normalize
 */
void normalize(float* vec, size_t length);

/**
 * Fast top-K (uses GPU if available, otherwise SIMD)
 */
std::vector<std::pair<size_t, float>> top_k(
    const float* query,
    const float* candidates,
    size_t num_candidates,
    size_t dim,
    size_t k
);

}  // namespace accel

}  // namespace neamc::gpu
