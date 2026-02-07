# Neam v0.6.4 Test Specification

## Cloud Management & Cost Efficiency Features

**Version:** 0.6.4
**Document Version:** 1.0
**Last Updated:** 2026-02-07

---

## Table of Contents

1. [Overview](#1-overview)
2. [Test Environment](#2-test-environment)
3. [Module 1: Intelligent Auto-Scaling](#3-module-1-intelligent-auto-scaling)
4. [Module 2: Multi-Cloud Orchestration](#4-module-2-multi-cloud-orchestration)
5. [Module 3: GPU/SIMD Acceleration](#5-module-3-gpusimd-acceleration)
6. [Module 4: FinOps Dashboard](#6-module-4-finops-dashboard)
7. [End-to-End Integration Tests](#7-end-to-end-integration-tests)
8. [Performance Benchmarks](#8-performance-benchmarks)
9. [Test Data & Mocks](#9-test-data--mocks)

---

## 1. Overview

### 1.1 Scope

This specification covers end-to-end testing for all v0.6.4 features:
- **432 unit tests** across 4 modules
- **48 integration tests** for cross-module functionality
- **24 end-to-end tests** for complete workflows
- **16 performance benchmarks** for latency and throughput

### 1.2 Test Categories

| Category | Count | Coverage Target |
|----------|-------|-----------------|
| Unit Tests | 432 | 90%+ line coverage |
| Integration Tests | 48 | All public APIs |
| E2E Tests | 24 | Critical user journeys |
| Performance Tests | 16 | Latency & throughput |
| **Total** | **520** | |

### 1.3 Success Criteria

- All tests pass on Linux x86_64, macOS ARM64, Windows x64
- No memory leaks (Valgrind/AddressSanitizer clean)
- P99 latency within specified bounds
- 90%+ code coverage for new modules

---

## 2. Test Environment

### 2.1 Required Infrastructure

```yaml
# test-infrastructure.yaml
environments:
  local:
    os: [linux-x64, macos-arm64, windows-x64]
    memory: 8GB minimum
    gpu: optional (for GPU tests)

  ci:
    runners:
      - ubuntu-22.04 (x64)
      - macos-14 (arm64)
      - windows-2022 (x64)
    gpu_runner: ubuntu-22.04-gpu (NVIDIA T4)

  integration:
    cloud_accounts:
      - aws: sandbox account
      - gcp: sandbox project
      - azure: sandbox subscription
      - alibaba: sandbox account (optional)
```

### 2.2 Test Dependencies

```cmake
# Test dependencies
find_package(GTest 1.14 REQUIRED)
find_package(GMock 1.14 REQUIRED)
find_package(benchmark 1.8 REQUIRED)

# Mock servers
# - Mock LLM server (port 8081)
# - Mock cloud pricing API (port 8082)
# - Mock metrics endpoint (port 8083)
```

### 2.3 Environment Variables

```bash
# Required for integration tests
export NEAM_TEST_MODE=1
export NEAM_MOCK_CLOUD=1
export NEAM_TEST_TIMEOUT=30

# Optional for cloud integration tests
export AWS_ACCESS_KEY_ID=test-key
export AWS_SECRET_ACCESS_KEY=test-secret
export ALIBABA_ACCESS_KEY_ID=test-key
export ALIBABA_ACCESS_KEY_SECRET=test-secret
```

---

## 3. Module 1: Intelligent Auto-Scaling

### 3.1 Predictive Scaler Tests

**File:** `tests/scaling/predictive_scaler_test.cpp`
**Test Count:** 45 tests

#### 3.1.1 Unit Tests

```cpp
// === Construction & Configuration ===

TEST(PredictiveScalerTest, DefaultConstruction) {
    auto scaler = create_predictive_scaler();
    EXPECT_TRUE(scaler != nullptr);
    EXPECT_TRUE(scaler->is_enabled());
    EXPECT_EQ(scaler->get_current_replicas(), 1);
}

TEST(PredictiveScalerTest, CustomPolicyConstruction) {
    ScalingPolicy policy;
    policy.min_replicas = 2;
    policy.max_replicas = 50;
    policy.target_cpu_utilization = 0.75;

    auto scaler = create_predictive_scaler(policy);
    auto retrieved = scaler->get_policy();

    EXPECT_EQ(retrieved.min_replicas, 2);
    EXPECT_EQ(retrieved.max_replicas, 50);
    EXPECT_DOUBLE_EQ(retrieved.target_cpu_utilization, 0.75);
}

TEST(PredictiveScalerTest, PolicyUpdate) {
    auto scaler = create_predictive_scaler();

    ScalingPolicy new_policy;
    new_policy.target_latency_p95_ms = 50.0;
    scaler->set_policy(new_policy);

    EXPECT_DOUBLE_EQ(scaler->get_policy().target_latency_p95_ms, 50.0);
}

// === Metrics Recording ===

TEST(PredictiveScalerTest, RecordSingleMetric) {
    auto scaler = create_predictive_scaler();

    ScalingMetrics metrics;
    metrics.request_rate = 100.0;
    metrics.cpu_utilization = 0.5;
    metrics.timestamp = std::chrono::system_clock::now();

    scaler->record_metrics(metrics);

    auto stats = scaler->get_stats();
    // Metrics should be recorded (internal state)
    EXPECT_NO_THROW(scaler->predict_load());
}

TEST(PredictiveScalerTest, RecordBatchMetrics) {
    auto scaler = create_predictive_scaler();

    std::vector<ScalingMetrics> batch;
    for (int i = 0; i < 100; ++i) {
        ScalingMetrics m;
        m.request_rate = 50.0 + i;
        m.cpu_utilization = 0.3 + (i * 0.005);
        m.timestamp = std::chrono::system_clock::now();
        batch.push_back(m);
    }

    scaler->record_metrics_batch(batch);
    auto prediction = scaler->predict_load();

    EXPECT_GT(prediction.confidence, 0.5);
}

TEST(PredictiveScalerTest, MetricsHistoryLimit) {
    auto scaler = create_predictive_scaler();

    // Record more than MAX_METRICS_HISTORY (10000)
    for (int i = 0; i < 15000; ++i) {
        ScalingMetrics m;
        m.request_rate = static_cast<double>(i);
        scaler->record_metrics(m);
    }

    // Should not crash, oldest metrics should be evicted
    EXPECT_NO_THROW(scaler->predict_load());
}

// === Load Prediction ===

TEST(PredictiveScalerTest, PredictLoadEmpty) {
    auto scaler = create_predictive_scaler();

    auto prediction = scaler->predict_load();

    EXPECT_EQ(prediction.predicted_rps_5min, 0.0);
    EXPECT_EQ(prediction.predicted_rps_1hr, 0.0);
    EXPECT_LT(prediction.confidence, 0.5);
}

TEST(PredictiveScalerTest, PredictLoadStable) {
    auto scaler = create_predictive_scaler();

    // Record stable traffic
    for (int i = 0; i < 100; ++i) {
        ScalingMetrics m;
        m.request_rate = 100.0;  // Constant
        m.cpu_utilization = 0.5;
        scaler->record_metrics(m);
    }

    auto prediction = scaler->predict_load();

    EXPECT_NEAR(prediction.predicted_rps_5min, 100.0, 10.0);
    EXPECT_EQ(prediction.trend, "stable");
}

TEST(PredictiveScalerTest, PredictLoadIncreasing) {
    auto scaler = create_predictive_scaler();

    // Record increasing traffic
    for (int i = 0; i < 100; ++i) {
        ScalingMetrics m;
        m.request_rate = 50.0 + i * 2.0;  // Linear increase
        m.cpu_utilization = 0.3 + (i * 0.005);
        scaler->record_metrics(m);
    }

    auto prediction = scaler->predict_load();

    EXPECT_GT(prediction.predicted_rps_5min, 200.0);
    EXPECT_EQ(prediction.trend, "increasing");
}

TEST(PredictiveScalerTest, PredictLoadSpike) {
    auto scaler = create_predictive_scaler();

    // Record normal traffic then spike
    for (int i = 0; i < 50; ++i) {
        ScalingMetrics m;
        m.request_rate = 100.0;
        scaler->record_metrics(m);
    }

    ScalingMetrics spike;
    spike.request_rate = 500.0;  // 5x spike
    scaler->record_metrics(spike);

    auto prediction = scaler->predict_load();

    EXPECT_EQ(prediction.trend, "spike");
}

TEST(PredictiveScalerTest, HourlyForecast) {
    auto scaler = create_predictive_scaler();

    for (int i = 0; i < 100; ++i) {
        ScalingMetrics m;
        m.request_rate = 100.0;
        scaler->record_metrics(m);
    }

    auto prediction = scaler->predict_load();

    EXPECT_EQ(prediction.hourly_forecast_24h.size(), 24);
    for (const auto& val : prediction.hourly_forecast_24h) {
        EXPECT_GE(val, 0.0);
    }
}

// === Scaling Decisions ===

TEST(PredictiveScalerTest, DecideNoChange) {
    ScalingPolicy policy;
    policy.target_cpu_utilization = 0.7;
    auto scaler = create_predictive_scaler(policy);

    ScalingMetrics m;
    m.cpu_utilization = 0.5;  // Below target
    m.latency_p95_ms = 50.0;
    scaler->record_metrics(m);

    auto decision = scaler->decide();

    EXPECT_EQ(decision.action, ScalingDecision::Action::NoChange);
}

TEST(PredictiveScalerTest, DecideScaleUpCPU) {
    ScalingPolicy policy;
    policy.target_cpu_utilization = 0.7;
    policy.max_replicas = 10;
    auto scaler = create_predictive_scaler(policy);
    scaler->set_current_replicas(2);

    ScalingMetrics m;
    m.cpu_utilization = 0.9;  // Above target
    m.latency_p95_ms = 50.0;
    scaler->record_metrics(m);

    auto decision = scaler->decide();

    EXPECT_EQ(decision.action, ScalingDecision::Action::ScaleUp);
    EXPECT_GT(decision.target_replicas, 2);
    EXPECT_LE(decision.target_replicas, 10);
}

TEST(PredictiveScalerTest, DecideScaleUpLatency) {
    ScalingPolicy policy;
    policy.target_latency_p95_ms = 100.0;
    policy.max_replicas = 10;
    auto scaler = create_predictive_scaler(policy);
    scaler->set_current_replicas(2);

    ScalingMetrics m;
    m.cpu_utilization = 0.5;
    m.latency_p95_ms = 150.0;  // Above target
    scaler->record_metrics(m);

    auto decision = scaler->decide_with_constraints(100.0, 100.0);

    EXPECT_EQ(decision.action, ScalingDecision::Action::ScaleUp);
}

TEST(PredictiveScalerTest, DecideScaleDown) {
    ScalingPolicy policy;
    policy.target_cpu_utilization = 0.7;
    policy.min_replicas = 1;
    auto scaler = create_predictive_scaler(policy);
    scaler->set_current_replicas(5);

    // Record sustained low utilization
    for (int i = 0; i < 15; ++i) {
        ScalingMetrics m;
        m.cpu_utilization = 0.2;  // Very low
        m.latency_p95_ms = 20.0;
        scaler->record_metrics(m);
    }

    auto decision = scaler->decide();

    EXPECT_EQ(decision.action, ScalingDecision::Action::ScaleDown);
    EXPECT_LT(decision.target_replicas, 5);
    EXPECT_GE(decision.target_replicas, 1);
}

TEST(PredictiveScalerTest, DecideRespectsMinReplicas) {
    ScalingPolicy policy;
    policy.min_replicas = 3;
    auto scaler = create_predictive_scaler(policy);
    scaler->set_current_replicas(3);

    for (int i = 0; i < 15; ++i) {
        ScalingMetrics m;
        m.cpu_utilization = 0.1;
        scaler->record_metrics(m);
    }

    auto decision = scaler->decide();

    // Should not scale below min
    EXPECT_GE(decision.target_replicas, 3);
}

TEST(PredictiveScalerTest, DecideRespectsMaxReplicas) {
    ScalingPolicy policy;
    policy.max_replicas = 5;
    auto scaler = create_predictive_scaler(policy);
    scaler->set_current_replicas(5);

    ScalingMetrics m;
    m.cpu_utilization = 0.99;
    scaler->record_metrics(m);

    auto decision = scaler->decide();

    // Should not scale above max
    EXPECT_LE(decision.target_replicas, 5);
}

// === Apply Callback ===

TEST(PredictiveScalerTest, ApplyWithoutCallback) {
    auto scaler = create_predictive_scaler();

    ScalingDecision decision;
    decision.action = ScalingDecision::Action::ScaleUp;
    decision.target_replicas = 5;

    bool result = scaler->apply_decision(decision);

    EXPECT_FALSE(result);  // No callback set
}

TEST(PredictiveScalerTest, ApplyWithCallback) {
    auto scaler = create_predictive_scaler();

    bool callback_invoked = false;
    int requested_replicas = 0;

    scaler->set_apply_callback([&](const ScalingDecision& d) {
        callback_invoked = true;
        requested_replicas = d.target_replicas;
        return true;
    });

    ScalingDecision decision;
    decision.action = ScalingDecision::Action::ScaleUp;
    decision.target_replicas = 5;

    bool result = scaler->apply_decision(decision);

    EXPECT_TRUE(result);
    EXPECT_TRUE(callback_invoked);
    EXPECT_EQ(requested_replicas, 5);
    EXPECT_EQ(scaler->get_current_replicas(), 5);
}

TEST(PredictiveScalerTest, ApplyCallbackFailure) {
    auto scaler = create_predictive_scaler();
    scaler->set_current_replicas(2);

    scaler->set_apply_callback([](const ScalingDecision&) {
        return false;  // Simulate failure
    });

    ScalingDecision decision;
    decision.target_replicas = 5;

    bool result = scaler->apply_decision(decision);

    EXPECT_FALSE(result);
    EXPECT_EQ(scaler->get_current_replicas(), 2);  // Unchanged
}

// === Scaling History ===

TEST(PredictiveScalerTest, ScalingHistoryTracking) {
    auto scaler = create_predictive_scaler();

    scaler->set_apply_callback([](const ScalingDecision&) { return true; });

    ScalingDecision d1;
    d1.action = ScalingDecision::Action::ScaleUp;
    d1.target_replicas = 3;
    scaler->apply_decision(d1);

    ScalingDecision d2;
    d2.action = ScalingDecision::Action::ScaleUp;
    d2.target_replicas = 5;
    scaler->apply_decision(d2);

    auto history = scaler->get_scaling_history(10);

    EXPECT_EQ(history.size(), 2);
}

// === Statistics ===

TEST(PredictiveScalerTest, StatsTracking) {
    auto scaler = create_predictive_scaler();
    scaler->set_apply_callback([](const ScalingDecision&) { return true; });

    ScalingDecision up;
    up.action = ScalingDecision::Action::ScaleUp;
    up.target_replicas = 3;
    scaler->apply_decision(up);

    ScalingDecision down;
    down.action = ScalingDecision::Action::ScaleDown;
    down.target_replicas = 2;
    scaler->apply_decision(down);

    auto stats = scaler->get_stats();

    EXPECT_EQ(stats.scale_up_events, 1);
    EXPECT_EQ(stats.scale_down_events, 1);
    EXPECT_EQ(stats.total_scaling_events, 2);
}

TEST(PredictiveScalerTest, StatsReset) {
    auto scaler = create_predictive_scaler();
    scaler->set_apply_callback([](const ScalingDecision&) { return true; });

    ScalingDecision d;
    d.action = ScalingDecision::Action::ScaleUp;
    scaler->apply_decision(d);

    scaler->reset_stats();
    auto stats = scaler->get_stats();

    EXPECT_EQ(stats.total_scaling_events, 0);
}

// === Enable/Disable ===

TEST(PredictiveScalerTest, EnableDisable) {
    auto scaler = create_predictive_scaler();

    EXPECT_TRUE(scaler->is_enabled());

    scaler->set_enabled(false);
    EXPECT_FALSE(scaler->is_enabled());

    scaler->set_enabled(true);
    EXPECT_TRUE(scaler->is_enabled());
}

// === Recommendations ===

TEST(PredictiveScalerTest, GetRecommendations) {
    auto scaler = create_predictive_scaler();

    // With few metrics
    auto recs = scaler->get_recommendations();
    EXPECT_FALSE(recs.empty());

    // Should recommend collecting more metrics
    bool found_metrics_rec = false;
    for (const auto& rec : recs) {
        if (rec.find("metrics") != std::string::npos) {
            found_metrics_rec = true;
            break;
        }
    }
    EXPECT_TRUE(found_metrics_rec);
}

// === Thread Safety ===

TEST(PredictiveScalerTest, ConcurrentMetricsRecording) {
    auto scaler = create_predictive_scaler();

    std::vector<std::thread> threads;
    for (int t = 0; t < 10; ++t) {
        threads.emplace_back([&scaler, t]() {
            for (int i = 0; i < 100; ++i) {
                ScalingMetrics m;
                m.request_rate = t * 100 + i;
                scaler->record_metrics(m);
            }
        });
    }

    for (auto& t : threads) {
        t.join();
    }

    // Should not crash, predictions should work
    EXPECT_NO_THROW(scaler->predict_load());
}
```

#### 3.1.2 Edge Cases

```cpp
// === Edge Cases ===

TEST(PredictiveScalerEdgeTest, ZeroCPUUtilization) {
    auto scaler = create_predictive_scaler();

    ScalingMetrics m;
    m.cpu_utilization = 0.0;
    scaler->record_metrics(m);

    auto decision = scaler->decide();
    EXPECT_NO_THROW(decision.action);
}

TEST(PredictiveScalerEdgeTest, MaxCPUUtilization) {
    auto scaler = create_predictive_scaler();

    ScalingMetrics m;
    m.cpu_utilization = 1.0;
    scaler->record_metrics(m);

    auto decision = scaler->decide();
    EXPECT_EQ(decision.action, ScalingDecision::Action::ScaleUp);
}

TEST(PredictiveScalerEdgeTest, NegativeMetricValues) {
    auto scaler = create_predictive_scaler();

    ScalingMetrics m;
    m.request_rate = -10.0;  // Invalid
    m.cpu_utilization = -0.5;  // Invalid

    // Should handle gracefully
    EXPECT_NO_THROW(scaler->record_metrics(m));
}

TEST(PredictiveScalerEdgeTest, VeryLargeRequestRate) {
    auto scaler = create_predictive_scaler();

    ScalingMetrics m;
    m.request_rate = 1e9;  // 1 billion RPS
    scaler->record_metrics(m);

    auto prediction = scaler->predict_load();
    EXPECT_GE(prediction.predicted_rps_5min, 0.0);
}
```
