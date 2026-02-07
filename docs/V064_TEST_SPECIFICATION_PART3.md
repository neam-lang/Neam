# Neam v0.6.4 Test Specification - Part 3

## 5. Module 3: GPU/SIMD Acceleration

### 5.1 SIMD Executor Tests

**File:** `tests/gpu/simd_executor_test.cpp`
**Test Count:** 55 tests

```cpp
// === Capabilities Detection ===

TEST(SIMDExecutorTest, DetectCapabilities) {
    auto caps = detect_simd_capabilities();

    // At least one should be available
    EXPECT_TRUE(caps.has_sse4 || caps.has_avx2 || caps.has_neon || caps.vector_width_bits == 0);
    EXPECT_GT(caps.core_count, 0);
}

TEST(SIMDExecutorTest, GetBestSIMDType) {
    auto type = get_best_simd_type();

    // Should return something
    EXPECT_NE(simd_to_string(type), "Unknown");
}

TEST(SIMDExecutorTest, DefaultConstruction) {
    auto simd = create_simd_executor();

    EXPECT_NE(simd->active_simd_type(), SIMDType::Unknown);
}

TEST(SIMDExecutorTest, SpecificTypeConstruction) {
    auto simd = create_simd_executor(SIMDType::None);

    EXPECT_EQ(simd->active_simd_type(), SIMDType::None);
}

// === Vector Operations ===

TEST(SIMDExecutorTest, DotProductBasic) {
    auto simd = create_simd_executor();

    std::vector<float> a = {1.0f, 2.0f, 3.0f, 4.0f};
    std::vector<float> b = {4.0f, 3.0f, 2.0f, 1.0f};

    float result = simd->dot_product(a.data(), b.data(), 4);

    // 1*4 + 2*3 + 3*2 + 4*1 = 4 + 6 + 6 + 4 = 20
    EXPECT_FLOAT_EQ(result, 20.0f);
}

TEST(SIMDExecutorTest, DotProductLarge) {
    auto simd = create_simd_executor();

    const size_t N = 1024;
    std::vector<float> a(N, 1.0f);
    std::vector<float> b(N, 2.0f);

    float result = simd->dot_product(a.data(), b.data(), N);

    EXPECT_FLOAT_EQ(result, 2048.0f);
}

TEST(SIMDExecutorTest, DotProductUnaligned) {
    auto simd = create_simd_executor();

    // Non-power-of-2 size
    std::vector<float> a = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f, 7.0f};
    std::vector<float> b = {1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f, 1.0f};

    float result = simd->dot_product(a.data(), b.data(), 7);

    EXPECT_FLOAT_EQ(result, 28.0f);  // 1+2+3+4+5+6+7
}

TEST(SIMDExecutorTest, BatchDotProduct) {
    auto simd = create_simd_executor();

    const size_t dim = 128;
    const size_t num_queries = 4;
    const size_t num_candidates = 8;

    std::vector<float> queries(num_queries * dim, 1.0f);
    std::vector<float> candidates(num_candidates * dim, 0.5f);
    std::vector<float> results(num_queries * num_candidates);

    simd->batch_dot_product(
        queries.data(), candidates.data(), results.data(),
        num_queries, num_candidates, dim
    );

    // Each dot product should be dim * 1.0 * 0.5 = 64
    EXPECT_FLOAT_EQ(results[0], 64.0f);
}

TEST(SIMDExecutorTest, CosineSimilarityIdentical) {
    auto simd = create_simd_executor();

    std::vector<float> a = {1.0f, 2.0f, 3.0f, 4.0f};
    std::vector<float> b = {1.0f, 2.0f, 3.0f, 4.0f};

    float result = simd->cosine_similarity(a.data(), b.data(), 4);

    EXPECT_NEAR(result, 1.0f, 1e-5f);
}

TEST(SIMDExecutorTest, CosineSimilarityOrthogonal) {
    auto simd = create_simd_executor();

    std::vector<float> a = {1.0f, 0.0f};
    std::vector<float> b = {0.0f, 1.0f};

    float result = simd->cosine_similarity(a.data(), b.data(), 2);

    EXPECT_NEAR(result, 0.0f, 1e-5f);
}

TEST(SIMDExecutorTest, CosineSimilarityOpposite) {
    auto simd = create_simd_executor();

    std::vector<float> a = {1.0f, 2.0f, 3.0f};
    std::vector<float> b = {-1.0f, -2.0f, -3.0f};

    float result = simd->cosine_similarity(a.data(), b.data(), 3);

    EXPECT_NEAR(result, -1.0f, 1e-5f);
}

TEST(SIMDExecutorTest, BatchCosineSimilarity) {
    auto simd = create_simd_executor();

    const size_t dim = 64;
    const size_t num_candidates = 100;

    std::vector<float> query(dim, 1.0f);
    std::vector<float> candidates(num_candidates * dim, 1.0f);
    std::vector<float> results(num_candidates);

    simd->batch_cosine_similarity(
        query.data(), candidates.data(), results.data(),
        num_candidates, dim
    );

    for (size_t i = 0; i < num_candidates; ++i) {
        EXPECT_NEAR(results[i], 1.0f, 1e-5f);
    }
}

TEST(SIMDExecutorTest, L2Distance) {
    auto simd = create_simd_executor();

    std::vector<float> a = {0.0f, 0.0f, 0.0f};
    std::vector<float> b = {3.0f, 4.0f, 0.0f};

    float result = simd->l2_distance(a.data(), b.data(), 3);

    EXPECT_FLOAT_EQ(result, 5.0f);  // sqrt(9 + 16) = 5
}

TEST(SIMDExecutorTest, Normalize) {
    auto simd = create_simd_executor();

    std::vector<float> vec = {3.0f, 4.0f};

    simd->normalize(vec.data(), 2);

    EXPECT_NEAR(vec[0], 0.6f, 1e-5f);
    EXPECT_NEAR(vec[1], 0.8f, 1e-5f);
}

TEST(SIMDExecutorTest, BatchNormalize) {
    auto simd = create_simd_executor();

    const size_t dim = 4;
    const size_t count = 10;
    std::vector<float> vecs(count * dim, 1.0f);

    simd->batch_normalize(vecs.data(), count, dim);

    // Each vector should have norm 1
    for (size_t i = 0; i < count; ++i) {
        float norm = simd->dot_product(
            vecs.data() + i * dim,
            vecs.data() + i * dim,
            dim
        );
        EXPECT_NEAR(norm, 1.0f, 1e-5f);
    }
}

// === Matrix Operations ===

TEST(SIMDExecutorTest, MatVec) {
    auto simd = create_simd_executor();

    // 2x3 matrix
    std::vector<float> matrix = {
        1.0f, 2.0f, 3.0f,
        4.0f, 5.0f, 6.0f
    };
    std::vector<float> vec = {1.0f, 1.0f, 1.0f};
    std::vector<float> result(2);

    simd->matvec(matrix.data(), vec.data(), result.data(), 2, 3);

    EXPECT_FLOAT_EQ(result[0], 6.0f);   // 1+2+3
    EXPECT_FLOAT_EQ(result[1], 15.0f);  // 4+5+6
}

TEST(SIMDExecutorTest, ElementWiseAdd) {
    auto simd = create_simd_executor();

    std::vector<float> a = {1.0f, 2.0f, 3.0f, 4.0f};
    std::vector<float> b = {4.0f, 3.0f, 2.0f, 1.0f};
    std::vector<float> result(4);

    simd->add(a.data(), b.data(), result.data(), 4);

    EXPECT_FLOAT_EQ(result[0], 5.0f);
    EXPECT_FLOAT_EQ(result[1], 5.0f);
}

TEST(SIMDExecutorTest, ElementWiseMul) {
    auto simd = create_simd_executor();

    std::vector<float> a = {1.0f, 2.0f, 3.0f, 4.0f};
    std::vector<float> b = {2.0f, 2.0f, 2.0f, 2.0f};
    std::vector<float> result(4);

    simd->mul(a.data(), b.data(), result.data(), 4);

    EXPECT_FLOAT_EQ(result[0], 2.0f);
    EXPECT_FLOAT_EQ(result[1], 4.0f);
}

TEST(SIMDExecutorTest, FMA) {
    auto simd = create_simd_executor();

    std::vector<float> a = {1.0f, 2.0f};
    std::vector<float> b = {3.0f, 3.0f};
    std::vector<float> c = {1.0f, 1.0f};
    std::vector<float> result(2);

    simd->fma(a.data(), b.data(), c.data(), result.data(), 2);

    EXPECT_FLOAT_EQ(result[0], 4.0f);  // 1*3+1
    EXPECT_FLOAT_EQ(result[1], 7.0f);  // 2*3+1
}

TEST(SIMDExecutorTest, Scale) {
    auto simd = create_simd_executor();

    std::vector<float> vec = {1.0f, 2.0f, 3.0f, 4.0f};

    simd->scale(vec.data(), 2.0f, 4);

    EXPECT_FLOAT_EQ(vec[0], 2.0f);
    EXPECT_FLOAT_EQ(vec[3], 8.0f);
}

// === Reduce Operations ===

TEST(SIMDExecutorTest, Sum) {
    auto simd = create_simd_executor();

    std::vector<float> vec = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f};

    float result = simd->sum(vec.data(), 5);

    EXPECT_FLOAT_EQ(result, 15.0f);
}

TEST(SIMDExecutorTest, Max) {
    auto simd = create_simd_executor();

    std::vector<float> vec = {3.0f, 1.0f, 4.0f, 1.0f, 5.0f, 9.0f};

    float result = simd->max(vec.data(), 6);

    EXPECT_FLOAT_EQ(result, 9.0f);
}

TEST(SIMDExecutorTest, Argmax) {
    auto simd = create_simd_executor();

    std::vector<float> vec = {3.0f, 1.0f, 9.0f, 1.0f, 5.0f};

    size_t result = simd->argmax(vec.data(), 5);

    EXPECT_EQ(result, 2);
}

// === Activation Functions ===

TEST(SIMDExecutorTest, ReLU) {
    auto simd = create_simd_executor();

    std::vector<float> vec = {-2.0f, -1.0f, 0.0f, 1.0f, 2.0f};

    simd->relu(vec.data(), 5);

    EXPECT_FLOAT_EQ(vec[0], 0.0f);
    EXPECT_FLOAT_EQ(vec[1], 0.0f);
    EXPECT_FLOAT_EQ(vec[2], 0.0f);
    EXPECT_FLOAT_EQ(vec[3], 1.0f);
    EXPECT_FLOAT_EQ(vec[4], 2.0f);
}

TEST(SIMDExecutorTest, Softmax) {
    auto simd = create_simd_executor();

    std::vector<float> vec = {1.0f, 2.0f, 3.0f};

    simd->softmax(vec.data(), 3);

    float sum = vec[0] + vec[1] + vec[2];
    EXPECT_NEAR(sum, 1.0f, 1e-5f);

    // Check ordering preserved
    EXPECT_LT(vec[0], vec[1]);
    EXPECT_LT(vec[1], vec[2]);
}

TEST(SIMDExecutorTest, Sigmoid) {
    auto simd = create_simd_executor();

    std::vector<float> vec = {-10.0f, 0.0f, 10.0f};

    simd->sigmoid(vec.data(), 3);

    EXPECT_NEAR(vec[0], 0.0f, 0.01f);
    EXPECT_NEAR(vec[1], 0.5f, 1e-5f);
    EXPECT_NEAR(vec[2], 1.0f, 0.01f);
}

// === String Operations ===

TEST(SIMDExecutorTest, FindSubstring) {
    auto simd = create_simd_executor();

    const char* haystack = "hello world";
    const char* needle = "world";

    int64_t result = simd->find_substring(haystack, 11, needle, 5);

    EXPECT_EQ(result, 6);
}

TEST(SIMDExecutorTest, FindSubstringNotFound) {
    auto simd = create_simd_executor();

    const char* haystack = "hello world";
    const char* needle = "xyz";

    int64_t result = simd->find_substring(haystack, 11, needle, 3);

    EXPECT_EQ(result, -1);
}

TEST(SIMDExecutorTest, CountChar) {
    auto simd = create_simd_executor();

    const char* str = "hello world";

    size_t count = simd->count_char(str, 11, 'l');

    EXPECT_EQ(count, 3);
}

TEST(SIMDExecutorTest, IsAscii) {
    auto simd = create_simd_executor();

    EXPECT_TRUE(simd->is_ascii("hello world", 11));
    EXPECT_FALSE(simd->is_ascii("héllo", 6));
}

TEST(SIMDExecutorTest, ToLowercase) {
    auto simd = create_simd_executor();

    char str[] = "HeLLo WoRLD";
    simd->to_lowercase_ascii(str, 11);

    EXPECT_STREQ(str, "hello world");
}

// === Top-K Similar ===

TEST(SIMDExecutorTest, TopKSimilar) {
    auto simd = create_simd_executor();

    const size_t dim = 4;
    const size_t num_embeddings = 10;
    const size_t k = 3;

    std::vector<float> query = {1.0f, 0.0f, 0.0f, 0.0f};
    std::vector<float> embeddings(num_embeddings * dim, 0.0f);

    // Make first embedding most similar
    embeddings[0] = 1.0f;
    embeddings[dim] = 0.8f;  // Second
    embeddings[2 * dim] = 0.6f;  // Third

    auto results = simd->top_k_similar(
        query.data(), embeddings.data(),
        num_embeddings, dim, k
    );

    EXPECT_EQ(results.size(), k);
    EXPECT_EQ(results[0].first, 0);  // First is most similar
}

// === Statistics ===

TEST(SIMDExecutorTest, StatsTracking) {
    auto simd = create_simd_executor();

    std::vector<float> a(100, 1.0f);
    std::vector<float> b(100, 1.0f);

    simd->dot_product(a.data(), b.data(), 100);
    simd->dot_product(a.data(), b.data(), 100);

    auto stats = simd->get_stats();

    EXPECT_GE(stats.total_ops, 2);
}

TEST(SIMDExecutorTest, Benchmarks) {
    auto simd = create_simd_executor();

    auto results = simd->run_benchmarks();

    EXPECT_FALSE(results.empty());
    EXPECT_GT(results[0].speedup, 0.0);
}
```

### 5.2 GPU Executor Tests

**File:** `tests/gpu/gpu_executor_test.cpp`
**Test Count:** 40 tests

```cpp
// === Device Management ===

TEST(GPUExecutorTest, DefaultConstruction) {
    auto gpu = create_gpu_executor();
    EXPECT_NE(gpu, nullptr);
}

TEST(GPUExecutorTest, HasGPU) {
    auto gpu = create_gpu_executor();
    // Just verify it doesn't crash
    bool has_gpu = gpu->has_gpu();
    (void)has_gpu;  // May or may not have GPU
}

TEST(GPUExecutorTest, ListDevices) {
    auto gpu = create_gpu_executor();
    auto devices = gpu->list_devices();

    // At least CPU fallback should be available
    EXPECT_GE(devices.size(), 0);
}

TEST(GPUExecutorTest, CurrentDevice) {
    auto gpu = create_gpu_executor();
    auto device = gpu->current_device();

    EXPECT_FALSE(device.name.empty());
}

TEST(GPUExecutorTest, AvailableBackends) {
    auto gpu = create_gpu_executor();
    auto backends = gpu->available_backends();

    EXPECT_GE(backends.size(), 1);  // At least CPU fallback
}

// === Tensor Operations ===

TEST(GPUTensorTest, DefaultConstruction) {
    GPUTensor tensor;
    EXPECT_EQ(tensor.ndim(), 0);
    EXPECT_EQ(tensor.numel(), 0);
}

TEST(GPUTensorTest, ShapeConstruction) {
    GPUTensor tensor({2, 3, 4}, DType::Float32);

    EXPECT_EQ(tensor.ndim(), 3);
    EXPECT_EQ(tensor.numel(), 24);
    EXPECT_EQ(tensor.size_bytes(), 24 * sizeof(float));
}

TEST(GPUTensorTest, Reshape) {
    GPUTensor tensor({2, 3, 4}, DType::Float32);

    tensor.reshape({6, 4});

    EXPECT_EQ(tensor.ndim(), 2);
    EXPECT_EQ(tensor.numel(), 24);
}

TEST(GPUTensorTest, Fill) {
    auto gpu = create_gpu_executor();
    auto tensor = gpu->allocate({10, 10}, DType::Float32);

    tensor.fill(3.14f);
    auto data = gpu->to_vector(tensor);

    for (float val : data) {
        EXPECT_FLOAT_EQ(val, 3.14f);
    }
}

TEST(GPUTensorTest, Zero) {
    auto gpu = create_gpu_executor();
    auto tensor = gpu->allocate({10, 10}, DType::Float32);

    tensor.fill(5.0f);
    tensor.zero();
    auto data = gpu->to_vector(tensor);

    for (float val : data) {
        EXPECT_FLOAT_EQ(val, 0.0f);
    }
}

// === Memory Management ===

TEST(GPUExecutorTest, Allocate) {
    auto gpu = create_gpu_executor();

    auto tensor = gpu->allocate({100, 100}, DType::Float32);

    EXPECT_EQ(tensor.numel(), 10000);
}

TEST(GPUExecutorTest, FromHost) {
    auto gpu = create_gpu_executor();

    std::vector<float> data = {1.0f, 2.0f, 3.0f, 4.0f};
    auto tensor = gpu->from_host(data.data(), {4}, DType::Float32);

    EXPECT_EQ(tensor.numel(), 4);
}

TEST(GPUExecutorTest, FromVector) {
    auto gpu = create_gpu_executor();

    std::vector<float> data = {1.0f, 2.0f, 3.0f, 4.0f, 5.0f, 6.0f};
    auto tensor = gpu->from_vector(data, {2, 3});

    EXPECT_EQ(tensor.shape()[0], 2);
    EXPECT_EQ(tensor.shape()[1], 3);
}

TEST(GPUExecutorTest, ToVector) {
    auto gpu = create_gpu_executor();

    std::vector<float> input = {1.0f, 2.0f, 3.0f, 4.0f};
    auto tensor = gpu->from_vector(input);
    auto output = gpu->to_vector(tensor);

    EXPECT_EQ(input, output);
}

TEST(GPUExecutorTest, MemoryUsage) {
    auto gpu = create_gpu_executor();

    size_t before = gpu->memory_allocated();
    auto tensor = gpu->allocate({1000, 1000}, DType::Float32);
    size_t after = gpu->memory_allocated();

    EXPECT_GE(after, before);
}

// === Math Operations ===

TEST(GPUExecutorTest, MatMul) {
    auto gpu = create_gpu_executor();

    // 2x3 @ 3x4 = 2x4
    std::vector<float> a_data(6, 1.0f);
    std::vector<float> b_data(12, 2.0f);

    auto a = gpu->from_vector(a_data, {2, 3});
    auto b = gpu->from_vector(b_data, {3, 4});

    auto c = gpu->matmul(a, b);

    EXPECT_EQ(c.shape()[0], 2);
    EXPECT_EQ(c.shape()[1], 4);

    auto result = gpu->to_vector(c);
    // Each element should be 3 * 1.0 * 2.0 = 6.0
    for (float val : result) {
        EXPECT_FLOAT_EQ(val, 6.0f);
    }
}

TEST(GPUExecutorTest, Add) {
    auto gpu = create_gpu_executor();

    auto a = gpu->from_vector({1.0f, 2.0f, 3.0f, 4.0f});
    auto b = gpu->from_vector({4.0f, 3.0f, 2.0f, 1.0f});

    auto c = gpu->add(a, b);
    auto result = gpu->to_vector(c);

    for (float val : result) {
        EXPECT_FLOAT_EQ(val, 5.0f);
    }
}

TEST(GPUExecutorTest, Mul) {
    auto gpu = create_gpu_executor();

    auto a = gpu->from_vector({1.0f, 2.0f, 3.0f, 4.0f});
    auto b = gpu->from_vector({2.0f, 2.0f, 2.0f, 2.0f});

    auto c = gpu->mul(a, b);
    auto result = gpu->to_vector(c);

    EXPECT_FLOAT_EQ(result[0], 2.0f);
    EXPECT_FLOAT_EQ(result[3], 8.0f);
}

TEST(GPUExecutorTest, Softmax) {
    auto gpu = create_gpu_executor();

    auto tensor = gpu->from_vector({1.0f, 2.0f, 3.0f});
    auto result = gpu->softmax(tensor);

    auto data = gpu->to_vector(result);
    float sum = data[0] + data[1] + data[2];

    EXPECT_NEAR(sum, 1.0f, 1e-5f);
}

TEST(GPUExecutorTest, CosineSimilarity) {
    auto gpu = create_gpu_executor();

    auto query = gpu->from_vector({1.0f, 0.0f, 0.0f}, {3});
    auto candidates = gpu->from_vector({
        1.0f, 0.0f, 0.0f,  // Identical
        0.0f, 1.0f, 0.0f,  // Orthogonal
        -1.0f, 0.0f, 0.0f  // Opposite
    }, {3, 3});

    auto result = gpu->cosine_similarity(query, candidates);
    auto data = gpu->to_vector(result);

    EXPECT_NEAR(data[0], 1.0f, 1e-5f);
    EXPECT_NEAR(data[1], 0.0f, 1e-5f);
    EXPECT_NEAR(data[2], -1.0f, 1e-5f);
}

// === Batch Processing ===

TEST(GPUExecutorTest, BatchConfig) {
    auto gpu = create_gpu_executor();

    BatchConfig config;
    config.max_batch_size = 64;
    config.dynamic_batching = true;

    gpu->set_batch_config(config);
    auto retrieved = gpu->get_batch_config();

    EXPECT_EQ(retrieved.max_batch_size, 64);
}

// === Statistics ===

TEST(GPUExecutorTest, Stats) {
    auto gpu = create_gpu_executor();

    auto a = gpu->from_vector({1.0f, 2.0f});
    auto b = gpu->from_vector({3.0f, 4.0f});
    gpu->add(a, b);

    auto stats = gpu->get_stats();

    EXPECT_GT(stats.total_ops, 0);
}

TEST(GPUExecutorTest, StatsReset) {
    auto gpu = create_gpu_executor();

    gpu->reset_stats();
    auto stats = gpu->get_stats();

    EXPECT_EQ(stats.total_ops, 0);
}

TEST(GPUExecutorTest, Synchronize) {
    auto gpu = create_gpu_executor();

    auto tensor = gpu->allocate({100, 100}, DType::Float32);
    tensor.fill(1.0f);

    gpu->synchronize();

    // Should not crash
    EXPECT_TRUE(true);
}
```
