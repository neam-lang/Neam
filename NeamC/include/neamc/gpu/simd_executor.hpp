// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2024-2026 Neam Language Contributors
//
// Neam v0.6.4 - SIMD Executor
// CPU-side SIMD acceleration using AVX-512, AVX2, and NEON

#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <cstddef>
#include <cstdint>

namespace neamc::gpu {

/**
 * SIMD instruction set types
 */
enum class SIMDType {
    None,       // Scalar fallback
    SSE4,       // SSE4.2 (128-bit)
    AVX2,       // AVX2 (256-bit)
    AVX512,     // AVX-512 (512-bit)
    NEON,       // ARM NEON (128-bit)
    SVE,        // ARM SVE (scalable)
    Unknown
};

/**
 * Convert SIMD type to string
 */
std::string simd_to_string(SIMDType type);

/**
 * SIMD capabilities of current CPU
 */
struct SIMDCapabilities {
    bool has_sse4;
    bool has_avx2;
    bool has_avx512;
    bool has_avx512_vnni;  // For int8 inference
    bool has_neon;
    bool has_sve;
    int vector_width_bits;  // Maximum vector width
    std::string cpu_name;
    int core_count;
    int thread_count;
};

/**
 * Detect SIMD capabilities
 */
SIMDCapabilities detect_simd_capabilities();

/**
 * Get best available SIMD type
 */
SIMDType get_best_simd_type();

/**
 * Aligned memory buffer for SIMD operations
 */
class AlignedBuffer {
public:
    explicit AlignedBuffer(size_t size, size_t alignment = 64);
    ~AlignedBuffer();

    // Move semantics
    AlignedBuffer(AlignedBuffer&& other) noexcept;
    AlignedBuffer& operator=(AlignedBuffer&& other) noexcept;

    // No copying
    AlignedBuffer(const AlignedBuffer&) = delete;
    AlignedBuffer& operator=(const AlignedBuffer&) = delete;

    void* data();
    const void* data() const;
    size_t size() const;
    size_t alignment() const;

    template<typename T>
    T* as() { return static_cast<T*>(data()); }

    template<typename T>
    const T* as() const { return static_cast<const T*>(data()); }

private:
    void* data_;
    size_t size_;
    size_t alignment_;
};

/**
 * SIMD Executor - CPU-side vectorized operations
 *
 * Features:
 * - Auto-detection of best SIMD instruction set
 * - Embedding similarity with AVX-512/NEON
 * - Batch text preprocessing
 * - JSON parsing acceleration
 * - String operations (search, tokenize)
 */
class SIMDExecutor {
public:
    SIMDExecutor();
    explicit SIMDExecutor(SIMDType preferred_type);
    ~SIMDExecutor();

    // Prevent copying
    SIMDExecutor(const SIMDExecutor&) = delete;
    SIMDExecutor& operator=(const SIMDExecutor&) = delete;

    /**
     * Get current SIMD capabilities
     */
    SIMDCapabilities get_capabilities() const;

    /**
     * Get active SIMD type
     */
    SIMDType active_simd_type() const;

    // ==================== Vector Operations ====================

    /**
     * Dot product of two float vectors
     */
    float dot_product(const float* a, const float* b, size_t length);

    /**
     * Batch dot products: result[i] = dot(queries[i], candidates[i])
     */
    void batch_dot_product(
        const float* queries,      // [num_queries, dim]
        const float* candidates,   // [num_candidates, dim]
        float* results,            // [num_queries, num_candidates]
        size_t num_queries,
        size_t num_candidates,
        size_t dim
    );

    /**
     * Cosine similarity
     */
    float cosine_similarity(const float* a, const float* b, size_t length);

    /**
     * Batch cosine similarities
     */
    void batch_cosine_similarity(
        const float* query,        // [dim]
        const float* candidates,   // [num_candidates, dim]
        float* results,            // [num_candidates]
        size_t num_candidates,
        size_t dim
    );

    /**
     * L2 distance (Euclidean)
     */
    float l2_distance(const float* a, const float* b, size_t length);

    /**
     * Vector normalization (in-place)
     */
    void normalize(float* vec, size_t length);

    /**
     * Batch normalization
     */
    void batch_normalize(float* vecs, size_t count, size_t dim);

    // ==================== Matrix Operations ====================

    /**
     * Matrix-vector multiplication: y = A @ x
     */
    void matvec(
        const float* matrix,  // [rows, cols]
        const float* vec,     // [cols]
        float* result,        // [rows]
        size_t rows,
        size_t cols
    );

    /**
     * Element-wise operations
     */
    void add(const float* a, const float* b, float* result, size_t length);
    void mul(const float* a, const float* b, float* result, size_t length);
    void fma(const float* a, const float* b, const float* c, float* result, size_t length);
    void scale(float* vec, float scalar, size_t length);

    /**
     * Reduce operations
     */
    float sum(const float* vec, size_t length);
    float max(const float* vec, size_t length);
    float min(const float* vec, size_t length);
    size_t argmax(const float* vec, size_t length);
    size_t argmin(const float* vec, size_t length);

    // ==================== Activation Functions ====================

    /**
     * ReLU activation
     */
    void relu(float* vec, size_t length);

    /**
     * GELU activation (approximate)
     */
    void gelu(float* vec, size_t length);

    /**
     * Softmax
     */
    void softmax(float* vec, size_t length);

    /**
     * Sigmoid
     */
    void sigmoid(float* vec, size_t length);

    /**
     * Tanh
     */
    void tanh(float* vec, size_t length);

    // ==================== Quantization ====================

    /**
     * Quantize float32 to int8
     */
    void quantize_int8(
        const float* input,
        int8_t* output,
        size_t length,
        float& scale,
        int8_t& zero_point
    );

    /**
     * Dequantize int8 to float32
     */
    void dequantize_int8(
        const int8_t* input,
        float* output,
        size_t length,
        float scale,
        int8_t zero_point
    );

    /**
     * Int8 dot product (for quantized inference)
     */
    int32_t dot_product_int8(const int8_t* a, const int8_t* b, size_t length);

    // ==================== String Operations ====================

    /**
     * Fast string search using SIMD
     */
    int64_t find_substring(
        const char* haystack,
        size_t haystack_len,
        const char* needle,
        size_t needle_len
    );

    /**
     * Count occurrences of character
     */
    size_t count_char(const char* str, size_t length, char c);

    /**
     * Count occurrences of substring
     */
    size_t count_substring(
        const char* str,
        size_t str_len,
        const char* pattern,
        size_t pattern_len
    );

    /**
     * Check if string contains only ASCII
     */
    bool is_ascii(const char* str, size_t length);

    /**
     * Fast lowercase conversion (ASCII only)
     */
    void to_lowercase_ascii(char* str, size_t length);

    /**
     * Fast uppercase conversion (ASCII only)
     */
    void to_uppercase_ascii(char* str, size_t length);

    // ==================== JSON Acceleration ====================

    /**
     * Fast JSON structure validation
     * Returns offset of first error, or -1 if valid
     */
    int64_t validate_json_structure(const char* json, size_t length);

    /**
     * Find all occurrences of a JSON key
     * Returns vector of offsets to the values
     */
    std::vector<size_t> find_json_key(
        const char* json,
        size_t json_len,
        const char* key,
        size_t key_len
    );

    /**
     * Count JSON array elements (top-level)
     */
    size_t count_json_array_elements(const char* json, size_t length);

    // ==================== Text Processing ====================

    /**
     * Fast whitespace tokenization
     * Returns vector of (offset, length) pairs
     */
    std::vector<std::pair<size_t, size_t>> tokenize_whitespace(
        const char* text,
        size_t length
    );

    /**
     * Count words (whitespace-separated)
     */
    size_t count_words(const char* text, size_t length);

    /**
     * Count lines
     */
    size_t count_lines(const char* text, size_t length);

    /**
     * Find all newline positions
     */
    std::vector<size_t> find_newlines(const char* text, size_t length);

    // ==================== Batch Processing ====================

    /**
     * Batch configuration
     */
    struct BatchConfig {
        size_t max_batch_size = 64;
        bool parallel = true;
        int num_threads = 0;  // 0 = auto-detect
    };

    void set_batch_config(BatchConfig config);
    BatchConfig get_batch_config() const;

    /**
     * Process batch of embeddings for similarity search
     */
    std::vector<std::pair<size_t, float>> top_k_similar(
        const float* query,           // [dim]
        const float* embeddings,      // [num_embeddings, dim]
        size_t num_embeddings,
        size_t dim,
        size_t k
    );

    /**
     * Batch embedding similarity with multiple queries
     */
    void batch_top_k_similar(
        const float* queries,         // [num_queries, dim]
        const float* embeddings,      // [num_embeddings, dim]
        size_t num_queries,
        size_t num_embeddings,
        size_t dim,
        size_t k,
        size_t* indices,              // [num_queries, k]
        float* scores                 // [num_queries, k]
    );

    // ==================== Memory Operations ====================

    /**
     * Fast memory copy (uses non-temporal stores for large copies)
     */
    void fast_memcpy(void* dst, const void* src, size_t bytes);

    /**
     * Fast memory set
     */
    void fast_memset(void* dst, int value, size_t bytes);

    /**
     * Prefetch data into cache
     */
    void prefetch(const void* addr, size_t bytes);

    // ==================== Statistics ====================

    struct Stats {
        size_t total_ops;
        size_t vectorized_ops;
        size_t scalar_fallback_ops;
        double total_compute_time_ms;
        double avg_speedup_vs_scalar;
        size_t bytes_processed;
        std::map<std::string, size_t> ops_by_type;
    };
    Stats get_stats() const;

    void reset_stats();

    // ==================== Benchmarking ====================

    /**
     * Run microbenchmarks and return performance metrics
     */
    struct BenchmarkResult {
        std::string operation;
        double scalar_time_us;
        double simd_time_us;
        double speedup;
        size_t elements;
    };
    std::vector<BenchmarkResult> run_benchmarks();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

/**
 * Create SIMD executor with auto-detected best type
 */
std::unique_ptr<SIMDExecutor> create_simd_executor();

/**
 * Create SIMD executor with specific type
 */
std::unique_ptr<SIMDExecutor> create_simd_executor(SIMDType type);

/**
 * Check if AVX-512 is available
 */
bool avx512_available();

/**
 * Check if AVX2 is available
 */
bool avx2_available();

/**
 * Check if NEON is available
 */
bool neon_available();

}  // namespace neamc::gpu
