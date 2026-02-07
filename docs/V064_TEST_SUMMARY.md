# Neam v0.6.4 Test Specification Summary

## Quick Reference

### Test Coverage Overview

| Module | Unit Tests | Integration | E2E | Benchmarks | Total |
|--------|-----------|-------------|-----|------------|-------|
| **Scaling** | 118 | 12 | 6 | 4 | 140 |
| - Predictive Scaler | 45 | - | - | 2 | 47 |
| - Warm Pool | 38 | - | - | 1 | 39 |
| - Resource Packer | 35 | - | - | 1 | 36 |
| **Multi-Cloud** | 72 | 8 | 4 | 2 | 86 |
| - Cost Router | 42 | - | - | 1 | 43 |
| - Alibaba Adapter | 30 | - | - | 1 | 31 |
| **GPU/SIMD** | 95 | 8 | 4 | 4 | 111 |
| - SIMD Executor | 55 | - | - | 3 | 58 |
| - GPU Executor | 40 | - | - | 1 | 41 |
| **FinOps** | 147 | 20 | 10 | 6 | 183 |
| - Cost Attribution | 45 | - | - | 2 | 47 |
| - Recommendations | 35 | - | - | 1 | 36 |
| - Benchmarking | 32 | - | - | 2 | 34 |
| - Dashboard | 35 | - | - | 1 | 36 |
| **Total** | **432** | **48** | **24** | **16** | **520** |

---

### Files Location

```
docs/
├── V064_TEST_SPECIFICATION.md       # Part 1: Overview + Predictive Scaler
├── V064_TEST_SPECIFICATION_PART2.md # Warm Pool + Resource Packer + Cost Router
├── V064_TEST_SPECIFICATION_PART3.md # SIMD + GPU Executor
├── V064_TEST_SPECIFICATION_PART4.md # FinOps + E2E + Benchmarks
└── V064_TEST_SUMMARY.md             # This file

tests/
├── scaling/
│   ├── predictive_scaler_test.cpp   # 45 tests
│   ├── warm_pool_test.cpp           # 38 tests
│   └── resource_packer_test.cpp     # 35 tests
├── multicloud/
│   ├── cost_router_test.cpp         # 42 tests
│   └── alibaba_adapter_test.cpp     # 30 tests
├── gpu/
│   ├── simd_executor_test.cpp       # 55 tests
│   └── gpu_executor_test.cpp        # 40 tests
├── finops/
│   ├── cost_attribution_test.cpp    # 45 tests
│   ├── recommendations_test.cpp     # 35 tests
│   ├── benchmarking_test.cpp        # 32 tests
│   └── dashboard_test.cpp           # 35 tests
├── integration/
│   └── v064_e2e_test.cpp            # 24 tests
├── benchmarks/
│   └── v064_benchmarks.cpp          # 16 benchmarks
└── mocks/
    ├── mock_cloud_pricing.hpp
    ├── mock_llm_provider.hpp
    └── mock_k8s_client.hpp
```

---

### Critical Test Scenarios

#### 1. Predictive Scaling

| Test | Description | Priority |
|------|-------------|----------|
| `PredictLoadStable` | Stable traffic prediction | Critical |
| `PredictLoadSpike` | Traffic spike detection | Critical |
| `DecideScaleUpCPU` | Scale-up on high CPU | Critical |
| `DecideRespectsMaxReplicas` | Max replica enforcement | Critical |
| `ConcurrentMetricsRecording` | Thread safety | High |

#### 2. Multi-Cloud Routing

| Test | Description | Priority |
|------|-------------|----------|
| `RouteBasic` | Basic routing | Critical |
| `RouteWithLatencyConstraint` | Latency-aware routing | Critical |
| `RouteWithDataResidency` | GDPR compliance | Critical |
| `CompareCosts` | Cost comparison | High |
| `MarkEndpointUnhealthy` | Failover | High |

#### 3. SIMD Acceleration

| Test | Description | Priority |
|------|-------------|----------|
| `DotProductBasic` | Vector ops correctness | Critical |
| `CosineSimilarityIdentical` | Similarity accuracy | Critical |
| `TopKSimilar` | RAG search | Critical |
| `BatchCosineSimilarity` | Batch performance | High |

#### 4. Cost Attribution

| Test | Description | Priority |
|------|-------------|----------|
| `RecordLLMCost` | LLM cost tracking | Critical |
| `CheckBudgets` | Budget alerts | Critical |
| `IsWithinBudget` | Budget enforcement | Critical |
| `ExportJSON` | Data export | High |

---

### Quick Commands

```bash
# Build tests
cmake -B build -DBUILD_TESTING=ON
cmake --build build

# Run all v0.6.4 tests
ctest --test-dir build -R "v064" --output-on-failure

# Run by module
ctest --test-dir build -R "scaling"
ctest --test-dir build -R "multicloud"
ctest --test-dir build -R "gpu|simd"
ctest --test-dir build -R "finops"
ctest --test-dir build -R "e2e"

# Run with parallel jobs
ctest --test-dir build -R "v064" -j8

# Run benchmarks
./build/tests/benchmarks/v064_benchmarks

# Generate coverage report
cmake -B build -DENABLE_COVERAGE=ON
cmake --build build
ctest --test-dir build
gcovr --html-details coverage.html
```

---

### Performance Targets

| Operation | Target | Measured |
|-----------|--------|----------|
| Dot product (1K floats) | < 1μs | TBD |
| Cosine similarity (768D) | < 5μs | TBD |
| Top-10 from 10K docs | < 50ms | TBD |
| Scaling decision | < 1ms | TBD |
| Route decision | < 100μs | TBD |
| Cost record | < 10μs | TBD |

---

### CI/CD Integration

```yaml
# .github/workflows/v064-tests.yml
name: v0.6.4 Tests

on: [push, pull_request]

jobs:
  test:
    runs-on: ${{ matrix.os }}
    strategy:
      matrix:
        os: [ubuntu-22.04, macos-14, windows-2022]

    steps:
      - uses: actions/checkout@v4

      - name: Configure
        run: cmake -B build -DBUILD_TESTING=ON

      - name: Build
        run: cmake --build build

      - name: Test
        run: ctest --test-dir build -R "v064" --output-on-failure

  benchmark:
    runs-on: ubuntu-22.04
    needs: test
    steps:
      - uses: actions/checkout@v4

      - name: Build benchmarks
        run: |
          cmake -B build -DBUILD_BENCHMARKS=ON
          cmake --build build

      - name: Run benchmarks
        run: ./build/tests/benchmarks/v064_benchmarks --benchmark_format=json > benchmark.json

      - name: Upload results
        uses: actions/upload-artifact@v4
        with:
          name: benchmark-results
          path: benchmark.json
```

---

### Success Criteria Checklist

- [ ] All 520 tests pass on Linux x86_64
- [ ] All 520 tests pass on macOS ARM64
- [ ] All 520 tests pass on Windows x64
- [ ] No memory leaks (Valgrind clean)
- [ ] No AddressSanitizer errors
- [ ] 90%+ code coverage for new modules
- [ ] All performance targets met
- [ ] Documentation complete

---

### Manual Test Procedures

#### Warm Pool Smoke Test
1. Start warm pool with `min_warm_instances=2`
2. Verify 2 instances created within 30s
3. Acquire instance, verify state changes to InUse
4. Release instance, verify returns to Ready
5. Stop pool, verify all instances terminated

#### Multi-Cloud Routing Test
1. Register AWS and GCP endpoints
2. Set GCP price lower than AWS
3. Route request, verify routes to GCP
4. Mark GCP unhealthy
5. Route request, verify fails over to AWS

#### Budget Enforcement Test
1. Create budget with $10 limit
2. Record $8 cost, verify warning alert
3. Record $2 more, verify critical alert
4. Record $1 more, verify hard limit enforced

---

### Troubleshooting

| Issue | Resolution |
|-------|------------|
| Tests timeout | Increase `NEAM_TEST_TIMEOUT` |
| GPU tests fail | Set `NEAM_MOCK_GPU=1` |
| Cloud tests fail | Ensure `NEAM_MOCK_CLOUD=1` |
| Flaky scaling tests | Increase metrics history |
| Memory leaks | Check tensor cleanup |
