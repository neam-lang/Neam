# Neam v0.6.4 Test Specification - Part 2

## 3.2 Warm Pool Manager Tests

**File:** `tests/scaling/warm_pool_test.cpp`
**Test Count:** 38 tests

```cpp
// === Construction ===

TEST(WarmPoolTest, DefaultConstruction) {
    auto pool = create_warm_pool_manager();
    EXPECT_EQ(pool->warm_instance_count(), 0);
    EXPECT_FALSE(pool->is_running());
}

TEST(WarmPoolTest, CustomConfigConstruction) {
    WarmPoolConfig config;
    config.min_warm_instances = 5;
    config.max_warm_instances = 20;
    config.prefer_spot = false;

    auto pool = create_warm_pool_manager(config);
    auto retrieved = pool->get_config();

    EXPECT_EQ(retrieved.min_warm_instances, 5);
    EXPECT_EQ(retrieved.max_warm_instances, 20);
    EXPECT_FALSE(retrieved.prefer_spot);
}

// === Instance Management ===

TEST(WarmPoolTest, EnsureWarmInstances) {
    auto pool = create_warm_pool_manager();

    pool->ensure_warm_instances(3);

    EXPECT_GE(pool->warm_instance_count(), 3);
    EXPECT_EQ(pool->available_instance_count(), 3);
}

TEST(WarmPoolTest, EnsureRespectsMax) {
    WarmPoolConfig config;
    config.max_warm_instances = 5;
    auto pool = create_warm_pool_manager(config);

    pool->ensure_warm_instances(10);

    EXPECT_LE(pool->warm_instance_count(), 5);
}

TEST(WarmPoolTest, AcquireInstance) {
    auto pool = create_warm_pool_manager();
    pool->ensure_warm_instances(3);

    auto instance = pool->acquire_instance();

    EXPECT_TRUE(instance.has_value());
    EXPECT_EQ(instance->state, WarmInstance::State::InUse);
    EXPECT_EQ(pool->available_instance_count(), 2);
    EXPECT_EQ(pool->in_use_instance_count(), 1);
}

TEST(WarmPoolTest, AcquireFromEmptyPool) {
    auto pool = create_warm_pool_manager();

    auto instance = pool->acquire_instance();

    EXPECT_FALSE(instance.has_value());
}

TEST(WarmPoolTest, AcquireWithFilters) {
    auto pool = create_warm_pool_manager();
    pool->ensure_warm_instances(3);

    auto instance = pool->acquire_instance("m5.large", "us-east-1");

    // May or may not find matching instance
    // Just verify no crash
    EXPECT_NO_THROW(pool->available_instance_count());
}

TEST(WarmPoolTest, ReleaseInstance) {
    auto pool = create_warm_pool_manager();
    pool->ensure_warm_instances(3);

    auto instance = pool->acquire_instance();
    ASSERT_TRUE(instance.has_value());

    pool->release_instance(instance->instance_id, true);

    EXPECT_EQ(pool->available_instance_count(), 3);
    EXPECT_EQ(pool->in_use_instance_count(), 0);
}

TEST(WarmPoolTest, ReleaseAndTerminate) {
    auto pool = create_warm_pool_manager();
    pool->ensure_warm_instances(3);

    auto instance = pool->acquire_instance();
    ASSERT_TRUE(instance.has_value());

    pool->release_instance(instance->instance_id, false);  // Don't keep warm

    EXPECT_EQ(pool->available_instance_count(), 2);
}

TEST(WarmPoolTest, ReleaseNonexistentInstance) {
    auto pool = create_warm_pool_manager();

    // Should not crash
    EXPECT_NO_THROW(pool->release_instance("nonexistent-id", true));
}

// === Pre-warming ===

TEST(WarmPoolTest, PrewarmWithAgent) {
    auto pool = create_warm_pool_manager();
    pool->ensure_warm_instances(2);

    pool->prewarm_with_agent("agent-123");

    auto instances = pool->list_available_instances();
    for (const auto& inst : instances) {
        bool found = std::find(inst.preloaded_agents.begin(),
                               inst.preloaded_agents.end(),
                               "agent-123") != inst.preloaded_agents.end();
        EXPECT_TRUE(found);
    }
}

TEST(WarmPoolTest, PrewarmWithMultipleAgents) {
    auto pool = create_warm_pool_manager();
    pool->ensure_warm_instances(2);

    pool->prewarm_with_agents({"agent-1", "agent-2", "agent-3"});

    auto instances = pool->list_available_instances();
    EXPECT_FALSE(instances.empty());
    EXPECT_GE(instances[0].preloaded_agents.size(), 3);
}

// === Health Checks ===

TEST(WarmPoolTest, HealthCheckCallback) {
    auto pool = create_warm_pool_manager();
    pool->ensure_warm_instances(3);

    int health_checks = 0;
    pool->set_health_check_callback([&](const WarmInstance&) {
        health_checks++;
        return true;
    });

    pool->run_health_check();

    EXPECT_EQ(health_checks, 3);
}

TEST(WarmPoolTest, HealthCheckFailure) {
    auto pool = create_warm_pool_manager();
    pool->ensure_warm_instances(3);

    pool->set_health_check_callback([](const WarmInstance& inst) {
        // Fail one instance
        return inst.instance_id != "warm-1";
    });

    pool->run_health_check();

    auto stats = pool->get_stats();
    EXPECT_GT(stats.health_check_failures, 0);
}

// === Cost Tracking ===

TEST(WarmPoolTest, CostCalculation) {
    auto pool = create_warm_pool_manager();
    pool->ensure_warm_instances(3);

    double cost = pool->current_warm_pool_cost_per_hour();

    EXPECT_GE(cost, 0.0);
}

// === Lifecycle ===

TEST(WarmPoolTest, StartStop) {
    auto pool = create_warm_pool_manager();

    pool->start();
    EXPECT_TRUE(pool->is_running());

    pool->stop();
    EXPECT_FALSE(pool->is_running());
}

TEST(WarmPoolTest, DoubleStart) {
    auto pool = create_warm_pool_manager();

    pool->start();
    pool->start();  // Should not crash

    EXPECT_TRUE(pool->is_running());
    pool->stop();
}

TEST(WarmPoolTest, TerminateAll) {
    auto pool = create_warm_pool_manager();
    pool->ensure_warm_instances(5);

    pool->terminate_all();

    EXPECT_EQ(pool->warm_instance_count(), 0);
}

// === Statistics ===

TEST(WarmPoolTest, StatsTracking) {
    auto pool = create_warm_pool_manager();
    pool->ensure_warm_instances(3);

    auto inst1 = pool->acquire_instance();
    auto inst2 = pool->acquire_instance();
    pool->release_instance(inst1->instance_id, true);

    auto stats = pool->get_stats();

    EXPECT_EQ(stats.total_instances_created, 3);
    EXPECT_EQ(stats.total_instances_acquired, 2);
    EXPECT_EQ(stats.total_instances_released, 1);
}

TEST(WarmPoolTest, StatsReset) {
    auto pool = create_warm_pool_manager();
    pool->ensure_warm_instances(2);
    pool->acquire_instance();

    pool->reset_stats();
    auto stats = pool->get_stats();

    EXPECT_EQ(stats.total_instances_acquired, 0);
}

// === Move Semantics ===

TEST(WarmPoolTest, MoveConstruction) {
    auto pool1 = create_warm_pool_manager();
    pool1->ensure_warm_instances(3);

    WarmPoolManager pool2(std::move(*pool1));

    EXPECT_EQ(pool2.warm_instance_count(), 3);
}
```

---

## 3.3 Resource Packer Tests

**File:** `tests/scaling/resource_packer_test.cpp`
**Test Count:** 35 tests

```cpp
// === Node Management ===

TEST(ResourcePackerTest, AddNode) {
    auto packer = create_resource_packer();

    NodeCapacity node;
    node.node_id = "node-1";
    node.cpu_millicores = 4000;
    node.memory_mb = 8192;
    node.hourly_cost = 0.50;

    packer->add_node(node);

    auto retrieved = packer->get_node("node-1");
    EXPECT_TRUE(retrieved.has_value());
    EXPECT_EQ(retrieved->cpu_millicores, 4000);
}

TEST(ResourcePackerTest, RemoveNode) {
    auto packer = create_resource_packer();

    NodeCapacity node;
    node.node_id = "node-1";
    packer->add_node(node);

    packer->remove_node("node-1");

    EXPECT_FALSE(packer->get_node("node-1").has_value());
}

TEST(ResourcePackerTest, ClusterCapacity) {
    auto packer = create_resource_packer();

    for (int i = 0; i < 5; ++i) {
        NodeCapacity node;
        node.node_id = "node-" + std::to_string(i);
        node.cpu_millicores = 4000;
        node.memory_mb = 8192;
        packer->add_node(node);
    }

    auto capacity = packer->get_cluster_capacity();

    EXPECT_EQ(capacity.total_nodes, 5);
    EXPECT_EQ(capacity.total_cpu_millicores, 20000);
    EXPECT_EQ(capacity.total_memory_mb, 40960);
}

// === Workload Placement ===

TEST(ResourcePackerTest, PlaceSingleWorkload) {
    auto packer = create_resource_packer();

    NodeCapacity node;
    node.node_id = "node-1";
    node.cpu_millicores = 4000;
    node.memory_mb = 8192;
    packer->add_node(node);

    ResourceRequirements workload;
    workload.workload_id = "workload-1";
    workload.cpu_request_millicores = 1000;
    workload.memory_request_mb = 2048;

    auto decision = packer->place_workload(workload);

    EXPECT_TRUE(decision.placed);
    EXPECT_EQ(decision.node_id, "node-1");
}

TEST(ResourcePackerTest, PlaceExceedsCapacity) {
    auto packer = create_resource_packer();

    NodeCapacity node;
    node.node_id = "node-1";
    node.cpu_millicores = 1000;
    node.memory_mb = 1024;
    packer->add_node(node);

    ResourceRequirements workload;
    workload.workload_id = "workload-1";
    workload.cpu_request_millicores = 2000;  // Exceeds
    workload.memory_request_mb = 512;

    auto decision = packer->place_workload(workload);

    EXPECT_FALSE(decision.placed);
}

TEST(ResourcePackerTest, PackMultipleWorkloads) {
    auto packer = create_resource_packer();

    // Add nodes
    for (int i = 0; i < 3; ++i) {
        NodeCapacity node;
        node.node_id = "node-" + std::to_string(i);
        node.cpu_millicores = 4000;
        node.memory_mb = 8192;
        packer->add_node(node);
    }

    // Add workloads
    for (int i = 0; i < 5; ++i) {
        ResourceRequirements w;
        w.workload_id = "workload-" + std::to_string(i);
        w.cpu_request_millicores = 1000;
        w.memory_request_mb = 2048;
        packer->add_workload(w);
    }

    auto result = packer->pack();

    EXPECT_TRUE(result.success);
    EXPECT_EQ(result.workloads_placed, 5);
    EXPECT_EQ(result.workloads_unplaced, 0);
}

TEST(ResourcePackerTest, PackWithAffinity) {
    auto packer = create_resource_packer();

    NodeCapacity node1;
    node1.node_id = "node-1";
    node1.cpu_millicores = 4000;
    node1.labels = {"gpu=true"};
    packer->add_node(node1);

    NodeCapacity node2;
    node2.node_id = "node-2";
    node2.cpu_millicores = 4000;
    packer->add_node(node2);

    ResourceRequirements workload;
    workload.workload_id = "gpu-workload";
    workload.cpu_request_millicores = 1000;
    workload.node_affinity_labels = {"gpu=true"};

    auto decision = packer->place_workload(workload);

    EXPECT_TRUE(decision.placed);
    EXPECT_EQ(decision.node_id, "node-1");
}

TEST(ResourcePackerTest, PackWithAntiAffinity) {
    auto packer = create_resource_packer();

    NodeCapacity node;
    node.node_id = "node-1";
    node.cpu_millicores = 8000;
    node.memory_mb = 16384;
    packer->add_node(node);

    ResourceRequirements w1;
    w1.workload_id = "workload-1";
    w1.cpu_request_millicores = 1000;
    packer->add_workload(w1);

    ResourceRequirements w2;
    w2.workload_id = "workload-2";
    w2.cpu_request_millicores = 1000;
    w2.anti_affinity_workloads = {"workload-1"};
    packer->add_workload(w2);

    auto result = packer->pack();

    // With only one node, anti-affinity may cause unplaced workloads
    EXPECT_LE(result.workloads_placed, 2);
}

// === Cost Optimization ===

TEST(ResourcePackerTest, CostOptimizedPacking) {
    PackingConfig config;
    config.algorithm = PackingAlgorithm::CostOptimized;
    auto packer = create_resource_packer(config);

    NodeCapacity cheap;
    cheap.node_id = "cheap-node";
    cheap.cpu_millicores = 4000;
    cheap.memory_mb = 8192;
    cheap.hourly_cost = 0.10;
    packer->add_node(cheap);

    NodeCapacity expensive;
    expensive.node_id = "expensive-node";
    expensive.cpu_millicores = 4000;
    expensive.memory_mb = 8192;
    expensive.hourly_cost = 1.00;
    packer->add_node(expensive);

    ResourceRequirements workload;
    workload.workload_id = "workload-1";
    workload.cpu_request_millicores = 1000;

    auto decision = packer->place_workload(workload);

    EXPECT_TRUE(decision.placed);
    EXPECT_EQ(decision.node_id, "cheap-node");
}

// === Repack & Compact ===

TEST(ResourcePackerTest, Repack) {
    auto packer = create_resource_packer();

    for (int i = 0; i < 3; ++i) {
        NodeCapacity node;
        node.node_id = "node-" + std::to_string(i);
        node.cpu_millicores = 4000;
        packer->add_node(node);
    }

    for (int i = 0; i < 5; ++i) {
        ResourceRequirements w;
        w.workload_id = "w-" + std::to_string(i);
        w.cpu_request_millicores = 500;
        packer->add_workload(w);
    }

    packer->pack();

    auto result = packer->repack();

    EXPECT_TRUE(result.success);
}

TEST(ResourcePackerTest, Compact) {
    auto packer = create_resource_packer();

    for (int i = 0; i < 5; ++i) {
        NodeCapacity node;
        node.node_id = "node-" + std::to_string(i);
        node.cpu_millicores = 4000;
        packer->add_node(node);
    }

    // Small workloads spread across nodes
    for (int i = 0; i < 5; ++i) {
        ResourceRequirements w;
        w.workload_id = "w-" + std::to_string(i);
        w.cpu_request_millicores = 500;
        packer->add_workload(w);
    }

    packer->pack();
    auto result = packer->compact();

    // Should consolidate to fewer nodes
    EXPECT_LT(result.nodes_used, 5);
}

// === Scoring ===

TEST(ResourcePackerTest, PlacementScoring) {
    auto packer = create_resource_packer();

    NodeCapacity node;
    node.cpu_millicores = 4000;
    node.memory_mb = 8192;
    node.hourly_cost = 0.50;

    ResourceRequirements workload;
    workload.cpu_request_millicores = 1000;
    workload.memory_request_mb = 2048;

    double score = packer->score_placement(workload, node);

    EXPECT_GT(score, 0.0);
}
```

---

## 4. Module 2: Multi-Cloud Orchestration

### 4.1 Cost Router Tests

**File:** `tests/multicloud/cost_router_test.cpp`
**Test Count:** 42 tests

```cpp
// === Endpoint Management ===

TEST(CostRouterTest, RegisterEndpoint) {
    auto router = create_cost_router();

    ProviderEndpoint endpoint;
    endpoint.provider = CloudProvider::AWS;
    endpoint.region = "us-east-1";
    endpoint.supports_spot = true;
    endpoint.available_agents = {"agent-1", "agent-2"};

    router->register_endpoint(endpoint);

    auto endpoints = router->list_endpoints();
    EXPECT_EQ(endpoints.size(), 1);
    EXPECT_EQ(endpoints[0].region, "us-east-1");
}

TEST(CostRouterTest, RemoveEndpoint) {
    auto router = create_cost_router();

    ProviderEndpoint endpoint;
    endpoint.provider = CloudProvider::GCP;
    endpoint.region = "us-central1";
    router->register_endpoint(endpoint);

    router->remove_endpoint(CloudProvider::GCP, "us-central1");

    EXPECT_EQ(router->list_endpoints().size(), 0);
}

TEST(CostRouterTest, ListEndpointsByProvider) {
    auto router = create_cost_router();

    ProviderEndpoint aws1, aws2, gcp1;
    aws1.provider = CloudProvider::AWS;
    aws1.region = "us-east-1";
    aws2.provider = CloudProvider::AWS;
    aws2.region = "us-west-2";
    gcp1.provider = CloudProvider::GCP;
    gcp1.region = "us-central1";

    router->register_endpoint(aws1);
    router->register_endpoint(aws2);
    router->register_endpoint(gcp1);

    auto aws_endpoints = router->list_endpoints(CloudProvider::AWS);
    EXPECT_EQ(aws_endpoints.size(), 2);
}

// === Pricing ===

TEST(CostRouterTest, UpdatePricing) {
    auto router = create_cost_router();

    RegionPricing pricing;
    pricing.provider = CloudProvider::AWS;
    pricing.region = "us-east-1";
    pricing.spot_price_per_hour = 0.05;
    pricing.ondemand_price_per_hour = 0.20;
    pricing.spot_availability = 0.95;

    router->update_pricing(pricing);

    auto retrieved = router->get_pricing(CloudProvider::AWS, "us-east-1");
    EXPECT_TRUE(retrieved.has_value());
    EXPECT_DOUBLE_EQ(retrieved->spot_price_per_hour, 0.05);
}

TEST(CostRouterTest, UpdatePricingBatch) {
    auto router = create_cost_router();

    std::vector<RegionPricing> batch;
    for (int i = 0; i < 10; ++i) {
        RegionPricing p;
        p.provider = CloudProvider::AWS;
        p.region = "region-" + std::to_string(i);
        p.spot_price_per_hour = 0.05 + i * 0.01;
        batch.push_back(p);
    }

    router->update_pricing_batch(batch);

    auto p5 = router->get_pricing(CloudProvider::AWS, "region-5");
    EXPECT_TRUE(p5.has_value());
}

// === Routing ===

TEST(CostRouterTest, RouteBasic) {
    auto router = create_cost_router();

    ProviderEndpoint endpoint;
    endpoint.provider = CloudProvider::AWS;
    endpoint.region = "us-east-1";
    endpoint.available_agents = {"test-agent"};
    router->register_endpoint(endpoint);

    RegionPricing pricing;
    pricing.provider = CloudProvider::AWS;
    pricing.region = "us-east-1";
    pricing.spot_price_per_hour = 0.05;
    router->update_pricing(pricing);

    auto decision = router->route("test-agent", "us-east-1");

    EXPECT_EQ(decision.target, RoutingDecision::Target::Remote);
    EXPECT_EQ(decision.provider, CloudProvider::AWS);
}

TEST(CostRouterTest, RouteNoMatchingAgent) {
    auto router = create_cost_router();

    ProviderEndpoint endpoint;
    endpoint.provider = CloudProvider::AWS;
    endpoint.region = "us-east-1";
    endpoint.available_agents = {"other-agent"};
    router->register_endpoint(endpoint);

    auto decision = router->route("test-agent", "us-east-1");

    EXPECT_EQ(decision.target, RoutingDecision::Target::Rejected);
}

TEST(CostRouterTest, RouteWithConstraints) {
    auto router = create_cost_router();

    // AWS endpoint
    ProviderEndpoint aws;
    aws.provider = CloudProvider::AWS;
    aws.region = "us-east-1";
    aws.available_agents = {"test-agent"};
    router->register_endpoint(aws);

    // GCP endpoint
    ProviderEndpoint gcp;
    gcp.provider = CloudProvider::GCP;
    gcp.region = "us-central1";
    gcp.available_agents = {"test-agent"};
    router->register_endpoint(gcp);

    // Route with AWS excluded
    RoutingConstraints constraints;
    constraints.excluded_providers = {CloudProvider::AWS};

    auto decision = router->route("test-agent", "us-east-1", constraints);

    EXPECT_EQ(decision.provider, CloudProvider::GCP);
}

TEST(CostRouterTest, RouteWithLatencyConstraint) {
    auto router = create_cost_router();

    ProviderEndpoint local;
    local.provider = CloudProvider::AWS;
    local.region = "us-east-1";
    local.available_agents = {"test-agent"};
    router->register_endpoint(local);

    ProviderEndpoint remote;
    remote.provider = CloudProvider::AWS;
    remote.region = "eu-west-1";  // Cross-continent
    remote.available_agents = {"test-agent"};
    router->register_endpoint(remote);

    RoutingConstraints constraints;
    constraints.max_latency_ms = 50.0;  // Strict latency

    auto decision = router->route("test-agent", "us-east-1", constraints);

    // Should prefer local region
    EXPECT_EQ(decision.region, "us-east-1");
}

TEST(CostRouterTest, RouteWithDataResidency) {
    auto router = create_cost_router();

    ProviderEndpoint us;
    us.provider = CloudProvider::AWS;
    us.region = "us-east-1";
    us.available_agents = {"test-agent"};
    router->register_endpoint(us);

    ProviderEndpoint eu;
    eu.provider = CloudProvider::AWS;
    eu.region = "eu-west-1";
    eu.available_agents = {"test-agent"};
    router->register_endpoint(eu);

    RoutingConstraints constraints;
    constraints.data_residency_requirement = "EU";

    auto decision = router->route("test-agent", "us-east-1", constraints);

    EXPECT_EQ(decision.region, "eu-west-1");
}

TEST(CostRouterTest, RouteBatch) {
    auto router = create_cost_router();

    ProviderEndpoint endpoint;
    endpoint.provider = CloudProvider::AWS;
    endpoint.region = "us-east-1";
    endpoint.available_agents = {"agent-1", "agent-2", "agent-3"};
    router->register_endpoint(endpoint);

    auto decisions = router->route_batch(
        {"agent-1", "agent-2", "agent-3"},
        "us-east-1"
    );

    EXPECT_EQ(decisions.size(), 3);
}

// === Cost Comparison ===

TEST(CostRouterTest, CompareCosts) {
    auto router = create_cost_router();

    RegionPricing aws, gcp, azure;
    aws.provider = CloudProvider::AWS;
    aws.region = "us-east-1";
    aws.ondemand_price_per_hour = 0.20;

    gcp.provider = CloudProvider::GCP;
    gcp.region = "us-central1";
    gcp.ondemand_price_per_hour = 0.15;

    azure.provider = CloudProvider::Azure;
    azure.region = "eastus";
    azure.ondemand_price_per_hour = 0.25;

    router->update_pricing(aws);
    router->update_pricing(gcp);
    router->update_pricing(azure);

    auto comparisons = router->compare_costs("test-agent", CloudProvider::AWS);

    EXPECT_EQ(comparisons.size(), 3);
    EXPECT_EQ(comparisons[0].ranking, 1);
    EXPECT_EQ(comparisons[0].provider, CloudProvider::GCP);  // Cheapest
}

TEST(CostRouterTest, GetCheapest) {
    auto router = create_cost_router();

    ProviderEndpoint cheap;
    cheap.provider = CloudProvider::GCP;
    cheap.region = "us-central1";
    cheap.available_agents = {"test-agent"};
    router->register_endpoint(cheap);

    RegionPricing pricing;
    pricing.provider = CloudProvider::GCP;
    pricing.region = "us-central1";
    pricing.ondemand_price_per_hour = 0.10;
    router->update_pricing(pricing);

    auto cheapest = router->get_cheapest("test-agent");

    EXPECT_TRUE(cheapest.has_value());
    EXPECT_EQ(cheapest->provider, CloudProvider::GCP);
}

// === Health Management ===

TEST(CostRouterTest, HealthCheckCallback) {
    auto router = create_cost_router();

    ProviderEndpoint endpoint;
    endpoint.provider = CloudProvider::AWS;
    endpoint.region = "us-east-1";
    router->register_endpoint(endpoint);

    bool callback_invoked = false;
    router->set_health_check_callback([&](const ProviderEndpoint&) {
        callback_invoked = true;
        return true;
    });

    router->run_health_check();

    EXPECT_TRUE(callback_invoked);
}

TEST(CostRouterTest, MarkEndpointUnhealthy) {
    auto router = create_cost_router();

    ProviderEndpoint endpoint;
    endpoint.provider = CloudProvider::AWS;
    endpoint.region = "us-east-1";
    endpoint.available_agents = {"test-agent"};
    router->register_endpoint(endpoint);

    router->mark_endpoint_unhealthy(CloudProvider::AWS, "us-east-1");

    auto decision = router->route("test-agent", "us-west-2");

    EXPECT_EQ(decision.target, RoutingDecision::Target::Rejected);
}

// === Statistics ===

TEST(CostRouterTest, StatsTracking) {
    auto router = create_cost_router();

    ProviderEndpoint endpoint;
    endpoint.provider = CloudProvider::AWS;
    endpoint.region = "us-east-1";
    endpoint.available_agents = {"test-agent"};
    router->register_endpoint(endpoint);

    for (int i = 0; i < 10; ++i) {
        router->route("test-agent", "us-east-1");
    }

    auto stats = router->get_stats();

    EXPECT_EQ(stats.total_routing_decisions, 10);
}
```
