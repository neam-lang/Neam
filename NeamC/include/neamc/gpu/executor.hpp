// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2024-2026 Neam Language Contributors
//
// Neam v0.6.4 - GPU Executor
// Hardware-accelerated operations for multimedia workloads

#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <optional>

namespace neamc::gpu {

/**
 * GPU backend types
 */
enum class Backend {
    CUDA,           // NVIDIA CUDA
    Metal,          // Apple Metal
    OpenCL,         // Cross-platform OpenCL
    Vulkan,         // Vulkan compute
    CPU_Fallback    // CPU fallback when no GPU
};

/**
 * Convert backend to string
 */
std::string backend_to_string(Backend backend);

/**
 * Device information
 */
struct DeviceInfo {
    Backend backend;
    int device_id;
    std::string name;
    std::string vendor;
    size_t total_memory;
    size_t available_memory;
    int compute_capability_major;
    int compute_capability_minor;
    int multiprocessor_count;
    int max_threads_per_block;
    int max_shared_memory_per_block;
    bool supports_fp16;
    bool supports_int8;
    double peak_tflops;
};

/**
 * Tensor data types
 */
enum class DType {
    Float32,
    Float16,
    BFloat16,
    Int32,
    Int16,
    Int8,
    UInt8
};

/**
 * GPU Tensor - Multi-dimensional array on GPU
 */
class GPUTensor {
public:
    GPUTensor();
    GPUTensor(const std::vector<size_t>& shape, DType dtype);
    ~GPUTensor();

    // Move semantics
    GPUTensor(GPUTensor&& other) noexcept;
    GPUTensor& operator=(GPUTensor&& other) noexcept;

    // No copying (use clone())
    GPUTensor(const GPUTensor&) = delete;
    GPUTensor& operator=(const GPUTensor&) = delete;

    /**
     * Shape and type information
     */
    const std::vector<size_t>& shape() const;
    size_t ndim() const;
    size_t numel() const;
    size_t size_bytes() const;
    DType dtype() const;

    /**
     * Device information
     */
    bool is_on_device() const;
    int device_id() const;

    /**
     * Data transfer
     */
    void to_device();
    void to_host();

    /**
     * Get raw pointers
     */
    void* data();
    const void* data() const;

    /**
     * Clone tensor
     */
    GPUTensor clone() const;

    /**
     * Reshape (must have same total elements)
     */
    void reshape(const std::vector<size_t>& new_shape);

    /**
     * Fill with value
     */
    void fill(float value);
    void zero();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * Batch configuration for dynamic batching
 */
struct BatchConfig {
    int max_batch_size = 32;
    int min_batch_size = 1;
    std::chrono::milliseconds batch_timeout{10};
    bool dynamic_batching = true;
    bool pad_to_max = false;  // Pad smaller batches to max size
};

/**
 * GPU Executor - Hardware-accelerated operations
 *
 * Features:
 * - Automatic device selection (CUDA, Metal, OpenCL)
 * - Memory management with pooling
 * - Dynamic batching for throughput
 * - Common ML operations (matmul, conv, etc.)
 * - Image/audio preprocessing
 */
class GPUExecutor {
public:
    GPUExecutor();
    explicit GPUExecutor(Backend preferred_backend);
    ~GPUExecutor();

    // Prevent copying
    GPUExecutor(const GPUExecutor&) = delete;
    GPUExecutor& operator=(const GPUExecutor&) = delete;

    // ==================== Device Management ====================

    /**
     * List available devices
     */
    std::vector<DeviceInfo> list_devices();

    /**
     * Select device for execution
     */
    void select_device(int device_id);

    /**
     * Get current device info
     */
    DeviceInfo current_device() const;

    /**
     * Get available backends
     */
    std::vector<Backend> available_backends() const;

    /**
     * Check if GPU is available
     */
    bool has_gpu() const;

    // ==================== Memory Management ====================

    /**
     * Allocate tensor on device
     */
    GPUTensor allocate(const std::vector<size_t>& shape, DType dtype);

    /**
     * Create tensor from host data
     */
    GPUTensor from_host(
        const void* data,
        const std::vector<size_t>& shape,
        DType dtype
    );

    /**
     * Create tensor from vector
     */
    GPUTensor from_vector(const std::vector<float>& data);
    GPUTensor from_vector(
        const std::vector<float>& data,
        const std::vector<size_t>& shape
    );

    /**
     * Copy tensor data to host
     */
    void to_host(const GPUTensor& tensor, void* host_data);
    std::vector<float> to_vector(const GPUTensor& tensor);

    /**
     * Get memory usage
     */
    size_t memory_allocated() const;
    size_t memory_reserved() const;

    /**
     * Clear memory cache
     */
    void clear_cache();

    // ==================== Batch Processing ====================

    /**
     * Set batch configuration
     */
    void set_batch_config(BatchConfig config);

    /**
     * Get batch configuration
     */
    BatchConfig get_batch_config() const;

    // ==================== Basic Operations ====================

    /**
     * Matrix multiplication: C = A @ B
     */
    GPUTensor matmul(const GPUTensor& a, const GPUTensor& b);

    /**
     * Element-wise addition
     */
    GPUTensor add(const GPUTensor& a, const GPUTensor& b);

    /**
     * Element-wise multiplication
     */
    GPUTensor mul(const GPUTensor& a, const GPUTensor& b);

    /**
     * Fused multiply-add: a * b + c
     */
    GPUTensor fma(const GPUTensor& a, const GPUTensor& b, const GPUTensor& c);

    /**
     * Softmax along axis
     */
    GPUTensor softmax(const GPUTensor& input, int axis = -1);

    /**
     * Layer normalization
     */
    GPUTensor layer_norm(
        const GPUTensor& input,
        const GPUTensor& weight,
        const GPUTensor& bias,
        float eps = 1e-5
    );

    /**
     * Batch normalization
     */
    GPUTensor batch_norm(
        const GPUTensor& input,
        const GPUTensor& mean,
        const GPUTensor& var,
        const GPUTensor& weight,
        const GPUTensor& bias,
        float eps = 1e-5
    );

    // ==================== Convolution ====================

    /**
     * 2D Convolution
     */
    GPUTensor conv2d(
        const GPUTensor& input,    // [N, C, H, W]
        const GPUTensor& kernel,   // [Out, In, KH, KW]
        int stride = 1,
        int padding = 0
    );

    /**
     * Max pooling
     */
    GPUTensor max_pool2d(
        const GPUTensor& input,
        int kernel_size,
        int stride = -1  // -1 = same as kernel_size
    );

    // ==================== Embedding Operations ====================

    /**
     * Embedding lookup
     */
    GPUTensor embedding_lookup(
        const GPUTensor& embeddings,  // [vocab_size, embed_dim]
        const GPUTensor& indices      // [batch, seq_len]
    );

    /**
     * Cosine similarity (batched)
     */
    GPUTensor cosine_similarity(
        const GPUTensor& query,       // [embed_dim]
        const GPUTensor& candidates   // [num_candidates, embed_dim]
    );

    /**
     * Batch cosine similarity
     */
    GPUTensor batch_cosine_similarity(
        const GPUTensor& queries,     // [num_queries, embed_dim]
        const GPUTensor& candidates   // [num_candidates, embed_dim]
    );

    // ==================== Image Processing ====================

    /**
     * Resize images
     */
    GPUTensor resize_images(
        const GPUTensor& images,  // [N, C, H, W] or [N, H, W, C]
        int target_height,
        int target_width,
        bool channels_first = true
    );

    /**
     * Normalize images (ImageNet normalization)
     */
    GPUTensor normalize_images(
        const GPUTensor& images,
        const std::vector<float>& mean = {0.485f, 0.456f, 0.406f},
        const std::vector<float>& std = {0.229f, 0.224f, 0.225f}
    );

    /**
     * Extract features using pre-loaded model
     */
    GPUTensor extract_features(
        const GPUTensor& images,
        const std::string& model_name  // "resnet50", "vit", etc.
    );

    // ==================== Audio Processing ====================

    /**
     * Compute spectrogram
     */
    GPUTensor compute_spectrogram(
        const GPUTensor& audio,
        int n_fft = 2048,
        int hop_length = 512
    );

    /**
     * Compute mel spectrogram
     */
    GPUTensor compute_mel_spectrogram(
        const GPUTensor& audio,
        int n_mels = 80,
        int n_fft = 2048,
        int hop_length = 512,
        int sample_rate = 16000
    );

    /**
     * Apply mel filterbank
     */
    GPUTensor mel_filterbank(
        const GPUTensor& spectrogram,
        int n_mels = 80
    );

    // ==================== Statistics ====================

    /**
     * Executor statistics
     */
    struct Stats {
        size_t total_ops;
        size_t gpu_ops;
        size_t cpu_fallback_ops;
        double gpu_utilization;
        double memory_utilization;
        double avg_batch_size;
        double total_compute_time_ms;
        double avg_op_time_ms;
    };
    Stats get_stats() const;

    /**
     * Reset statistics
     */
    void reset_stats();

    // ==================== Synchronization ====================

    /**
     * Synchronize all pending operations
     */
    void synchronize();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * Create GPU executor with automatic backend selection
 */
std::unique_ptr<GPUExecutor> create_gpu_executor();

/**
 * Create GPU executor with specific backend
 */
std::unique_ptr<GPUExecutor> create_gpu_executor(Backend backend);

/**
 * Check if CUDA is available
 */
bool cuda_available();

/**
 * Check if Metal is available
 */
bool metal_available();

/**
 * Get recommended backend for current system
 */
Backend get_recommended_backend();

}  // namespace neamc::gpu
