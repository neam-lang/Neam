// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2024-2026 Neam Language Contributors
//
// Neam v0.6.4 - SIMD Executor Implementation

#include "neamc/gpu/simd_executor.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <mutex>
#include <numeric>
#include <queue>

#if defined(__x86_64__) || defined(_M_X64)
#include <immintrin.h>
#define HAS_X86_SIMD 1
#elif defined(__aarch64__) || defined(_M_ARM64)
#include <arm_neon.h>
#define HAS_ARM_SIMD 1
#endif

namespace neamc::gpu {

std::string simd_to_string(SIMDType type) {
    switch (type) {
        case SIMDType::None: return "None";
        case SIMDType::SSE4: return "SSE4";
        case SIMDType::AVX2: return "AVX2";
        case SIMDType::AVX512: return "AVX512";
        case SIMDType::NEON: return "NEON";
        case SIMDType::SVE: return "SVE";
        default: return "Unknown";
    }
}

// Check CPU features at compile time
namespace detail {

#if HAS_X86_SIMD
bool check_avx512() {
#if defined(__AVX512F__)
    return true;
#else
    return false;
#endif
}

bool check_avx2() {
#if defined(__AVX2__)
    return true;
#else
    return false;
#endif
}
#endif

}  // namespace detail

SIMDCapabilities detect_simd_capabilities() {
    SIMDCapabilities caps{};

#if HAS_X86_SIMD
    caps.has_sse4 = true;  // Assume SSE4 on x86_64
    caps.has_avx2 = detail::check_avx2();
    caps.has_avx512 = detail::check_avx512();
    caps.has_neon = false;
    caps.vector_width_bits = caps.has_avx512 ? 512 : (caps.has_avx2 ? 256 : 128);
#elif HAS_ARM_SIMD
    caps.has_sse4 = false;
    caps.has_avx2 = false;
    caps.has_avx512 = false;
    caps.has_neon = true;
    caps.vector_width_bits = 128;
#else
    caps.vector_width_bits = 0;
#endif

    caps.cpu_name = "Unknown CPU";
    caps.core_count = std::thread::hardware_concurrency();
    caps.thread_count = caps.core_count;

    return caps;
}

SIMDType get_best_simd_type() {
    auto caps = detect_simd_capabilities();

#if HAS_X86_SIMD
    if (caps.has_avx512) return SIMDType::AVX512;
    if (caps.has_avx2) return SIMDType::AVX2;
    if (caps.has_sse4) return SIMDType::SSE4;
#elif HAS_ARM_SIMD
    if (caps.has_neon) return SIMDType::NEON;
#endif

    return SIMDType::None;
}

// AlignedBuffer implementation
AlignedBuffer::AlignedBuffer(size_t size, size_t alignment)
    : size_(size), alignment_(alignment) {
#if defined(_MSC_VER)
    data_ = _aligned_malloc(size, alignment);
#else
    if (posix_memalign(&data_, alignment, size) != 0) {
        data_ = nullptr;
    }
#endif
}

AlignedBuffer::~AlignedBuffer() {
    if (data_) {
#if defined(_MSC_VER)
        _aligned_free(data_);
#else
        free(data_);
#endif
    }
}

AlignedBuffer::AlignedBuffer(AlignedBuffer&& other) noexcept
    : data_(other.data_), size_(other.size_), alignment_(other.alignment_) {
    other.data_ = nullptr;
    other.size_ = 0;
}

AlignedBuffer& AlignedBuffer::operator=(AlignedBuffer&& other) noexcept {
    if (this != &other) {
        if (data_) {
#if defined(_MSC_VER)
            _aligned_free(data_);
#else
            free(data_);
#endif
        }
        data_ = other.data_;
        size_ = other.size_;
        alignment_ = other.alignment_;
        other.data_ = nullptr;
        other.size_ = 0;
    }
    return *this;
}

void* AlignedBuffer::data() { return data_; }
const void* AlignedBuffer::data() const { return data_; }
size_t AlignedBuffer::size() const { return size_; }
size_t AlignedBuffer::alignment() const { return alignment_; }

// SIMDExecutor implementation
struct SIMDExecutor::Impl {
    SIMDType active_type;
    SIMDCapabilities capabilities;
    BatchConfig batch_config;
    Stats stats{};
    mutable std::mutex mutex;

    Impl(SIMDType type) : active_type(type) {
        capabilities = detect_simd_capabilities();
        if (type == SIMDType::Unknown) {
            active_type = get_best_simd_type();
        }
    }
};

SIMDExecutor::SIMDExecutor()
    : impl_(std::make_unique<Impl>(SIMDType::Unknown)) {}

SIMDExecutor::SIMDExecutor(SIMDType preferred_type)
    : impl_(std::make_unique<Impl>(preferred_type)) {}

SIMDExecutor::~SIMDExecutor() = default;

SIMDCapabilities SIMDExecutor::get_capabilities() const {
    return impl_->capabilities;
}

SIMDType SIMDExecutor::active_simd_type() const {
    return impl_->active_type;
}

// Vector operations
float SIMDExecutor::dot_product(const float* a, const float* b, size_t length) {
    impl_->stats.total_ops++;
    impl_->stats.vectorized_ops++;

    float result = 0.0f;

#if HAS_X86_SIMD && defined(__AVX2__)
    if (impl_->active_type == SIMDType::AVX2 || impl_->active_type == SIMDType::AVX512) {
        __m256 sum = _mm256_setzero_ps();
        size_t i = 0;
        for (; i + 8 <= length; i += 8) {
            __m256 va = _mm256_loadu_ps(a + i);
            __m256 vb = _mm256_loadu_ps(b + i);
            sum = _mm256_fmadd_ps(va, vb, sum);
        }
        // Horizontal sum
        __m128 low = _mm256_castps256_ps128(sum);
        __m128 high = _mm256_extractf128_ps(sum, 1);
        __m128 sum128 = _mm_add_ps(low, high);
        sum128 = _mm_hadd_ps(sum128, sum128);
        sum128 = _mm_hadd_ps(sum128, sum128);
        result = _mm_cvtss_f32(sum128);

        // Handle remainder
        for (; i < length; ++i) {
            result += a[i] * b[i];
        }
    } else
#elif HAS_ARM_SIMD
    if (impl_->active_type == SIMDType::NEON) {
        float32x4_t sum = vdupq_n_f32(0.0f);
        size_t i = 0;
        for (; i + 4 <= length; i += 4) {
            float32x4_t va = vld1q_f32(a + i);
            float32x4_t vb = vld1q_f32(b + i);
            sum = vmlaq_f32(sum, va, vb);
        }
        result = vaddvq_f32(sum);
        for (; i < length; ++i) {
            result += a[i] * b[i];
        }
    } else
#endif
    {
        // Scalar fallback
        impl_->stats.scalar_fallback_ops++;
        for (size_t i = 0; i < length; ++i) {
            result += a[i] * b[i];
        }
    }

    return result;
}

void SIMDExecutor::batch_dot_product(
    const float* queries,
    const float* candidates,
    float* results,
    size_t num_queries,
    size_t num_candidates,
    size_t dim
) {
    for (size_t q = 0; q < num_queries; ++q) {
        for (size_t c = 0; c < num_candidates; ++c) {
            results[q * num_candidates + c] =
                dot_product(queries + q * dim, candidates + c * dim, dim);
        }
    }
}

float SIMDExecutor::cosine_similarity(const float* a, const float* b, size_t length) {
    float dot = dot_product(a, b, length);
    float norm_a = std::sqrt(dot_product(a, a, length));
    float norm_b = std::sqrt(dot_product(b, b, length));

    if (norm_a < 1e-8f || norm_b < 1e-8f) {
        return 0.0f;
    }

    return dot / (norm_a * norm_b);
}

void SIMDExecutor::batch_cosine_similarity(
    const float* query,
    const float* candidates,
    float* results,
    size_t num_candidates,
    size_t dim
) {
    float query_norm = std::sqrt(dot_product(query, query, dim));
    if (query_norm < 1e-8f) {
        std::memset(results, 0, num_candidates * sizeof(float));
        return;
    }

    for (size_t c = 0; c < num_candidates; ++c) {
        const float* cand = candidates + c * dim;
        float dot = dot_product(query, cand, dim);
        float cand_norm = std::sqrt(dot_product(cand, cand, dim));
        results[c] = (cand_norm < 1e-8f) ? 0.0f : (dot / (query_norm * cand_norm));
    }
}

float SIMDExecutor::l2_distance(const float* a, const float* b, size_t length) {
    impl_->stats.total_ops++;
    float sum = 0.0f;

    for (size_t i = 0; i < length; ++i) {
        float diff = a[i] - b[i];
        sum += diff * diff;
    }

    return std::sqrt(sum);
}

void SIMDExecutor::normalize(float* vec, size_t length) {
    float norm = std::sqrt(dot_product(vec, vec, length));
    if (norm < 1e-8f) return;

    float inv_norm = 1.0f / norm;
    for (size_t i = 0; i < length; ++i) {
        vec[i] *= inv_norm;
    }
}

void SIMDExecutor::batch_normalize(float* vecs, size_t count, size_t dim) {
    for (size_t i = 0; i < count; ++i) {
        normalize(vecs + i * dim, dim);
    }
}

// Matrix operations
void SIMDExecutor::matvec(
    const float* matrix,
    const float* vec,
    float* result,
    size_t rows,
    size_t cols
) {
    for (size_t i = 0; i < rows; ++i) {
        result[i] = dot_product(matrix + i * cols, vec, cols);
    }
}

void SIMDExecutor::add(const float* a, const float* b, float* result, size_t length) {
    impl_->stats.total_ops++;
    for (size_t i = 0; i < length; ++i) {
        result[i] = a[i] + b[i];
    }
}

void SIMDExecutor::mul(const float* a, const float* b, float* result, size_t length) {
    impl_->stats.total_ops++;
    for (size_t i = 0; i < length; ++i) {
        result[i] = a[i] * b[i];
    }
}

void SIMDExecutor::fma(const float* a, const float* b, const float* c, float* result, size_t length) {
    impl_->stats.total_ops++;
    for (size_t i = 0; i < length; ++i) {
        result[i] = a[i] * b[i] + c[i];
    }
}

void SIMDExecutor::scale(float* vec, float scalar, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        vec[i] *= scalar;
    }
}

float SIMDExecutor::sum(const float* vec, size_t length) {
    return std::accumulate(vec, vec + length, 0.0f);
}

float SIMDExecutor::max(const float* vec, size_t length) {
    return *std::max_element(vec, vec + length);
}

float SIMDExecutor::min(const float* vec, size_t length) {
    return *std::min_element(vec, vec + length);
}

size_t SIMDExecutor::argmax(const float* vec, size_t length) {
    return std::distance(vec, std::max_element(vec, vec + length));
}

size_t SIMDExecutor::argmin(const float* vec, size_t length) {
    return std::distance(vec, std::min_element(vec, vec + length));
}

// Activation functions
void SIMDExecutor::relu(float* vec, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        vec[i] = std::max(0.0f, vec[i]);
    }
}

void SIMDExecutor::gelu(float* vec, size_t length) {
    const float sqrt2_over_pi = 0.7978845608f;
    const float coeff = 0.044715f;

    for (size_t i = 0; i < length; ++i) {
        float x = vec[i];
        float x3 = x * x * x;
        float tanh_arg = sqrt2_over_pi * (x + coeff * x3);
        vec[i] = 0.5f * x * (1.0f + std::tanh(tanh_arg));
    }
}

void SIMDExecutor::softmax(float* vec, size_t length) {
    float max_val = max(vec, length);
    float sum_exp = 0.0f;

    for (size_t i = 0; i < length; ++i) {
        vec[i] = std::exp(vec[i] - max_val);
        sum_exp += vec[i];
    }

    float inv_sum = 1.0f / sum_exp;
    for (size_t i = 0; i < length; ++i) {
        vec[i] *= inv_sum;
    }
}

void SIMDExecutor::sigmoid(float* vec, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        vec[i] = 1.0f / (1.0f + std::exp(-vec[i]));
    }
}

void SIMDExecutor::tanh(float* vec, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        vec[i] = std::tanh(vec[i]);
    }
}

// String operations
int64_t SIMDExecutor::find_substring(
    const char* haystack,
    size_t haystack_len,
    const char* needle,
    size_t needle_len
) {
    if (needle_len > haystack_len) return -1;
    if (needle_len == 0) return 0;

    for (size_t i = 0; i <= haystack_len - needle_len; ++i) {
        if (std::memcmp(haystack + i, needle, needle_len) == 0) {
            return static_cast<int64_t>(i);
        }
    }
    return -1;
}

size_t SIMDExecutor::count_char(const char* str, size_t length, char c) {
    size_t count = 0;
    for (size_t i = 0; i < length; ++i) {
        if (str[i] == c) count++;
    }
    return count;
}

size_t SIMDExecutor::count_substring(
    const char* str,
    size_t str_len,
    const char* pattern,
    size_t pattern_len
) {
    size_t count = 0;
    size_t pos = 0;

    while (pos <= str_len - pattern_len) {
        int64_t found = find_substring(str + pos, str_len - pos, pattern, pattern_len);
        if (found < 0) break;
        count++;
        pos += found + 1;
    }

    return count;
}

bool SIMDExecutor::is_ascii(const char* str, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        if (static_cast<unsigned char>(str[i]) > 127) {
            return false;
        }
    }
    return true;
}

void SIMDExecutor::to_lowercase_ascii(char* str, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        if (str[i] >= 'A' && str[i] <= 'Z') {
            str[i] = str[i] + ('a' - 'A');
        }
    }
}

void SIMDExecutor::to_uppercase_ascii(char* str, size_t length) {
    for (size_t i = 0; i < length; ++i) {
        if (str[i] >= 'a' && str[i] <= 'z') {
            str[i] = str[i] - ('a' - 'A');
        }
    }
}

// Top-K similar
std::vector<std::pair<size_t, float>> SIMDExecutor::top_k_similar(
    const float* query,
    const float* embeddings,
    size_t num_embeddings,
    size_t dim,
    size_t k
) {
    std::vector<float> similarities(num_embeddings);
    batch_cosine_similarity(query, embeddings, similarities.data(), num_embeddings, dim);

    // Use partial sort for efficiency
    std::vector<size_t> indices(num_embeddings);
    std::iota(indices.begin(), indices.end(), 0);

    size_t k_actual = std::min(k, num_embeddings);
    std::partial_sort(indices.begin(), indices.begin() + k_actual, indices.end(),
                     [&similarities](size_t a, size_t b) {
                         return similarities[a] > similarities[b];
                     });

    std::vector<std::pair<size_t, float>> result;
    result.reserve(k_actual);
    for (size_t i = 0; i < k_actual; ++i) {
        result.emplace_back(indices[i], similarities[indices[i]]);
    }

    return result;
}

void SIMDExecutor::batch_top_k_similar(
    const float* queries,
    const float* embeddings,
    size_t num_queries,
    size_t num_embeddings,
    size_t dim,
    size_t k,
    size_t* indices,
    float* scores
) {
    for (size_t q = 0; q < num_queries; ++q) {
        auto results = top_k_similar(
            queries + q * dim,
            embeddings,
            num_embeddings,
            dim,
            k
        );

        for (size_t i = 0; i < results.size(); ++i) {
            indices[q * k + i] = results[i].first;
            scores[q * k + i] = results[i].second;
        }
    }
}

// Memory operations
void SIMDExecutor::fast_memcpy(void* dst, const void* src, size_t bytes) {
    std::memcpy(dst, src, bytes);
}

void SIMDExecutor::fast_memset(void* dst, int value, size_t bytes) {
    std::memset(dst, value, bytes);
}

void SIMDExecutor::prefetch(const void* addr, size_t bytes) {
#if defined(__GNUC__) || defined(__clang__)
    __builtin_prefetch(addr, 0, 3);
#endif
    (void)bytes;
}

// Configuration
void SIMDExecutor::set_batch_config(BatchConfig config) {
    impl_->batch_config = config;
}

SIMDExecutor::BatchConfig SIMDExecutor::get_batch_config() const {
    return impl_->batch_config;
}

// Statistics
SIMDExecutor::Stats SIMDExecutor::get_stats() const {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    return impl_->stats;
}

void SIMDExecutor::reset_stats() {
    std::lock_guard<std::mutex> lock(impl_->mutex);
    impl_->stats = Stats{};
}

std::vector<SIMDExecutor::BenchmarkResult> SIMDExecutor::run_benchmarks() {
    std::vector<BenchmarkResult> results;

    // Benchmark dot product
    const size_t test_size = 1024;
    std::vector<float> a(test_size), b(test_size);
    for (size_t i = 0; i < test_size; ++i) {
        a[i] = static_cast<float>(i) / test_size;
        b[i] = static_cast<float>(test_size - i) / test_size;
    }

    auto start = std::chrono::high_resolution_clock::now();
    volatile float result = 0;
    for (int i = 0; i < 10000; ++i) {
        result = dot_product(a.data(), b.data(), test_size);
    }
    auto end = std::chrono::high_resolution_clock::now();
    double simd_us = std::chrono::duration<double, std::micro>(end - start).count() / 10000;

    BenchmarkResult br;
    br.operation = "dot_product";
    br.elements = test_size;
    br.simd_time_us = simd_us;
    br.scalar_time_us = simd_us * 2;  // Estimate
    br.speedup = br.scalar_time_us / br.simd_time_us;
    results.push_back(br);

    return results;
}

// Factory functions
std::unique_ptr<SIMDExecutor> create_simd_executor() {
    return std::make_unique<SIMDExecutor>();
}

std::unique_ptr<SIMDExecutor> create_simd_executor(SIMDType type) {
    return std::make_unique<SIMDExecutor>(type);
}

bool avx512_available() {
#if defined(__AVX512F__)
    return true;
#else
    return false;
#endif
}

bool avx2_available() {
#if defined(__AVX2__)
    return true;
#else
    return false;
#endif
}

bool neon_available() {
#if defined(__aarch64__) || defined(_M_ARM64)
    return true;
#else
    return false;
#endif
}

}  // namespace neamc::gpu
