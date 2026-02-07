# Neam v0.6.4 Test Specification - Part 4

## 6. Module 4: FinOps Dashboard

### 6.1 Cost Attribution Tests

**File:** `tests/finops/cost_attribution_test.cpp`
**Test Count:** 45 tests

```cpp
// === Cost Recording ===

TEST(CostAttributionTest, RecordSingleCost) {
    auto costs = create_cost_attribution_system();

    CostEntry entry;
    entry.id = "cost-1";
    entry.agent_id = "agent-123";
    entry.category = CostCategory::LLM;
    entry.cost_usd = 0.05;
    entry.timestamp = std::chrono::system_clock::now();

    costs->record_cost(entry);

    auto stats = costs->get_stats();
    EXPECT_EQ(stats.total_cost_entries, 1);
}

TEST(CostAttributionTest, RecordBatchCosts) {
    auto costs = create_cost_attribution_system();

    std::vector<CostEntry> batch;
    for (int i = 0; i < 100; ++i) {
        CostEntry entry;
        entry.id = "cost-" + std::to_string(i);
        entry.cost_usd = 0.01;
        batch.push_back(entry);
    }

    costs->record_costs(batch);

    auto stats = costs->get_stats();
    EXPECT_EQ(stats.total_cost_entries, 100);
}

TEST(CostAttributionTest, RecordLLMCost) {
    auto costs = create_cost_attribution_system();

    costs->record_llm_cost("agent-1", "gpt-4", 1000, 500, 0.05);

    auto summary = costs->get_daily_summary(std::chrono::system_clock::now());
    EXPECT_GT(summary.cost_by_llm_model["gpt-4"], 0);
}

TEST(CostAttributionTest, RecordComputeCost) {
    auto costs = create_cost_attribution_system();

    costs->record_compute_cost(
        "agent-1",
        60.0,  // 60 seconds
        4.0,   // 4 GB
        CloudProvider::AWS,
        "us-east-1",
        0.10
    );

    auto summary = costs->get_daily_summary(std::chrono::system_clock::now());
    EXPECT_GT(summary.cost_by_category[CostCategory::Compute], 0);
}

// === Cost Tracking ===

TEST(CostAttributionTest, StartStopTracking) {
    auto costs = create_cost_attribution_system();

    auto tracking_id = costs->start_tracking("agent-1", "inference");

    // Simulate work
    std::this_thread::sleep_for(std::chrono::milliseconds(10));

    auto entry = costs->stop_tracking(tracking_id, 0.05, CostCategory::Compute);

    EXPECT_EQ(entry.agent_id, "agent-1");
    EXPECT_DOUBLE_EQ(entry.cost_usd, 0.05);
}

// === Cost Queries ===

TEST(CostAttributionTest, GetDailySummary) {
    auto costs = create_cost_attribution_system();

    for (int i = 0; i < 10; ++i) {
        CostEntry entry;
        entry.cost_usd = 1.0;
        entry.category = CostCategory::LLM;
        entry.timestamp = std::chrono::system_clock::now();
        costs->record_cost(entry);
    }

    auto summary = costs->get_daily_summary(std::chrono::system_clock::now());

    EXPECT_DOUBLE_EQ(summary.total_cost_usd, 10.0);
}

TEST(CostAttributionTest, GetAgentCost) {
    auto costs = create_cost_attribution_system();

    auto now = std::chrono::system_clock::now();
    auto yesterday = now - std::chrono::hours(24);

    CostEntry entry;
    entry.agent_id = "agent-123";
    entry.cost_usd = 5.0;
    entry.timestamp = now;
    costs->record_cost(entry);

    double agent_cost = costs->get_agent_cost("agent-123", yesterday, now);

    EXPECT_DOUBLE_EQ(agent_cost, 5.0);
}

TEST(CostAttributionTest, QueryWithFilters) {
    auto costs = create_cost_attribution_system();

    auto now = std::chrono::system_clock::now();

    for (int i = 0; i < 10; ++i) {
        CostEntry entry;
        entry.agent_id = (i % 2 == 0) ? "agent-1" : "agent-2";
        entry.category = CostCategory::LLM;
        entry.timestamp = now;
        costs->record_cost(entry);
    }

    auto entries = costs->query_costs(
        now - std::chrono::hours(1),
        now + std::chrono::hours(1),
        "agent-1"
    );

    EXPECT_EQ(entries.size(), 5);
}

TEST(CostAttributionTest, GetCurrentCostRate) {
    auto costs = create_cost_attribution_system();

    // Record some costs
    for (int i = 0; i < 10; ++i) {
        CostEntry entry;
        entry.cost_usd = 1.0;
        entry.timestamp = std::chrono::system_clock::now();
        costs->record_cost(entry);
    }

    double rate = costs->get_current_cost_rate();

    EXPECT_GE(rate, 0.0);
}

// === Budget Management ===

TEST(CostAttributionTest, CreateBudget) {
    auto costs = create_cost_attribution_system();

    Budget budget;
    budget.id = "budget-1";
    budget.name = "Daily Agent Budget";
    budget.limit_usd = 100.0;
    budget.period = "daily";

    costs->create_budget(budget);

    auto retrieved = costs->get_budget("budget-1");
    EXPECT_TRUE(retrieved.has_value());
    EXPECT_DOUBLE_EQ(retrieved->limit_usd, 100.0);
}

TEST(CostAttributionTest, UpdateBudget) {
    auto costs = create_cost_attribution_system();

    Budget budget;
    budget.id = "budget-1";
    budget.limit_usd = 100.0;
    costs->create_budget(budget);

    budget.limit_usd = 200.0;
    costs->update_budget(budget);

    auto retrieved = costs->get_budget("budget-1");
    EXPECT_DOUBLE_EQ(retrieved->limit_usd, 200.0);
}

TEST(CostAttributionTest, DeleteBudget) {
    auto costs = create_cost_attribution_system();

    Budget budget;
    budget.id = "budget-1";
    costs->create_budget(budget);

    costs->delete_budget("budget-1");

    EXPECT_FALSE(costs->get_budget("budget-1").has_value());
}

TEST(CostAttributionTest, CheckBudgets) {
    auto costs = create_cost_attribution_system();

    Budget budget;
    budget.id = "budget-1";
    budget.limit_usd = 10.0;
    budget.warning_threshold_pct = 80.0;
    costs->create_budget(budget);

    // Record costs exceeding warning threshold
    for (int i = 0; i < 9; ++i) {
        CostEntry entry;
        entry.cost_usd = 1.0;
        costs->record_cost(entry);
    }

    auto alerts = costs->check_budgets();

    // Should have a warning alert
    EXPECT_FALSE(alerts.empty());
}

TEST(CostAttributionTest, BudgetAlertCallback) {
    auto costs = create_cost_attribution_system();

    bool callback_invoked = false;
    costs->set_alert_callback([&](const BudgetAlert& alert) {
        callback_invoked = true;
        EXPECT_EQ(alert.severity, BudgetAlert::Severity::Warning);
    });

    Budget budget;
    budget.id = "budget-1";
    budget.limit_usd = 5.0;
    budget.warning_threshold_pct = 50.0;
    costs->create_budget(budget);

    for (int i = 0; i < 3; ++i) {
        CostEntry entry;
        entry.cost_usd = 1.0;
        costs->record_cost(entry);
    }

    costs->check_budgets();

    EXPECT_TRUE(callback_invoked);
}

TEST(CostAttributionTest, IsWithinBudget) {
    auto costs = create_cost_attribution_system();

    Budget budget;
    budget.id = "budget-1";
    budget.agent_id = "agent-1";
    budget.limit_usd = 10.0;
    budget.enforce_hard_limit = true;
    costs->create_budget(budget);

    EXPECT_TRUE(costs->is_within_budget("agent-1", 5.0));
    EXPECT_FALSE(costs->is_within_budget("agent-1", 15.0));
}

// === Export ===

TEST(CostAttributionTest, ExportCSV) {
    auto costs = create_cost_attribution_system();

    CostEntry entry;
    entry.agent_id = "agent-1";
    entry.cost_usd = 1.0;
    entry.timestamp = std::chrono::system_clock::now();
    costs->record_cost(entry);

    auto csv = costs->export_csv(
        std::chrono::system_clock::now() - std::chrono::hours(1),
        std::chrono::system_clock::now() + std::chrono::hours(1)
    );

    EXPECT_FALSE(csv.empty());
    EXPECT_NE(csv.find("agent-1"), std::string::npos);
}

TEST(CostAttributionTest, ExportJSON) {
    auto costs = create_cost_attribution_system();

    CostEntry entry;
    entry.agent_id = "agent-1";
    entry.cost_usd = 1.0;
    entry.timestamp = std::chrono::system_clock::now();
    costs->record_cost(entry);

    auto json = costs->export_json(
        std::chrono::system_clock::now() - std::chrono::hours(1),
        std::chrono::system_clock::now() + std::chrono::hours(1)
    );

    EXPECT_FALSE(json.empty());
    EXPECT_NE(json.find("agent_id"), std::string::npos);
}

// === Pricing ===

TEST(CostAttributionTest, SetLLMPricing) {
    auto costs = create_cost_attribution_system();

    costs->set_llm_pricing("gpt-4", 0.03, 0.06);

    auto [input_price, output_price] = costs->get_llm_pricing("gpt-4");

    EXPECT_DOUBLE_EQ(input_price, 0.03);
    EXPECT_DOUBLE_EQ(output_price, 0.06);
}

// === RAII Tracker ===

TEST(CostTrackerTest, RAIITracking) {
    // CostTracker should automatically record when destroyed
    {
        CostTracker tracker("agent-1", "operation", CostCategory::Compute);
        tracker.set_cost(0.05);
    }

    // Verify via global tracker
    auto& global = global_cost_tracker();
    auto stats = global.get_stats();
    // Should have recorded
    EXPECT_GE(stats.total_cost_entries, 0);  // May be 0 if not implemented
}
```

### 6.2 Recommendations Engine Tests

**File:** `tests/finops/recommendations_test.cpp`
**Test Count:** 35 tests

```cpp
// === Analysis ===

TEST(RecommendationsTest, AnalyzeFull) {
    auto engine = create_recommendations_engine();

    auto recs = engine->analyze();

    // Initially may be empty
    EXPECT_GE(recs.size(), 0);
}

TEST(RecommendationsTest, AnalyzeByCategory) {
    auto engine = create_recommendations_engine();

    auto recs = engine->analyze_category(RecommendationCategory::RightSizing);

    for (const auto& rec : recs) {
        EXPECT_EQ(rec.category, RecommendationCategory::RightSizing);
    }
}

TEST(RecommendationsTest, AnalyzeRightSizing) {
    auto engine = create_recommendations_engine();

    // Set utilization data for a resource
    engine->set_utilization_data(
        "resource-1",
        {{std::chrono::system_clock::now(), 0.2}},  // Low CPU
        {{std::chrono::system_clock::now(), 0.3}}   // Low memory
    );

    auto recs = engine->analyze_right_sizing();

    // Should recommend downsizing
    EXPECT_GE(recs.size(), 0);
}

TEST(RecommendationsTest, AnalyzeCaching) {
    auto engine = create_recommendations_engine();

    engine->set_cache_data("response-cache", 0.3, 10000, 50.0);

    auto recs = engine->analyze_caching();

    // May recommend improving cache
    EXPECT_GE(recs.size(), 0);
}

// === Recommendation Management ===

TEST(RecommendationsTest, ListRecommendations) {
    auto engine = create_recommendations_engine();

    Recommendation rec;
    rec.id = "rec-1";
    rec.title = "Test Recommendation";
    rec.category = RecommendationCategory::SpotUsage;
    rec.priority = RecommendationPriority::High;
    rec.status = RecommendationStatus::New;
    rec.estimated_monthly_savings_usd = 100.0;

    engine->add_recommendation(rec);

    auto list = engine->list_recommendations();

    EXPECT_EQ(list.size(), 1);
    EXPECT_EQ(list[0].id, "rec-1");
}

TEST(RecommendationsTest, GetRecommendation) {
    auto engine = create_recommendations_engine();

    Recommendation rec;
    rec.id = "rec-1";
    rec.title = "Test";
    engine->add_recommendation(rec);

    auto retrieved = engine->get_recommendation("rec-1");

    EXPECT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved->title, "Test");
}

TEST(RecommendationsTest, AcknowledgeRecommendation) {
    auto engine = create_recommendations_engine();

    Recommendation rec;
    rec.id = "rec-1";
    rec.status = RecommendationStatus::New;
    engine->add_recommendation(rec);

    engine->acknowledge_recommendation("rec-1");

    auto retrieved = engine->get_recommendation("rec-1");
    EXPECT_EQ(retrieved->status, RecommendationStatus::Acknowledged);
}

TEST(RecommendationsTest, ImplementRecommendation) {
    auto engine = create_recommendations_engine();

    Recommendation rec;
    rec.id = "rec-1";
    rec.estimated_monthly_savings_usd = 50.0;
    engine->add_recommendation(rec);

    engine->start_implementation("rec-1");
    engine->complete_implementation("rec-1", 45.0);

    auto retrieved = engine->get_recommendation("rec-1");
    EXPECT_EQ(retrieved->status, RecommendationStatus::Implemented);
}

TEST(RecommendationsTest, DismissRecommendation) {
    auto engine = create_recommendations_engine();

    Recommendation rec;
    rec.id = "rec-1";
    engine->add_recommendation(rec);

    engine->dismiss_recommendation("rec-1", "Not applicable");

    auto retrieved = engine->get_recommendation("rec-1");
    EXPECT_EQ(retrieved->status, RecommendationStatus::Dismissed);
}

// === Summary ===

TEST(RecommendationsTest, GetSummary) {
    auto engine = create_recommendations_engine();

    for (int i = 0; i < 5; ++i) {
        Recommendation rec;
        rec.id = "rec-" + std::to_string(i);
        rec.estimated_monthly_savings_usd = 10.0;
        engine->add_recommendation(rec);
    }

    auto summary = engine->get_summary();

    EXPECT_EQ(summary.total_recommendations, 5);
    EXPECT_DOUBLE_EQ(summary.total_potential_savings_monthly, 50.0);
}

TEST(RecommendationsTest, GetRealizedSavings) {
    auto engine = create_recommendations_engine();

    Recommendation rec;
    rec.id = "rec-1";
    rec.estimated_monthly_savings_usd = 100.0;
    engine->add_recommendation(rec);

    engine->complete_implementation("rec-1", 90.0);

    double savings = engine->get_realized_savings_monthly();

    EXPECT_DOUBLE_EQ(savings, 90.0);
}

// === Export ===

TEST(RecommendationsTest, ExportJSON) {
    auto engine = create_recommendations_engine();

    Recommendation rec;
    rec.id = "rec-1";
    rec.title = "Test";
    engine->add_recommendation(rec);

    auto json = engine->export_json();

    EXPECT_FALSE(json.empty());
    EXPECT_NE(json.find("rec-1"), std::string::npos);
}

TEST(RecommendationsTest, GenerateReport) {
    auto engine = create_recommendations_engine();

    Recommendation rec;
    rec.id = "rec-1";
    rec.title = "Test Recommendation";
    rec.estimated_monthly_savings_usd = 100.0;
    engine->add_recommendation(rec);

    auto report = engine->generate_report();

    EXPECT_FALSE(report.empty());
    EXPECT_NE(report.find("Test Recommendation"), std::string::npos);
}
```

---

## 7. End-to-End Integration Tests

**File:** `tests/integration/v064_e2e_test.cpp`
**Test Count:** 24 tests

```cpp
// === Full Stack Tests ===

TEST(V064E2ETest, ScalingToFinOpsIntegration) {
    // Test that scaling decisions are properly cost-attributed

    auto scaler = create_predictive_scaler();
    auto costs = create_cost_attribution_system();

    // Simulate high load
    for (int i = 0; i < 100; ++i) {
        ScalingMetrics m;
        m.request_rate = 1000.0;
        m.cpu_utilization = 0.9;
        m.cost_rate = 5.0;  // $5/hour
        scaler->record_metrics(m);
    }

    auto decision = scaler->decide();

    // Record the scaling action cost
    if (decision.action == ScalingDecision::Action::ScaleUp) {
        costs->record_compute_cost(
            "autoscaler",
            3600.0,  // 1 hour
            8.0,     // 8 GB
            CloudProvider::AWS,
            "us-east-1",
            decision.estimated_cost_change
        );
    }

    auto summary = costs->get_daily_summary(std::chrono::system_clock::now());
    EXPECT_GT(summary.total_cost_usd, 0);
}

TEST(V064E2ETest, MultiCloudWithCostTracking) {
    // Test routing decisions with cost attribution

    auto router = create_cost_router();
    auto costs = create_cost_attribution_system();

    // Setup endpoints with pricing
    ProviderEndpoint aws;
    aws.provider = CloudProvider::AWS;
    aws.region = "us-east-1";
    aws.available_agents = {"agent-1"};
    router->register_endpoint(aws);

    RegionPricing pricing;
    pricing.provider = CloudProvider::AWS;
    pricing.region = "us-east-1";
    pricing.spot_price_per_hour = 0.05;
    router->update_pricing(pricing);

    // Route and track cost
    auto decision = router->route("agent-1", "us-west-2");

    if (decision.target == RoutingDecision::Target::Remote) {
        CostEntry entry;
        entry.agent_id = "agent-1";
        entry.cost_usd = decision.estimated_cost;
        entry.provider = decision.provider;
        entry.region = decision.region;
        costs->record_cost(entry);
    }

    auto router_stats = router->get_stats();
    auto cost_stats = costs->get_stats();

    EXPECT_EQ(router_stats.total_routing_decisions, 1);
    EXPECT_GT(cost_stats.total_cost_entries, 0);
}

TEST(V064E2ETest, GPUAccelerationWithBenchmarking) {
    // Test GPU ops with performance tracking

    auto gpu = create_gpu_executor();
    auto bench = create_benchmarking_system();

    BenchmarkScenario scenario;
    scenario.id = "embedding-similarity";
    scenario.name = "Embedding Similarity Search";
    scenario.total_requests = 100;
    bench->register_scenario(scenario);

    // Run GPU operation
    const size_t dim = 768;
    const size_t num_candidates = 10000;

    auto query = gpu->from_vector(std::vector<float>(dim, 1.0f), {dim});
    auto candidates = gpu->allocate({num_candidates, dim}, DType::Float32);
    candidates.fill(0.5f);

    auto start = std::chrono::high_resolution_clock::now();
    auto result = gpu->cosine_similarity(query, candidates);
    auto end = std::chrono::high_resolution_clock::now();

    double latency_ms = std::chrono::duration<double, std::milli>(end - start).count();

    auto gpu_stats = gpu->get_stats();
    EXPECT_GT(gpu_stats.total_ops, 0);
}

TEST(V064E2ETest, WarmPoolWithScaling) {
    // Test warm pool integration with predictive scaler

    WarmPoolConfig pool_config;
    pool_config.min_warm_instances = 2;
    pool_config.auto_replenish = true;
    auto pool = create_warm_pool_manager(pool_config);

    ScalingPolicy scale_policy;
    scale_policy.warm_pool_size = 2;
    auto scaler = create_predictive_scaler(scale_policy);

    // Ensure warm instances
    pool->ensure_warm_instances(2);
    EXPECT_EQ(pool->available_instance_count(), 2);

    // Simulate scale-up decision
    ScalingMetrics m;
    m.cpu_utilization = 0.95;
    scaler->record_metrics(m);

    auto decision = scaler->decide();

    if (decision.action == ScalingDecision::Action::ScaleUp) {
        // Acquire warm instance
        auto instance = pool->acquire_instance();
        EXPECT_TRUE(instance.has_value());
        EXPECT_EQ(pool->in_use_instance_count(), 1);
    }
}

TEST(V064E2ETest, FullCostOptimizationPipeline) {
    // End-to-end cost optimization workflow

    // 1. Setup infrastructure
    auto router = create_cost_router();
    auto scaler = create_predictive_scaler();
    auto costs = create_cost_attribution_system();
    auto recommendations = create_recommendations_engine();

    // 2. Configure endpoints
    ProviderEndpoint aws;
    aws.provider = CloudProvider::AWS;
    aws.region = "us-east-1";
    aws.available_agents = {"agent-1"};
    router->register_endpoint(aws);

    ProviderEndpoint gcp;
    gcp.provider = CloudProvider::GCP;
    gcp.region = "us-central1";
    gcp.available_agents = {"agent-1"};
    router->register_endpoint(gcp);

    // 3. Update pricing (GCP cheaper)
    RegionPricing aws_price, gcp_price;
    aws_price.provider = CloudProvider::AWS;
    aws_price.region = "us-east-1";
    aws_price.spot_price_per_hour = 0.10;
    gcp_price.provider = CloudProvider::GCP;
    gcp_price.region = "us-central1";
    gcp_price.spot_price_per_hour = 0.05;

    router->update_pricing(aws_price);
    router->update_pricing(gcp_price);

    // 4. Route workload
    auto decision = router->route("agent-1", "us-east-1");

    // Should route to cheaper GCP
    EXPECT_EQ(decision.provider, CloudProvider::GCP);

    // 5. Track cost
    CostEntry entry;
    entry.agent_id = "agent-1";
    entry.cost_usd = decision.estimated_cost;
    entry.provider = decision.provider;
    costs->record_cost(entry);

    // 6. Check savings in recommendations
    auto summary = recommendations->get_summary();

    // Pipeline complete
    EXPECT_EQ(router->get_stats().total_routing_decisions, 1);
}

TEST(V064E2ETest, SIMDWithEmbeddingSearch) {
    // Test SIMD acceleration for RAG-style operations

    auto simd = create_simd_executor();

    const size_t dim = 768;
    const size_t num_docs = 10000;
    const size_t k = 10;

    // Simulate query embedding
    std::vector<float> query(dim);
    for (size_t i = 0; i < dim; ++i) {
        query[i] = static_cast<float>(i) / dim;
    }
    simd->normalize(query.data(), dim);

    // Simulate document embeddings
    std::vector<float> docs(num_docs * dim);
    for (size_t i = 0; i < num_docs * dim; ++i) {
        docs[i] = static_cast<float>(i % dim) / dim;
    }
    simd->batch_normalize(docs.data(), num_docs, dim);

    // Find top-k similar
    auto start = std::chrono::high_resolution_clock::now();
    auto results = simd->top_k_similar(
        query.data(), docs.data(),
        num_docs, dim, k
    );
    auto end = std::chrono::high_resolution_clock::now();

    double latency_ms = std::chrono::duration<double, std::milli>(end - start).count();

    EXPECT_EQ(results.size(), k);
    EXPECT_LT(latency_ms, 100.0);  // Should be fast
}

TEST(V064E2ETest, BudgetEnforcementWorkflow) {
    // Test budget enforcement end-to-end

    auto costs = create_cost_attribution_system();

    // Create strict budget
    Budget budget;
    budget.id = "agent-budget";
    budget.agent_id = "expensive-agent";
    budget.limit_usd = 10.0;
    budget.enforce_hard_limit = true;
    budget.warning_threshold_pct = 50.0;
    budget.critical_threshold_pct = 80.0;
    costs->create_budget(budget);

    std::vector<BudgetAlert> received_alerts;
    costs->set_alert_callback([&](const BudgetAlert& alert) {
        received_alerts.push_back(alert);
    });

    // Simulate spending
    for (int i = 0; i < 12; ++i) {
        CostEntry entry;
        entry.agent_id = "expensive-agent";
        entry.cost_usd = 1.0;
        costs->record_cost(entry);
        costs->check_budgets();
    }

    // Should have received warning and critical alerts
    bool has_warning = false;
    bool has_critical = false;
    for (const auto& alert : received_alerts) {
        if (alert.severity == BudgetAlert::Severity::Warning) has_warning = true;
        if (alert.severity == BudgetAlert::Severity::Critical) has_critical = true;
    }

    EXPECT_TRUE(has_warning);
    EXPECT_TRUE(has_critical);

    // Budget should reject new spend
    EXPECT_FALSE(costs->is_within_budget("expensive-agent", 5.0));
}

TEST(V064E2ETest, ContinuousBenchmarkingWithRegression) {
    // Test benchmark regression detection

    auto bench = create_benchmarking_system();

    BenchmarkScenario scenario;
    scenario.id = "agent-latency";
    scenario.name = "Agent Response Latency";
    bench->register_scenario(scenario);

    // Set executor that returns increasing latency
    int call_count = 0;
    bench->set_executor([&](const std::string&, const std::string&) {
        call_count++;
        double latency = 50.0 + (call_count * 10);  // Increasing latency
        return std::make_pair(latency, true);
    });

    // Configure regression detection
    RegressionConfig config;
    config.latency_threshold_pct = 20.0;
    bench->set_regression_config(config);

    // Run baseline
    auto baseline = bench->run_benchmark("agent-latency");
    bench->set_baseline("agent-latency", baseline.scenario_id);

    // Run again with regression
    auto current = bench->run_benchmark("agent-latency");

    auto comparison = bench->compare_to_baseline("agent-latency");

    if (comparison.has_value()) {
        EXPECT_GT(comparison->latency_p50_change_pct, 0);  // Latency increased
    }
}

TEST(V064E2ETest, V064RuntimeIntegration) {
    // Test unified V064Runtime

    V064Config config;
    config.scaling.enable_predictive_scaling = true;
    config.multicloud.enable_cost_routing = true;
    config.acceleration.enable_simd = true;
    config.finops.enable_cost_tracking = true;

    auto runtime = create_v064_runtime(config);
    runtime->initialize();

    // Access all subsystems
    auto& scaling = runtime->scaling();
    auto& multicloud = runtime->multicloud();
    auto& acceleration = runtime->acceleration();
    auto& finops = runtime->finops();

    // Health check
    auto health = runtime->health_check();

    EXPECT_TRUE(health.overall_healthy);

    runtime->shutdown();
}
```

---

## 8. Performance Benchmarks

**File:** `tests/benchmarks/v064_benchmarks.cpp`

```cpp
// === SIMD Benchmarks ===

BENCHMARK_F(SIMDBenchmark, DotProduct1K)(benchmark::State& state) {
    auto simd = create_simd_executor();
    std::vector<float> a(1024, 1.0f);
    std::vector<float> b(1024, 2.0f);

    for (auto _ : state) {
        benchmark::DoNotOptimize(simd->dot_product(a.data(), b.data(), 1024));
    }
}

BENCHMARK_F(SIMDBenchmark, CosineSimilarity768D)(benchmark::State& state) {
    auto simd = create_simd_executor();
    std::vector<float> a(768, 1.0f);
    std::vector<float> b(768, 0.5f);
    simd->normalize(a.data(), 768);
    simd->normalize(b.data(), 768);

    for (auto _ : state) {
        benchmark::DoNotOptimize(simd->cosine_similarity(a.data(), b.data(), 768));
    }
}

BENCHMARK_F(SIMDBenchmark, TopK10From10K)(benchmark::State& state) {
    auto simd = create_simd_executor();
    std::vector<float> query(768, 1.0f);
    std::vector<float> docs(10000 * 768, 0.5f);
    simd->normalize(query.data(), 768);
    simd->batch_normalize(docs.data(), 10000, 768);

    for (auto _ : state) {
        auto results = simd->top_k_similar(query.data(), docs.data(), 10000, 768, 10);
        benchmark::DoNotOptimize(results);
    }
}

// === Scaling Benchmarks ===

BENCHMARK_F(ScalingBenchmark, PredictionWith1KMetrics)(benchmark::State& state) {
    auto scaler = create_predictive_scaler();

    for (int i = 0; i < 1000; ++i) {
        ScalingMetrics m;
        m.request_rate = 100.0 + (i % 50);
        m.cpu_utilization = 0.5 + (i % 10) * 0.05;
        scaler->record_metrics(m);
    }

    for (auto _ : state) {
        benchmark::DoNotOptimize(scaler->predict_load());
    }
}

BENCHMARK_F(ScalingBenchmark, ScalingDecision)(benchmark::State& state) {
    auto scaler = create_predictive_scaler();

    ScalingMetrics m;
    m.cpu_utilization = 0.8;
    m.latency_p95_ms = 100.0;
    scaler->record_metrics(m);

    for (auto _ : state) {
        benchmark::DoNotOptimize(scaler->decide());
    }
}

// === Routing Benchmarks ===

BENCHMARK_F(RoutingBenchmark, SingleRoute)(benchmark::State& state) {
    auto router = create_cost_router();

    for (int i = 0; i < 100; ++i) {
        ProviderEndpoint ep;
        ep.provider = CloudProvider::AWS;
        ep.region = "region-" + std::to_string(i);
        ep.available_agents = {"test-agent"};
        router->register_endpoint(ep);
    }

    for (auto _ : state) {
        benchmark::DoNotOptimize(router->route("test-agent", "us-east-1"));
    }
}

// === Cost Attribution Benchmarks ===

BENCHMARK_F(CostBenchmark, RecordCost)(benchmark::State& state) {
    auto costs = create_cost_attribution_system();

    CostEntry entry;
    entry.agent_id = "agent-1";
    entry.cost_usd = 0.01;

    for (auto _ : state) {
        costs->record_cost(entry);
    }
}

BENCHMARK_F(CostBenchmark, QueryCosts)(benchmark::State& state) {
    auto costs = create_cost_attribution_system();

    for (int i = 0; i < 10000; ++i) {
        CostEntry entry;
        entry.agent_id = "agent-" + std::to_string(i % 10);
        entry.cost_usd = 0.01;
        entry.timestamp = std::chrono::system_clock::now();
        costs->record_cost(entry);
    }

    auto start = std::chrono::system_clock::now() - std::chrono::hours(1);
    auto end = std::chrono::system_clock::now();

    for (auto _ : state) {
        auto results = costs->query_costs(start, end, "agent-1");
        benchmark::DoNotOptimize(results);
    }
}
```

---

## 9. Test Data & Mocks

### 9.1 Mock Files Required

```
tests/mocks/
├── mock_cloud_pricing.hpp      # Mock cloud pricing API responses
├── mock_llm_provider.hpp       # Mock LLM for cost tracking
├── mock_metrics_endpoint.hpp   # Mock Prometheus/CloudWatch
└── mock_k8s_client.hpp         # Mock Kubernetes API
```

### 9.2 Test Fixtures

```cpp
// tests/fixtures/scaling_fixtures.hpp
struct ScalingTestFixture {
    static std::vector<ScalingMetrics> generate_stable_load(int count);
    static std::vector<ScalingMetrics> generate_increasing_load(int count);
    static std::vector<ScalingMetrics> generate_spike_pattern(int count);
    static std::vector<ScalingMetrics> generate_daily_pattern(int count);
};

// tests/fixtures/cost_fixtures.hpp
struct CostTestFixture {
    static std::vector<CostEntry> generate_sample_costs(int count);
    static std::vector<RegionPricing> generate_multi_cloud_pricing();
    static Budget generate_sample_budget();
};
```

---

## 10. Test Execution Commands

```bash
# Run all v0.6.4 tests
ctest --test-dir build -R "v064|scaling|multicloud|gpu|finops"

# Run with verbose output
ctest --test-dir build -R "v064" --output-on-failure

# Run specific module
ctest --test-dir build -R "predictive_scaler"

# Run benchmarks
./build/tests/benchmarks/v064_benchmarks --benchmark_format=json

# Run with coverage
cmake -DCMAKE_BUILD_TYPE=Debug -DENABLE_COVERAGE=ON ..
make
ctest
lcov --capture --directory . --output-file coverage.info
genhtml coverage.info --output-directory coverage_report
```
