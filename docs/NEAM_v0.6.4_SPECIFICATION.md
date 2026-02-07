# Neam v0.6.4 Technical Specification

## Cost Efficiency Optimizations for Enterprise-Scale Agent Deployments

**Version:** 0.6.4
**Status:** Planning
**Target Release:** Q3 2026
**Prerequisites:** v0.6.3 (Phase 1 features complete)

---

## Table of Contents

1. [Executive Summary](#executive-summary)
2. [Architecture Overview](#architecture-overview)
3. [Feature Specifications](#feature-specifications)
   - [F1: Intelligent Auto-Scaling Module](#f1-intelligent-auto-scaling-module)
   - [F2: Multi-Cloud Orchestration & Routing](#f2-multi-cloud-orchestration--routing)
   - [F3: GPU & Vectorized Operations](#f3-gpu--vectorized-operations)
   - [F4: FinOps Dashboard & Benchmarking](#f4-finops-dashboard--benchmarking)
   - [F5: Module System & Guardrails](#f5-module-system--guardrails)
4. [Implementation Plan](#implementation-plan)
5. [API Reference](#api-reference)
6. [Migration Guide](#migration-guide)
7. [Testing Strategy](#testing-strategy)
8. [Risk Assessment](#risk-assessment)

---

## Executive Summary

### Vision

Neam v0.6.4 transforms the platform from a cost-efficient agent runtime into an **intelligent cost optimization system** that actively guides users to deploy and operate agents at the lowest possible cost while maintaining required performance levels.

### Key Objectives

| Objective | Target | Metric |
|-----------|--------|--------|
| Reduce over-provisioning | 20% additional savings | Infrastructure cost per invocation |
| Enable multi-cloud arbitrage | 15-30% cost reduction | Cross-cloud cost optimization |
| GPU utilization efficiency | 3-5× throughput improvement | Frames/tokens per GPU-hour |
| Cost visibility | Real-time attribution | Cost per agent, per skill, per invocation |
| Developer productivity | 50% faster optimization cycles | Time to identify cost hotspots |

### Cost Savings Summary

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    Cumulative Cost Savings by Version                        │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  Baseline (Python/LangChain)                    ████████████████████ 100%   │
│                                                                              │
│  Neam v0.6.2 (Optimized Runtime)                ██░░░░░░░░░░░░░░░░░░  10-15%│
│  └─ 85-90% infrastructure savings                                           │
│                                                                              │
│  Neam v0.6.3 (Spot + Budget Enforcement)        █░░░░░░░░░░░░░░░░░░░   5-10%│
│  └─ +70% compute savings (spot), +20% LLM savings (budgets)                 │
│                                                                              │
│  Neam v0.6.4 (Intelligent Optimization)         ▌░░░░░░░░░░░░░░░░░░░   3-7% │
│  └─ +20% scaling efficiency, +15-30% multi-cloud arbitrage                  │
│                                                                              │
│  Total v0.6.4 vs Baseline:                      ▌░░░░░░░░░░░░░░░░░░░   2-5% │
│  └─ 95-98% total cost reduction possible                                    │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## Architecture Overview

### System Architecture

```
┌─────────────────────────────────────────────────────────────────────────────────┐
│                           Neam v0.6.4 Architecture                               │
├─────────────────────────────────────────────────────────────────────────────────┤
│                                                                                  │
│  ┌─────────────────────────────────────────────────────────────────────────┐   │
│  │                         FinOps Control Plane                             │   │
│  ├─────────────────────────────────────────────────────────────────────────┤   │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  ┌─────────────┐ │   │
│  │  │    Cost      │  │   Budget     │  │  Benchmark   │  │ Recommend-  │ │   │
│  │  │  Dashboard   │  │   Engine     │  │   Runner     │  │ ation Engine│ │   │
│  │  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘  └──────┬──────┘ │   │
│  │         └──────────────────┼──────────────────┼──────────────────┘       │   │
│  │                            │                  │                          │   │
│  │                            ▼                  ▼                          │   │
│  │                    ┌──────────────────────────────┐                     │   │
│  │                    │      Metrics Aggregator      │                     │   │
│  │                    └──────────────────────────────┘                     │   │
│  └─────────────────────────────────┬───────────────────────────────────────┘   │
│                                    │                                           │
│  ┌─────────────────────────────────┼───────────────────────────────────────┐   │
│  │                    Intelligent Scaling Layer                             │   │
│  ├─────────────────────────────────┴───────────────────────────────────────┤   │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  ┌─────────────┐ │   │
│  │  │  Predictive  │  │    Warm      │  │   Resource   │  │   Scaling   │ │   │
│  │  │   Scaler     │  │    Pool      │  │   Packer     │  │   Advisor   │ │   │
│  │  └──────────────┘  └──────────────┘  └──────────────┘  └─────────────┘ │   │
│  └─────────────────────────────────┬───────────────────────────────────────┘   │
│                                    │                                           │
│  ┌─────────────────────────────────┼───────────────────────────────────────┐   │
│  │                    Multi-Cloud Orchestration                             │   │
│  ├─────────────────────────────────┴───────────────────────────────────────┤   │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  ┌─────────────┐ │   │
│  │  │   Cost-Aware │  │  Cross-Cloud │  │   Provider   │  │   Spot      │ │   │
│  │  │    Router    │  │  Federation  │  │   Registry   │  │  Arbitrage  │ │   │
│  │  └──────────────┘  └──────────────┘  └──────────────┘  └─────────────┘ │   │
│  └─────────────────────────────────┬───────────────────────────────────────┘   │
│                                    │                                           │
│  ┌─────────────────────────────────┼───────────────────────────────────────┐   │
│  │                    Accelerated Runtime                                   │   │
│  ├─────────────────────────────────┴───────────────────────────────────────┤   │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐  ┌─────────────┐ │   │
│  │  │   GPU/SIMD   │  │    JIT       │  │    Arena     │  │   Batch     │ │   │
│  │  │   Executor   │  │   Compiler   │  │   Allocator  │  │  Processor  │ │   │
│  │  └──────────────┘  └──────────────┘  └──────────────┘  └─────────────┘ │   │
│  └─────────────────────────────────────────────────────────────────────────┘   │
│                                                                                  │
│  ┌─────────────────────────────────────────────────────────────────────────┐   │
│  │                    Cloud Providers                                       │   │
│  ├─────────────────────────────────────────────────────────────────────────┤   │
│  │  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────┐  ┌─────────────┐  │   │
│  │  │   AWS   │  │   GCP   │  │  Azure  │  │ Alibaba │  │  On-Prem    │  │   │
│  │  └─────────┘  └─────────┘  └─────────┘  └─────────┘  └─────────────┘  │   │
│  └─────────────────────────────────────────────────────────────────────────┘   │
│                                                                                  │
└─────────────────────────────────────────────────────────────────────────────────┘
```

### Component Interactions

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                         Request Flow with Cost Optimization                  │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  1. Incoming Request                                                        │
│     │                                                                       │
│     ▼                                                                       │
│  ┌──────────────────┐                                                       │
│  │ Cost-Aware Router│──── Check: Which cloud/region is cheapest?           │
│  └────────┬─────────┘     Consider: spot availability, latency, data loc   │
│           │                                                                 │
│           ▼                                                                 │
│  2. ┌──────────────────┐                                                   │
│     │ Predictive Scaler│──── Check: Do we need to scale up/down?           │
│     └────────┬─────────┘     Consider: traffic patterns, warm pool status  │
│              │                                                              │
│              ▼                                                              │
│  3. ┌──────────────────┐                                                   │
│     │ Resource Packer  │──── Optimize: Pack agents efficiently              │
│     └────────┬─────────┘     Consider: memory, CPU, GPU sharing            │
│              │                                                              │
│              ▼                                                              │
│  4. ┌──────────────────┐                                                   │
│     │ Budget Engine    │──── Check: Within budget? Model selection?        │
│     └────────┬─────────┘     Action: Throttle, downgrade, or proceed       │
│              │                                                              │
│              ▼                                                              │
│  5. ┌──────────────────┐                                                   │
│     │ GPU/SIMD Executor│──── Accelerate: Use hardware efficiently          │
│     └────────┬─────────┘     Batch: Combine operations where possible      │
│              │                                                              │
│              ▼                                                              │
│  6. ┌──────────────────┐                                                   │
│     │ Metrics Collector│──── Record: Cost, latency, resource usage         │
│     └────────┬─────────┘     Export: OTLP, CSV, Dashboard                  │
│              │                                                              │
│              ▼                                                              │
│  7. Response + Cost Attribution                                             │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## Feature Specifications

---

## F1: Intelligent Auto-Scaling Module

### Overview

A Neam-aware auto-scaler that interfaces with Kubernetes and cloud APIs to scale agent deployments optimally using predictive algorithms and workload-specific knowledge.

### Goals

- Achieve near-zero idle cost
- Handle traffic bursts without latency spikes
- Provide scaling recommendations
- Support multi-tenancy with fair resource allocation

### Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                      Intelligent Auto-Scaling Module                         │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  ┌────────────────────────────────────────────────────────────────────┐    │
│  │                        Metrics Ingestion                           │    │
│  ├────────────────────────────────────────────────────────────────────┤    │
│  │  • Request rate (RPS per agent)                                    │    │
│  │  • Queue depth (pending tasks)                                     │    │
│  │  • Latency percentiles (p50, p95, p99)                            │    │
│  │  • Resource utilization (CPU, Memory, GPU)                         │    │
│  │  • Cost accumulation rate                                          │    │
│  └────────────────────────────────────────────────────────────────────┘    │
│                              │                                              │
│                              ▼                                              │
│  ┌────────────────────────────────────────────────────────────────────┐    │
│  │                     Prediction Engine                               │    │
│  ├────────────────────────────────────────────────────────────────────┤    │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐             │    │
│  │  │   Time-Series│  │   Pattern    │  │   Anomaly    │             │    │
│  │  │   Forecaster │  │   Detector   │  │   Detector   │             │    │
│  │  └──────────────┘  └──────────────┘  └──────────────┘             │    │
│  │         │                 │                 │                      │    │
│  │         └─────────────────┼─────────────────┘                      │    │
│  │                           ▼                                        │    │
│  │                  ┌──────────────────┐                             │    │
│  │                  │  Load Prediction │                             │    │
│  │                  │   (5min, 1hr)    │                             │    │
│  │                  └──────────────────┘                             │    │
│  └────────────────────────────────────────────────────────────────────┘    │
│                              │                                              │
│                              ▼                                              │
│  ┌────────────────────────────────────────────────────────────────────┐    │
│  │                      Scaling Decision Engine                        │    │
│  ├────────────────────────────────────────────────────────────────────┤    │
│  │                                                                     │    │
│  │  Inputs:                    Outputs:                               │    │
│  │  • Predicted load           • Scale up/down decision               │    │
│  │  • Current capacity         • Target replica count                 │    │
│  │  • Cost constraints         • Instance type recommendation         │    │
│  │  • Latency SLOs             • Warm pool adjustment                 │    │
│  │  • Spot availability        • FaaS vs Container recommendation    │    │
│  │                                                                     │    │
│  └────────────────────────────────────────────────────────────────────┘    │
│                              │                                              │
│                              ▼                                              │
│  ┌────────────────────────────────────────────────────────────────────┐    │
│  │                       Execution Layer                               │    │
│  ├────────────────────────────────────────────────────────────────────┤    │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐             │    │
│  │  │  Kubernetes  │  │  Cloud APIs  │  │   Warm Pool  │             │    │
│  │  │    HPA/VPA   │  │  (ASG, MIG)  │  │   Manager    │             │    │
│  │  └──────────────┘  └──────────────┘  └──────────────┘             │    │
│  └────────────────────────────────────────────────────────────────────┘    │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Technical Specification

#### 1. Predictive Scaler

```cpp
// NeamC/include/neamc/scaling/predictive_scaler.hpp

namespace neamc::scaling {

struct ScalingMetrics {
    double request_rate;           // Requests per second
    double queue_depth;            // Pending tasks
    double latency_p50_ms;
    double latency_p95_ms;
    double latency_p99_ms;
    double cpu_utilization;        // 0.0 - 1.0
    double memory_utilization;     // 0.0 - 1.0
    double gpu_utilization;        // 0.0 - 1.0 (if applicable)
    double cost_rate;              // $/hour current spend
    std::chrono::system_clock::time_point timestamp;
};

struct ScalingPolicy {
    double target_cpu_utilization = 0.70;
    double target_memory_utilization = 0.80;
    double target_latency_p95_ms = 100.0;
    double max_cost_per_hour = 100.0;
    int min_replicas = 1;
    int max_replicas = 100;
    int warm_pool_size = 2;
    std::chrono::seconds scale_up_cooldown{60};
    std::chrono::seconds scale_down_cooldown{300};
    bool prefer_spot_instances = true;
    double spot_fallback_threshold = 0.3;  // Fallback if <30% spot available
};

struct ScalingDecision {
    enum class Action {
        NoChange,
        ScaleUp,
        ScaleDown,
        SwitchToSpot,
        SwitchToOnDemand,
        SwitchToFaaS,
        SwitchToContainers
    };

    Action action;
    int target_replicas;
    std::string instance_type;
    std::string reasoning;
    double estimated_cost_change;  // Positive = more expensive
    double estimated_latency_change;
};

struct LoadPrediction {
    double predicted_rps_5min;
    double predicted_rps_1hr;
    double confidence;  // 0.0 - 1.0
    std::vector<double> hourly_forecast_24h;
};

class PredictiveScaler {
public:
    explicit PredictiveScaler(ScalingPolicy policy);

    // Ingest metrics
    void record_metrics(const ScalingMetrics& metrics);

    // Get current prediction
    LoadPrediction predict_load() const;

    // Get scaling decision
    ScalingDecision decide() const;

    // Apply decision (interface with K8s/Cloud APIs)
    void apply_decision(const ScalingDecision& decision);

    // Get recommendations
    std::vector<std::string> get_recommendations() const;

    // Configuration
    void set_policy(ScalingPolicy policy);
    ScalingPolicy get_policy() const;

    // Statistics
    struct Stats {
        int scale_up_events;
        int scale_down_events;
        double avg_prediction_accuracy;
        double cost_savings_from_scaling;
    };
    Stats get_stats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace neamc::scaling
```

#### 2. Warm Pool Manager

```cpp
// NeamC/include/neamc/scaling/warm_pool.hpp

namespace neamc::scaling {

struct WarmInstance {
    std::string instance_id;
    std::string instance_type;
    std::string cloud_provider;
    std::string region;
    std::chrono::system_clock::time_point warmed_at;
    bool is_spot;
    double hourly_cost;
};

struct WarmPoolConfig {
    int min_warm_instances = 2;
    int max_warm_instances = 10;
    std::chrono::seconds warm_timeout{300};  // How long to keep warm
    std::vector<std::string> preferred_instance_types;
    std::vector<std::string> preferred_regions;
    double max_warm_pool_cost_per_hour = 10.0;
};

class WarmPoolManager {
public:
    explicit WarmPoolManager(WarmPoolConfig config);

    // Pool management
    void ensure_warm_instances(int count);
    WarmInstance acquire_instance();
    void release_instance(const std::string& instance_id);

    // Pre-warming with agent code
    void prewarm_with_agent(const std::string& agent_id);

    // Statistics
    int warm_instance_count() const;
    double current_warm_pool_cost() const;
    std::vector<WarmInstance> list_instances() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace neamc::scaling
```

#### 3. Resource Packer

```cpp
// NeamC/include/neamc/scaling/resource_packer.hpp

namespace neamc::scaling {

struct AgentResourceProfile {
    std::string agent_id;
    double avg_cpu_cores;
    double peak_cpu_cores;
    double avg_memory_mb;
    double peak_memory_mb;
    double avg_gpu_memory_mb;  // 0 if no GPU
    double avg_invocation_duration_ms;
    double requests_per_second;
};

struct PackingPlan {
    struct InstanceAllocation {
        std::string instance_type;
        std::vector<std::string> agent_ids;
        double utilization_score;  // 0.0 - 1.0
        double estimated_cost_per_hour;
    };

    std::vector<InstanceAllocation> allocations;
    double total_cost_per_hour;
    double avg_utilization;
};

class ResourcePacker {
public:
    // Compute optimal packing
    PackingPlan compute_packing(
        const std::vector<AgentResourceProfile>& agents,
        const std::vector<std::string>& available_instance_types
    );

    // Check if agents can be co-located
    bool can_colocate(
        const AgentResourceProfile& a,
        const AgentResourceProfile& b
    ) const;

    // Get packing recommendations
    std::vector<std::string> get_recommendations(
        const std::vector<AgentResourceProfile>& current_deployment
    ) const;
};

}  // namespace neamc::scaling
```

### DSL Extensions

```neam
// Agent with scaling hints
agent high_traffic_agent {
    name = "High Traffic Handler"
    model = "gpt-4"

    // Scaling configuration
    scaling {
        min_replicas = 2
        max_replicas = 50
        target_cpu = 0.7
        target_latency_p95 = 100ms

        // Warm pool
        warm_pool = 3
        prewarm_on_deploy = true

        // Cost constraints
        max_cost_per_hour = $50
        prefer_spot = true

        // Burst handling
        burst_capacity = 10x  // Can scale to 10x normal for bursts
        scale_up_cooldown = 30s
        scale_down_cooldown = 5min
    }
}

// Auto-scaling group definition
scaling_group text_agents {
    agents = [faq_agent, support_agent, triage_agent]

    policy {
        shared_resources = true  // Pack together
        min_total_replicas = 3
        max_total_replicas = 100

        time_based_scaling {
            schedule "weekday_peak" {
                cron = "0 9-17 * * 1-5"  // 9am-5pm weekdays
                min_replicas = 10
            }
            schedule "night" {
                cron = "0 0-8 * * *"  // Midnight to 8am
                min_replicas = 1
            }
        }
    }
}
```

### Kubernetes Integration

```yaml
# neam-autoscaler-deployment.yaml
apiVersion: apps/v1
kind: Deployment
metadata:
  name: neam-autoscaler
spec:
  replicas: 1
  selector:
    matchLabels:
      app: neam-autoscaler
  template:
    metadata:
      labels:
        app: neam-autoscaler
    spec:
      serviceAccountName: neam-autoscaler
      containers:
      - name: autoscaler
        image: neam/autoscaler:0.6.4
        args:
        - --config=/etc/neam/scaling-config.yaml
        - --metrics-addr=:8080
        - --enable-predictions
        - --warm-pool-enabled
        env:
        - name: CLOUD_PROVIDER
          value: "aws"  # or gcp, azure, alibaba
        volumeMounts:
        - name: config
          mountPath: /etc/neam
      volumes:
      - name: config
        configMap:
          name: neam-scaling-config
---
# Custom Resource Definition for Neam scaling
apiVersion: apiextensions.k8s.io/v1
kind: CustomResourceDefinition
metadata:
  name: neamscalers.neam.io
spec:
  group: neam.io
  names:
    kind: NeamScaler
    plural: neamscalers
    singular: neamscaler
  scope: Namespaced
  versions:
  - name: v1
    served: true
    storage: true
    schema:
      openAPIV3Schema:
        type: object
        properties:
          spec:
            type: object
            properties:
              targetDeployment:
                type: string
              minReplicas:
                type: integer
              maxReplicas:
                type: integer
              targetCPUUtilization:
                type: number
              targetLatencyP95Ms:
                type: number
              warmPoolSize:
                type: integer
              preferSpot:
                type: boolean
              maxCostPerHour:
                type: number
---
# Example NeamScaler resource
apiVersion: neam.io/v1
kind: NeamScaler
metadata:
  name: text-agents-scaler
spec:
  targetDeployment: text-agents
  minReplicas: 2
  maxReplicas: 50
  targetCPUUtilization: 0.7
  targetLatencyP95Ms: 100
  warmPoolSize: 3
  preferSpot: true
  maxCostPerHour: 50
```

---

## F2: Multi-Cloud Orchestration & Routing

### Overview

Extend Neam's A2A federation to enable cost-aware routing across multiple cloud providers and regions, dynamically selecting the cheapest execution environment for each agent invocation.

### Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                     Multi-Cloud Orchestration Layer                          │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  ┌────────────────────────────────────────────────────────────────────┐    │
│  │                      Cost-Aware Router                              │    │
│  ├────────────────────────────────────────────────────────────────────┤    │
│  │                                                                     │    │
│  │  Routing Factors:                                                   │    │
│  │  ┌─────────────┐ ┌─────────────┐ ┌─────────────┐ ┌─────────────┐  │    │
│  │  │  Compute    │ │   Spot      │ │   Data      │ │   Latency   │  │    │
│  │  │    Cost     │ │ Availability│ │  Locality   │ │    SLO      │  │    │
│  │  └─────────────┘ └─────────────┘ └─────────────┘ └─────────────┘  │    │
│  │                                                                     │    │
│  │  Decision Matrix:                                                   │    │
│  │  ┌──────────────────────────────────────────────────────────────┐ │    │
│  │  │ Provider │ Region      │ Spot$/hr │ OnDem$/hr │ Latency │ Score │ │    │
│  │  ├──────────────────────────────────────────────────────────────┤ │    │
│  │  │ Alibaba  │ cn-hangzhou │ $0.05    │ $0.15     │ 20ms    │ 0.95  │ │    │
│  │  │ AWS      │ ap-south-1  │ $0.08    │ $0.20     │ 45ms    │ 0.82  │ │    │
│  │  │ GCP      │ asia-south1 │ $0.07    │ $0.18     │ 40ms    │ 0.85  │ │    │
│  │  │ Azure    │ southeastasia│ $0.09   │ $0.22     │ 50ms    │ 0.78  │ │    │
│  │  └──────────────────────────────────────────────────────────────┘ │    │
│  └────────────────────────────────────────────────────────────────────┘    │
│                              │                                              │
│                              ▼                                              │
│  ┌────────────────────────────────────────────────────────────────────┐    │
│  │                    Provider Registry                                │    │
│  ├────────────────────────────────────────────────────────────────────┤    │
│  │                                                                     │    │
│  │  ┌──────────┐  ┌──────────┐  ┌──────────┐  ┌──────────┐           │    │
│  │  │   AWS    │  │   GCP    │  │  Azure   │  │ Alibaba  │           │    │
│  │  │ Adapter  │  │ Adapter  │  │ Adapter  │  │ Adapter  │           │    │
│  │  └────┬─────┘  └────┬─────┘  └────┬─────┘  └────┬─────┘           │    │
│  │       │             │             │             │                  │    │
│  │       ▼             ▼             ▼             ▼                  │    │
│  │  ┌─────────────────────────────────────────────────────────────┐  │    │
│  │  │              Unified Cloud Interface                         │  │    │
│  │  │  • get_spot_price(region, instance_type)                    │  │    │
│  │  │  • get_latency(region, target_region)                       │  │    │
│  │  │  • deploy_agent(agent, region, options)                     │  │    │
│  │  │  • route_request(request, constraints)                      │  │    │
│  │  └─────────────────────────────────────────────────────────────┘  │    │
│  └────────────────────────────────────────────────────────────────────┘    │
│                              │                                              │
│                              ▼                                              │
│  ┌────────────────────────────────────────────────────────────────────┐    │
│  │                    Cross-Cloud Federation                           │    │
│  ├────────────────────────────────────────────────────────────────────┤    │
│  │                                                                     │    │
│  │     AWS (us-east-1)              GCP (asia-south1)                 │    │
│  │    ┌─────────────┐              ┌─────────────┐                    │    │
│  │    │ Text Agent  │◄────A2A─────►│ Video Agent │                    │    │
│  │    │   $0.10/hr  │              │   $2.50/hr  │                    │    │
│  │    └─────────────┘              │ (cheap GPU) │                    │    │
│  │                                 └─────────────┘                    │    │
│  │                                                                     │    │
│  │     Azure (westeurope)          Alibaba (cn-hangzhou)              │    │
│  │    ┌─────────────┐              ┌─────────────┐                    │    │
│  │    │ Enterprise  │◄────A2A─────►│ Image Agent │                    │    │
│  │    │ Data Agent  │              │   $0.05/hr  │                    │    │
│  │    └─────────────┘              │ (cheapest)  │                    │    │
│  │                                 └─────────────┘                    │    │
│  └────────────────────────────────────────────────────────────────────┘    │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Technical Specification

#### 1. Cost-Aware Router

```cpp
// NeamC/include/neamc/multicloud/cost_router.hpp

namespace neamc::multicloud {

struct CloudProvider {
    enum class Type { AWS, GCP, Azure, Alibaba, OnPrem };
    Type type;
    std::string name;
    std::vector<std::string> regions;
};

struct RegionPricing {
    std::string provider;
    std::string region;
    std::string instance_type;
    double spot_price_per_hour;
    double ondemand_price_per_hour;
    double spot_availability;  // 0.0 - 1.0
    double egress_cost_per_gb;
    std::chrono::system_clock::time_point last_updated;
};

struct RoutingConstraints {
    double max_latency_ms = 500.0;
    double max_cost_per_invocation = 1.0;
    bool require_spot = false;
    bool allow_cross_region = true;
    bool allow_cross_cloud = true;
    std::vector<std::string> preferred_regions;
    std::vector<std::string> excluded_regions;
    std::string data_residency_requirement;  // e.g., "EU", "APAC"
};

struct RoutingDecision {
    std::string provider;
    std::string region;
    std::string instance_type;
    bool use_spot;
    double estimated_cost;
    double estimated_latency_ms;
    std::string reasoning;
    std::vector<std::string> fallback_options;
};

class CostAwareRouter {
public:
    CostAwareRouter();

    // Register cloud providers
    void register_provider(const CloudProvider& provider);

    // Update pricing data
    void update_pricing(const RegionPricing& pricing);

    // Get optimal routing decision
    RoutingDecision route(
        const std::string& agent_id,
        const std::string& source_region,
        const RoutingConstraints& constraints
    );

    // Batch routing for multiple requests
    std::vector<RoutingDecision> route_batch(
        const std::vector<std::string>& agent_ids,
        const std::string& source_region,
        const RoutingConstraints& constraints
    );

    // Get cost comparison across providers
    struct CostComparison {
        std::string provider;
        std::string region;
        double hourly_cost;
        double monthly_cost_estimate;
        double savings_vs_baseline;
    };
    std::vector<CostComparison> compare_costs(
        const std::string& agent_id,
        const std::string& baseline_provider
    );

    // Pricing refresh
    void refresh_spot_prices();

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace neamc::multicloud
```

#### 2. Alibaba Cloud Adapter

```cpp
// NeamC/include/neamc/deploy/alibaba_adapter.hpp

namespace neamc::deploy {

struct AlibabaConfig {
    std::string access_key_id;
    std::string access_key_secret;
    std::string default_region = "cn-hangzhou";
    bool use_internal_network = true;  // For lower latency within Alibaba
};

struct AliFunctionConfig {
    std::string function_name;
    std::string service_name;
    int memory_mb = 128;
    int timeout_seconds = 60;
    std::string runtime = "custom.debian10";
    std::string handler = "neam-runtime";
    std::map<std::string, std::string> environment;
};

struct AliECSConfig {
    std::string instance_type = "ecs.g6.large";
    std::string image_id;  // Neam base image
    bool use_spot = true;
    double spot_price_limit = 0.0;  // 0 = market price
    std::string security_group_id;
    std::string vswitch_id;
};

class AlibabaAdapter {
public:
    explicit AlibabaAdapter(AlibabaConfig config);

    // Function Compute (serverless)
    std::string deploy_function(const AliFunctionConfig& config);
    void update_function(const std::string& function_name, const AliFunctionConfig& config);
    void delete_function(const std::string& function_name);

    // ECS (containers/VMs)
    std::string deploy_ecs(const AliECSConfig& config);
    void terminate_ecs(const std::string& instance_id);

    // Container Service for Kubernetes (ACK)
    std::string deploy_to_ack(const std::string& cluster_id, const std::string& manifest);

    // Pricing
    double get_spot_price(const std::string& region, const std::string& instance_type);
    double get_function_price(int memory_mb, int invocations);

    // GPU instances
    std::vector<std::string> list_gpu_instance_types(const std::string& region);
    double get_gpu_spot_price(const std::string& region, const std::string& instance_type);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Generate deployment templates
std::string generate_alibaba_fc_template(
    const std::string& agent_name,
    const AliFunctionConfig& config
);

std::string generate_alibaba_ecs_template(
    const std::string& agent_name,
    const AliECSConfig& config
);

std::string generate_alibaba_ack_manifest(
    const std::string& agent_name,
    int replicas,
    const std::map<std::string, std::string>& resources
);

}  // namespace neamc::deploy
```

#### 3. Cross-Cloud Federation

```cpp
// NeamC/include/neamc/multicloud/federation.hpp

namespace neamc::multicloud {

struct CrossCloudAgent {
    std::string agent_id;
    std::string provider;
    std::string region;
    std::string endpoint_url;
    double cost_per_invocation;
    double avg_latency_ms;
    std::vector<std::string> skills;
};

struct CrossCloudRequest {
    std::string source_agent_id;
    std::string target_agent_id;
    std::string skill_id;
    nlohmann::json payload;
    RoutingConstraints constraints;
};

struct CrossCloudResponse {
    bool success;
    nlohmann::json result;
    double actual_cost;
    double actual_latency_ms;
    std::string executed_in_provider;
    std::string executed_in_region;
};

class CrossCloudFederation {
public:
    CrossCloudFederation(CostAwareRouter& router);

    // Agent registration
    void register_agent(const CrossCloudAgent& agent);
    void deregister_agent(const std::string& agent_id);

    // Cross-cloud invocation
    CrossCloudResponse invoke(const CrossCloudRequest& request);

    // Async invocation
    std::future<CrossCloudResponse> invoke_async(const CrossCloudRequest& request);

    // Batch invocation with optimal distribution
    std::vector<CrossCloudResponse> invoke_batch(
        const std::vector<CrossCloudRequest>& requests
    );

    // Get federation statistics
    struct FederationStats {
        int total_cross_cloud_calls;
        double total_cost_savings;
        std::map<std::string, int> calls_per_provider;
        std::map<std::string, double> cost_per_provider;
    };
    FederationStats get_stats() const;

private:
    CostAwareRouter& router_;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace neamc::multicloud
```

### DSL Extensions

```neam
// Multi-cloud deployment configuration
deployment my_agents {
    // Define cloud targets
    clouds {
        aws {
            regions = ["us-east-1", "ap-south-1"]
            prefer_spot = true
            max_spot_price = $0.10
        }

        gcp {
            regions = ["asia-south1", "us-central1"]
            prefer_preemptible = true
        }

        alibaba {
            regions = ["cn-hangzhou", "cn-shanghai"]
            prefer_spot = true
            use_internal_network = true
        }

        azure {
            regions = ["southeastasia", "westeurope"]
            prefer_spot = true
        }
    }

    // Routing rules
    routing {
        // Route by data locality
        rule "data_locality" {
            when data_region == "APAC"
            prefer clouds.alibaba, clouds.gcp
        }

        // Route by cost
        rule "cost_optimization" {
            when workload_type == "batch"
            prefer cheapest_spot()
            fallback cheapest_ondemand()
        }

        // Route by capability
        rule "gpu_workloads" {
            when requires_gpu == true
            prefer alibaba.cn-hangzhou  // Cheapest GPU
            fallback gcp.asia-south1
        }
    }

    // Agent placement
    agent text_agent {
        deploy_to = [aws.us-east-1, alibaba.cn-hangzhou]
        replication = 3
    }

    agent video_agent {
        deploy_to = [alibaba.cn-hangzhou]  // Cheapest GPU
        instance_type = "ecs.gn6i-c4g1.xlarge"
        replication = 2
    }

    agent enterprise_agent {
        deploy_to = [azure.westeurope]  // Data residency
        replication = 2
    }
}

// Cost-aware agent routing
agent smart_router {
    name = "Cost-Optimized Router"

    skill route_request {
        parameters = {
            request: any,
            constraints: RoutingConstraints
        }

        // Automatic cost-aware routing
        route = auto_optimize(
            prefer = [cheapest_available],
            constraints = constraints,
            fallback = [next_cheapest]
        )
    }
}
```

### CLI Commands

```bash
# Multi-cloud deployment
neam deploy --multi-cloud --config deployment.neam

# Show cost comparison
neam cloud compare --agent text_agent --regions all
# Output:
# Provider   Region          Spot $/hr  OnDem $/hr  Latency  Recommended
# Alibaba    cn-hangzhou     $0.05      $0.15       20ms     ✓ Best Value
# GCP        asia-south1     $0.07      $0.18       40ms
# AWS        ap-south-1      $0.08      $0.20       45ms
# Azure      southeastasia   $0.09      $0.22       50ms

# Route analysis
neam cloud analyze-routing --last 24h
# Output:
# Cross-cloud calls: 15,234
# Cost savings from routing: $127.50 (23%)
# By provider:
#   Alibaba: 8,234 calls ($45.20)
#   AWS:     4,521 calls ($67.80)
#   GCP:     2,479 calls ($52.30)

# Spot price monitoring
neam cloud spot-prices --providers aws,gcp,alibaba --instance-type gpu
```

---

## F3: GPU & Vectorized Operations

### Overview

Introduce compilation paths and runtime support for GPU acceleration and SIMD operations, enabling efficient processing of multimedia workloads on cloud GPU instances.

### Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                      GPU & Vectorized Operations                             │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  ┌────────────────────────────────────────────────────────────────────┐    │
│  │                     Neam DSL Source                                 │    │
│  │                                                                     │    │
│  │  @gpu                                                              │    │
│  │  fun process_frames(frames: List<Frame>) -> List<Embedding> {      │    │
│  │      return frames.parallel_map(|f| embed_frame(f))                │    │
│  │  }                                                                 │    │
│  └────────────────────────────────────────────────────────────────────┘    │
│                              │                                              │
│                              ▼                                              │
│  ┌────────────────────────────────────────────────────────────────────┐    │
│  │                     Compiler Frontend                               │    │
│  ├────────────────────────────────────────────────────────────────────┤    │
│  │  • Detect @gpu / @simd annotations                                 │    │
│  │  • Identify parallelizable operations                              │    │
│  │  • Generate IR with execution hints                                │    │
│  └────────────────────────────────────────────────────────────────────┘    │
│                              │                                              │
│              ┌───────────────┼───────────────┐                             │
│              ▼               ▼               ▼                             │
│  ┌──────────────────┐ ┌──────────────┐ ┌──────────────────┐               │
│  │   CPU Bytecode   │ │  SIMD Path   │ │   GPU Path       │               │
│  │   (Default)      │ │  (AVX/NEON)  │ │   (CUDA/Metal)   │               │
│  └──────────────────┘ └──────────────┘ └──────────────────┘               │
│              │               │               │                             │
│              ▼               ▼               ▼                             │
│  ┌────────────────────────────────────────────────────────────────────┐    │
│  │                     Unified Runtime                                 │    │
│  ├────────────────────────────────────────────────────────────────────┤    │
│  │                                                                     │    │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐             │    │
│  │  │    VM        │  │   SIMD       │  │    GPU       │             │    │
│  │  │  Interpreter │  │  Executor    │  │  Dispatcher  │             │    │
│  │  └──────────────┘  └──────────────┘  └──────────────┘             │    │
│  │                                                                     │    │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐             │    │
│  │  │   Batch      │  │   Memory     │  │    Async     │             │    │
│  │  │  Scheduler   │  │   Manager    │  │   Pipeline   │             │    │
│  │  └──────────────┘  └──────────────┘  └──────────────┘             │    │
│  └────────────────────────────────────────────────────────────────────┘    │
│                              │                                              │
│              ┌───────────────┼───────────────┐                             │
│              ▼               ▼               ▼                             │
│  ┌──────────────────┐ ┌──────────────┐ ┌──────────────────┐               │
│  │   CPU Threads    │ │  AVX-512     │ │  CUDA/cuDNN      │               │
│  │   (std::thread)  │ │  ARM NEON    │ │  Metal/MPS       │               │
│  └──────────────────┘ └──────────────┘ └──────────────────┘               │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Technical Specification

#### 1. GPU Executor

```cpp
// NeamC/include/neamc/gpu/executor.hpp

namespace neamc::gpu {

enum class Backend {
    CUDA,
    Metal,
    OpenCL,
    Vulkan,
    CPU_Fallback
};

struct DeviceInfo {
    Backend backend;
    std::string name;
    size_t total_memory;
    size_t available_memory;
    int compute_capability_major;
    int compute_capability_minor;
    int multiprocessor_count;
};

struct GPUTensor {
    void* data;
    std::vector<size_t> shape;
    enum class DType { Float32, Float16, Int32, Int8 };
    DType dtype;
    bool on_device;
};

struct BatchConfig {
    int max_batch_size = 32;
    int min_batch_size = 1;
    std::chrono::milliseconds batch_timeout{10};
    bool dynamic_batching = true;
};

class GPUExecutor {
public:
    GPUExecutor();
    ~GPUExecutor();

    // Device management
    std::vector<DeviceInfo> list_devices();
    void select_device(int device_id);
    DeviceInfo current_device() const;

    // Memory management
    GPUTensor allocate(const std::vector<size_t>& shape, GPUTensor::DType dtype);
    void free(GPUTensor& tensor);
    void copy_to_device(GPUTensor& tensor, const void* host_data);
    void copy_from_device(const GPUTensor& tensor, void* host_data);

    // Batch processing
    void set_batch_config(BatchConfig config);

    // Execute GPU kernels
    template<typename F>
    GPUTensor execute(F&& kernel, const std::vector<GPUTensor>& inputs);

    // Common operations (pre-implemented)
    GPUTensor matmul(const GPUTensor& a, const GPUTensor& b);
    GPUTensor conv2d(const GPUTensor& input, const GPUTensor& kernel);
    GPUTensor embedding_lookup(const GPUTensor& embeddings, const GPUTensor& indices);
    GPUTensor batch_normalize(const GPUTensor& input);
    GPUTensor softmax(const GPUTensor& input, int axis = -1);

    // Image processing
    GPUTensor resize_images(const GPUTensor& images, int height, int width);
    GPUTensor normalize_images(const GPUTensor& images);
    GPUTensor extract_features(const GPUTensor& images, const std::string& model);

    // Audio processing
    GPUTensor compute_spectrogram(const GPUTensor& audio);
    GPUTensor mel_filterbank(const GPUTensor& spectrogram);

    // Statistics
    struct Stats {
        size_t total_ops;
        size_t gpu_ops;
        double gpu_utilization;
        double memory_utilization;
        double avg_batch_size;
    };
    Stats get_stats() const;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace neamc::gpu
```

#### 2. SIMD Executor

```cpp
// NeamC/include/neamc/simd/executor.hpp

namespace neamc::simd {

enum class ISA {
    SSE4_2,
    AVX2,
    AVX512,
    NEON,
    SVE,
    Scalar  // Fallback
};

struct SIMDCapabilities {
    bool has_sse42;
    bool has_avx2;
    bool has_avx512;
    bool has_neon;
    bool has_sve;
    int vector_width_bits;
};

class SIMDExecutor {
public:
    SIMDExecutor();

    // Query capabilities
    SIMDCapabilities get_capabilities() const;
    ISA best_available_isa() const;

    // Vectorized operations
    std::vector<float> add(const std::vector<float>& a, const std::vector<float>& b);
    std::vector<float> mul(const std::vector<float>& a, const std::vector<float>& b);
    std::vector<float> fma(const std::vector<float>& a,
                           const std::vector<float>& b,
                           const std::vector<float>& c);  // a*b + c

    float dot_product(const std::vector<float>& a, const std::vector<float>& b);
    float sum(const std::vector<float>& v);
    float max(const std::vector<float>& v);
    float min(const std::vector<float>& v);

    // String operations (SIMD-accelerated)
    size_t find_char(const std::string& s, char c);
    std::vector<size_t> find_all(const std::string& s, const std::string& pattern);

    // Embedding operations
    std::vector<float> cosine_similarity_batch(
        const std::vector<float>& query,  // Single query embedding
        const std::vector<std::vector<float>>& candidates  // Many candidates
    );

    // JSON parsing (SIMD-accelerated via simdjson)
    nlohmann::json parse_json_fast(const std::string& json_str);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace neamc::simd
```

#### 3. Batch Processor

```cpp
// NeamC/include/neamc/gpu/batch_processor.hpp

namespace neamc::gpu {

template<typename Input, typename Output>
class BatchProcessor {
public:
    using ProcessFn = std::function<std::vector<Output>(const std::vector<Input>&)>;

    struct Config {
        int max_batch_size = 32;
        int min_batch_size = 1;
        std::chrono::milliseconds max_wait{10};
        bool adaptive_batching = true;
    };

    BatchProcessor(ProcessFn process_fn, Config config);

    // Submit single item (will be batched)
    std::future<Output> submit(Input input);

    // Submit batch directly
    std::vector<Output> process_batch(std::vector<Input> inputs);

    // Statistics
    struct Stats {
        size_t total_items_processed;
        size_t total_batches_processed;
        double avg_batch_size;
        double avg_wait_time_ms;
        double items_per_second;
    };
    Stats get_stats() const;

    // Shutdown
    void shutdown();

private:
    ProcessFn process_fn_;
    Config config_;
    // Internal batching queue
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Pre-built batch processors
using ImageBatchProcessor = BatchProcessor<std::vector<uint8_t>, std::vector<float>>;
using TextBatchProcessor = BatchProcessor<std::string, std::vector<float>>;
using AudioBatchProcessor = BatchProcessor<std::vector<float>, std::vector<float>>;

}  // namespace neamc::gpu
```

### DSL Extensions

```neam
// GPU-accelerated agent
agent video_processor {
    name = "Video Frame Processor"

    // Specify GPU requirements
    hardware {
        gpu = required
        min_gpu_memory = 4GB
        preferred_gpu = ["nvidia", "metal"]
    }

    // GPU-accelerated skill
    @gpu
    skill process_video {
        description = "Process video frames in batch"
        parameters = {
            frames: List<Frame>,
            operations: List<string>
        }

        // Batched GPU processing
        batch_size = 32

        implementation = """
            // Automatic GPU dispatch
            embeddings = frames.parallel_map(|frame| {
                frame
                    |> resize(224, 224)
                    |> normalize()
                    |> extract_features("resnet50")
            })
            return embeddings
        """
    }

    // SIMD-accelerated skill
    @simd
    skill similarity_search {
        description = "Find similar embeddings"
        parameters = {
            query: Embedding,
            candidates: List<Embedding>
        }

        implementation = """
            // SIMD-accelerated cosine similarity
            scores = simd.cosine_similarity_batch(query, candidates)
            return scores.top_k(10)
        """
    }
}

// Batch processing configuration
batch_config video_batch {
    max_batch_size = 64
    min_batch_size = 8
    max_wait_ms = 50
    adaptive = true

    // Prioritization
    priority_queue = true
    high_priority_threshold = 0.8  // Fill ratio
}

// GPU resource sharing
gpu_pool shared_gpu {
    devices = auto_detect()

    allocation {
        video_processor = 60%  // 60% of GPU memory
        image_analyzer = 30%
        reserved = 10%
    }

    scheduling = "fair_share"
    preemption = false
}
```

### Performance Targets

| Operation | CPU Baseline | SIMD | GPU | Speedup |
|-----------|--------------|------|-----|---------|
| Image resize (224x224) | 5ms | 1.5ms | 0.1ms | 50× |
| Feature extraction | 50ms | N/A | 5ms | 10× |
| Embedding similarity (1000) | 2ms | 0.3ms | 0.05ms | 40× |
| JSON parsing | 1ms | 0.2ms | N/A | 5× |
| Batch inference (32) | 1600ms | N/A | 80ms | 20× |

---

## F4: FinOps Dashboard & Benchmarking

### Overview

Create a comprehensive "Neam Cost Dashboard" for real-time cost monitoring, continuous benchmarking, and automated cost optimization recommendations.

### Architecture

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                     FinOps Dashboard Architecture                            │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  ┌────────────────────────────────────────────────────────────────────┐    │
│  │                        Data Collection                              │    │
│  ├────────────────────────────────────────────────────────────────────┤    │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐             │    │
│  │  │  Neam Agent  │  │   Cloud      │  │   LLM API    │             │    │
│  │  │   Metrics    │  │   Billing    │  │   Usage      │             │    │
│  │  └──────┬───────┘  └──────┬───────┘  └──────┬───────┘             │    │
│  │         │                 │                 │                      │    │
│  │         └─────────────────┼─────────────────┘                      │    │
│  │                           ▼                                        │    │
│  │                  ┌──────────────────┐                             │    │
│  │                  │  Metrics Store   │                             │    │
│  │                  │  (TimescaleDB)   │                             │    │
│  │                  └──────────────────┘                             │    │
│  └────────────────────────────────────────────────────────────────────┘    │
│                              │                                              │
│                              ▼                                              │
│  ┌────────────────────────────────────────────────────────────────────┐    │
│  │                     Analysis Engine                                 │    │
│  ├────────────────────────────────────────────────────────────────────┤    │
│  │                                                                     │    │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐             │    │
│  │  │    Cost      │  │   Anomaly    │  │   Forecast   │             │    │
│  │  │  Attribution │  │  Detection   │  │    Engine    │             │    │
│  │  └──────────────┘  └──────────────┘  └──────────────┘             │    │
│  │                                                                     │    │
│  │  ┌──────────────┐  ┌──────────────┐  ┌──────────────┐             │    │
│  │  │  Benchmark   │  │   Savings    │  │   Alert      │             │    │
│  │  │   Comparator │  │   Calculator │  │   Manager    │             │    │
│  │  └──────────────┘  └──────────────┘  └──────────────┘             │    │
│  └────────────────────────────────────────────────────────────────────┘    │
│                              │                                              │
│                              ▼                                              │
│  ┌────────────────────────────────────────────────────────────────────┐    │
│  │                    Recommendation Engine                            │    │
│  ├────────────────────────────────────────────────────────────────────┤    │
│  │                                                                     │    │
│  │  Recommendations:                                                   │    │
│  │  ┌────────────────────────────────────────────────────────────┐   │    │
│  │  │ 1. Switch agent "FAQ Bot" to GPT-3.5 for 30% of queries   │   │    │
│  │  │    → Estimated savings: $450/month with <2% accuracy loss │   │    │
│  │  ├────────────────────────────────────────────────────────────┤   │    │
│  │  │ 2. Enable caching for agent "Product Search"               │   │    │
│  │  │    → 45% of queries are repeated, save $200/month         │   │    │
│  │  ├────────────────────────────────────────────────────────────┤   │    │
│  │  │ 3. Move "Video Agent" to Alibaba cn-hangzhou (spot)        │   │    │
│  │  │    → GPU cost: $0.85/hr → $0.35/hr, save $720/month       │   │    │
│  │  └────────────────────────────────────────────────────────────┘   │    │
│  └────────────────────────────────────────────────────────────────────┘    │
│                              │                                              │
│                              ▼                                              │
│  ┌────────────────────────────────────────────────────────────────────┐    │
│  │                        Dashboard UI                                 │    │
│  ├────────────────────────────────────────────────────────────────────┤    │
│  │                                                                     │    │
│  │  ┌─────────────────────────────────────────────────────────────┐  │    │
│  │  │  Total Cost: $4,523/month    │  Savings Opportunity: $1,370 │  │    │
│  │  ├─────────────────────────────────────────────────────────────┤  │    │
│  │  │                                                             │  │    │
│  │  │  By Agent:          By Modality:        By Provider:       │  │    │
│  │  │  ┌────────────┐    ┌────────────┐      ┌────────────┐     │  │    │
│  │  │  │Video: 45%  │    │LLM:   60%  │      │AWS:   40%  │     │  │    │
│  │  │  │Chat:  30%  │    │GPU:   25%  │      │GCP:   35%  │     │  │    │
│  │  │  │Search:15%  │    │Voice: 10%  │      │Alibaba:25% │     │  │    │
│  │  │  │Other: 10%  │    │Other:  5%  │      │          │     │  │    │
│  │  │  └────────────┘    └────────────┘      └────────────┘     │  │    │
│  │  │                                                             │  │    │
│  │  └─────────────────────────────────────────────────────────────┘  │    │
│  └────────────────────────────────────────────────────────────────────┘    │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Technical Specification

#### 1. Cost Attribution Engine

```cpp
// NeamC/include/neamc/finops/cost_attribution.hpp

namespace neamc::finops {

struct CostEntry {
    std::string agent_id;
    std::string skill_id;
    std::string invocation_id;
    std::chrono::system_clock::time_point timestamp;

    // Cost breakdown
    double llm_cost;           // LLM API cost
    double compute_cost;       // Cloud compute cost
    double gpu_cost;           // GPU-specific cost
    double network_cost;       // Data transfer cost
    double storage_cost;       // Storage/caching cost
    double total_cost;

    // Resource usage
    int input_tokens;
    int output_tokens;
    double cpu_seconds;
    double gpu_seconds;
    double memory_gb_seconds;

    // Metadata
    std::string model_used;
    std::string cloud_provider;
    std::string region;
    bool used_cache;
    bool used_spot;
};

struct CostSummary {
    std::chrono::system_clock::time_point period_start;
    std::chrono::system_clock::time_point period_end;

    double total_cost;
    double total_llm_cost;
    double total_compute_cost;
    double total_gpu_cost;

    std::map<std::string, double> cost_by_agent;
    std::map<std::string, double> cost_by_skill;
    std::map<std::string, double> cost_by_model;
    std::map<std::string, double> cost_by_provider;
    std::map<std::string, double> cost_by_region;

    int total_invocations;
    double avg_cost_per_invocation;

    // Efficiency metrics
    double cache_hit_rate;
    double spot_usage_rate;
    double batch_efficiency;
};

class CostAttributionEngine {
public:
    CostAttributionEngine();

    // Record costs
    void record(const CostEntry& entry);
    void record_batch(const std::vector<CostEntry>& entries);

    // Query costs
    CostSummary get_summary(
        std::chrono::system_clock::time_point start,
        std::chrono::system_clock::time_point end
    );

    CostSummary get_summary_by_agent(
        const std::string& agent_id,
        std::chrono::system_clock::time_point start,
        std::chrono::system_clock::time_point end
    );

    // Top consumers
    std::vector<std::pair<std::string, double>> top_agents_by_cost(int n);
    std::vector<std::pair<std::string, double>> top_skills_by_cost(int n);

    // Cost trends
    std::vector<std::pair<std::chrono::system_clock::time_point, double>>
    get_cost_trend(
        std::chrono::hours granularity,
        int periods
    );

    // Export
    void export_csv(const std::string& path);
    void export_json(const std::string& path);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace neamc::finops
```

#### 2. Recommendation Engine

```cpp
// NeamC/include/neamc/finops/recommendations.hpp

namespace neamc::finops {

struct CostRecommendation {
    enum class Type {
        SwitchModel,
        EnableCaching,
        ChangeProvider,
        UseSpotInstances,
        AdjustBatching,
        OptimizePrompt,
        ScaleDown,
        ConsolidateAgents
    };

    Type type;
    std::string agent_id;
    std::string description;
    double estimated_monthly_savings;
    double confidence;  // 0.0 - 1.0
    std::string implementation_steps;

    // Trade-offs
    double estimated_latency_impact_pct;  // Positive = slower
    double estimated_accuracy_impact_pct; // Positive = worse

    // Priority
    enum class Priority { Low, Medium, High, Critical };
    Priority priority;
};

struct RecommendationConfig {
    double min_savings_threshold = 10.0;  // Min $10/month savings
    double max_latency_impact_pct = 10.0;  // Max 10% latency increase
    double max_accuracy_impact_pct = 5.0;  // Max 5% accuracy loss
    bool include_provider_changes = true;
    bool include_model_changes = true;
    bool include_architecture_changes = true;
};

class RecommendationEngine {
public:
    explicit RecommendationEngine(RecommendationConfig config);

    // Generate recommendations
    std::vector<CostRecommendation> generate_recommendations(
        const CostSummary& current_costs,
        const std::map<std::string, AgentProfile>& agent_profiles
    );

    // Model switching analysis
    struct ModelAlternative {
        std::string current_model;
        std::string suggested_model;
        double cost_reduction_pct;
        double accuracy_delta;  // From A/B test or estimate
    };
    std::vector<ModelAlternative> analyze_model_alternatives(
        const std::string& agent_id
    );

    // Caching opportunity analysis
    struct CachingOpportunity {
        std::string agent_id;
        std::string skill_id;
        double cache_hit_potential;  // 0.0 - 1.0
        double estimated_savings;
    };
    std::vector<CachingOpportunity> analyze_caching_opportunities();

    // Apply recommendation (with confirmation)
    bool apply_recommendation(
        const CostRecommendation& rec,
        bool dry_run = true
    );

private:
    RecommendationConfig config_;
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace neamc::finops
```

#### 3. Continuous Benchmarking

```cpp
// NeamC/include/neamc/finops/benchmarking.hpp

namespace neamc::finops {

struct BenchmarkConfig {
    std::string name;
    std::string description;
    std::vector<std::string> agent_ids;

    // Workload definition
    struct Workload {
        std::string type;  // "text", "voice", "video", "mixed"
        int requests_per_second;
        int duration_seconds;
        std::string input_distribution;  // "uniform", "realistic", "custom"
    };
    Workload workload;

    // Environment
    std::string cloud_provider;
    std::string region;
    std::string instance_type;
    bool use_spot;

    // Metrics to collect
    bool measure_latency = true;
    bool measure_cost = true;
    bool measure_accuracy = true;
    bool measure_throughput = true;
};

struct BenchmarkResult {
    std::string benchmark_id;
    std::chrono::system_clock::time_point run_at;
    std::string neam_version;

    // Performance metrics
    double avg_latency_ms;
    double p50_latency_ms;
    double p95_latency_ms;
    double p99_latency_ms;
    double throughput_rps;

    // Cost metrics
    double total_cost;
    double cost_per_request;
    double cost_per_1k_tokens;

    // Accuracy metrics (if applicable)
    double accuracy_score;

    // Resource metrics
    double avg_cpu_utilization;
    double avg_memory_utilization;
    double avg_gpu_utilization;

    // Comparison
    std::map<std::string, double> vs_previous_version;
};

class ContinuousBenchmarking {
public:
    ContinuousBenchmarking();

    // Run benchmark
    BenchmarkResult run_benchmark(const BenchmarkConfig& config);

    // Schedule recurring benchmarks
    void schedule_benchmark(
        const BenchmarkConfig& config,
        const std::string& cron_schedule
    );

    // Compare versions
    struct VersionComparison {
        std::string version_a;
        std::string version_b;
        double latency_improvement_pct;
        double cost_improvement_pct;
        double throughput_improvement_pct;
    };
    VersionComparison compare_versions(
        const std::string& version_a,
        const std::string& version_b,
        const BenchmarkConfig& config
    );

    // Historical data
    std::vector<BenchmarkResult> get_history(
        const std::string& benchmark_name,
        int limit = 100
    );

    // Regression detection
    struct RegressionAlert {
        std::string metric;
        double baseline_value;
        double current_value;
        double regression_pct;
    };
    std::vector<RegressionAlert> detect_regressions(
        const std::string& benchmark_name
    );

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

}  // namespace neamc::finops
```

### Dashboard UI (Web)

```typescript
// neam-dashboard/src/types.ts

interface CostDashboardState {
  summary: CostSummary;
  recommendations: CostRecommendation[];
  benchmarkHistory: BenchmarkResult[];
  alerts: CostAlert[];
  timeRange: TimeRange;
}

interface CostSummary {
  totalCost: number;
  costByAgent: Record<string, number>;
  costByModality: Record<string, number>;
  costByProvider: Record<string, number>;
  trend: CostTrendPoint[];
  savingsOpportunity: number;
}

interface CostRecommendation {
  id: string;
  type: RecommendationType;
  agentId: string;
  description: string;
  estimatedSavings: number;
  confidence: number;
  tradeoffs: {
    latencyImpact: number;
    accuracyImpact: number;
  };
  status: 'pending' | 'applied' | 'dismissed';
}

// Dashboard components
const CostDashboard: React.FC = () => {
  return (
    <DashboardLayout>
      <CostSummaryPanel />
      <CostBreakdownCharts />
      <RecommendationsPanel />
      <AlertsPanel />
      <BenchmarkHistory />
    </DashboardLayout>
  );
};
```

### CLI Commands

```bash
# View cost summary
neam cost summary --period 30d
# Output:
# ═══════════════════════════════════════════════════════════════
# Neam Cost Summary (Last 30 Days)
# ═══════════════════════════════════════════════════════════════
# Total Cost:          $4,523.47
# Cost Change:         +12% vs previous period
# ───────────────────────────────────────────────────────────────
# By Agent:
#   video_processor    $2,035.56 (45.0%)  ████████████████████░░░░
#   chat_support       $1,357.04 (30.0%)  █████████████░░░░░░░░░░░
#   product_search     $678.52  (15.0%)   ███████░░░░░░░░░░░░░░░░░
#   other              $452.35  (10.0%)   █████░░░░░░░░░░░░░░░░░░░
# ───────────────────────────────────────────────────────────────
# Savings Opportunities: $1,370.00/month
# Run `neam cost recommendations` for details

# View recommendations
neam cost recommendations
# Output:
# ┌──────────────────────────────────────────────────────────────┐
# │ Cost Optimization Recommendations                            │
# ├──────────────────────────────────────────────────────────────┤
# │                                                              │
# │ 1. [HIGH] Switch video_processor to Alibaba GPU spot        │
# │    Savings: $720/month | Impact: +5ms latency               │
# │    Command: neam cost apply --id rec-001                    │
# │                                                              │
# │ 2. [MEDIUM] Enable caching for product_search               │
# │    Savings: $200/month | Cache hit rate: 45%                │
# │    Command: neam cost apply --id rec-002                    │
# │                                                              │
# │ 3. [MEDIUM] Use GPT-3.5 for simple FAQ queries              │
# │    Savings: $450/month | Accuracy impact: -2%               │
# │    Command: neam cost apply --id rec-003                    │
# │                                                              │
# └──────────────────────────────────────────────────────────────┘

# Run benchmark
neam benchmark run --config benchmark.yaml
neam benchmark compare --version1 0.6.3 --version2 0.6.4

# Export cost report
neam cost export --format csv --period 30d --output costs.csv
```

---

## F5: Module System & Guardrails

### Overview

Complete the module import system for code reuse and implement guardrails execution in the VM for compliance and cost protection.

### Module System

```neam
// Standard library module
module std.text {
    export fun tokenize(text: string) -> List<string> { ... }
    export fun clean(text: string) -> string { ... }
    export fun truncate(text: string, max_len: int) -> string { ... }
}

// Import and use
import std.text
import std.http as http

agent my_agent {
    skill process_text {
        let tokens = std.text.tokenize(input)
        let cleaned = std.text.clean(input)
        // ...
    }
}

// Relative imports
import ./utils/helpers
import ../shared/common

// Selective imports
import { tokenize, clean } from std.text
```

### Guardrails System

```neam
// Define guardrails
guardrail cost_limit {
    type = "cost"
    max_per_invocation = $0.50
    max_per_hour = $10.00
    max_per_day = $100.00

    on_exceed = "reject"  // or "warn", "downgrade_model"
}

guardrail pii_filter {
    type = "content"

    rules {
        block_ssn = true
        block_credit_card = true
        block_phone = true
        block_email = false  // Allow emails
    }

    on_violation = "redact"
}

guardrail rate_limit {
    type = "rate"
    max_requests_per_minute = 100
    max_tokens_per_minute = 50000

    on_exceed = "throttle"
}

// Apply guardrails to agent
agent protected_agent {
    guardrails = [cost_limit, pii_filter, rate_limit]

    skill process_request {
        // Guardrails automatically enforced
    }
}
```

---

## Implementation Plan

### Timeline

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                    Neam v0.6.4 Implementation Timeline                       │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  Month 1-2: Foundation                                                       │
│  ────────────────────────────────────────────────────────────────────────   │
│  Week 1-2:  Auto-scaling design & Kubernetes CRD                            │
│  Week 3-4:  Predictive scaler prototype                                     │
│  Week 5-6:  Warm pool manager implementation                                │
│  Week 7-8:  Resource packer & bin packing algorithm                         │
│                                                                              │
│  Month 3-4: Multi-Cloud                                                      │
│  ────────────────────────────────────────────────────────────────────────   │
│  Week 9-10:  Cost-aware router design                                       │
│  Week 11-12: Alibaba Cloud adapter                                          │
│  Week 13-14: Cross-cloud federation                                         │
│  Week 15-16: Spot arbitrage across clouds                                   │
│                                                                              │
│  Month 5-6: GPU/SIMD & FinOps                                               │
│  ────────────────────────────────────────────────────────────────────────   │
│  Week 17-18: GPU executor (CUDA/Metal)                                      │
│  Week 19-20: SIMD executor (AVX/NEON)                                       │
│  Week 21-22: Batch processor                                                │
│  Week 23-24: FinOps dashboard backend                                       │
│                                                                              │
│  Month 7-8: Polish & Release                                                 │
│  ────────────────────────────────────────────────────────────────────────   │
│  Week 25-26: Dashboard UI                                                   │
│  Week 27-28: Recommendation engine                                          │
│  Week 29-30: Module system & guardrails                                     │
│  Week 31-32: Testing, docs, release prep                                    │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

### Milestones

| Milestone | Target Date | Deliverables |
|-----------|-------------|--------------|
| M1: Auto-Scaling Alpha | Month 2 | Predictive scaler, warm pool, K8s integration |
| M2: Multi-Cloud Alpha | Month 4 | Cost router, Alibaba adapter, cross-cloud A2A |
| M3: GPU Acceleration Alpha | Month 5 | GPU executor, batch processing |
| M4: FinOps Dashboard Beta | Month 6 | Cost attribution, recommendations |
| M5: v0.6.4 Beta | Month 7 | All features integrated, beta testing |
| M6: v0.6.4 Release | Month 8 | Production-ready release |

### Resource Requirements

| Role | Count | Focus |
|------|-------|-------|
| Backend Engineer | 2 | Auto-scaling, multi-cloud |
| Systems Engineer | 1 | GPU/SIMD, performance |
| Frontend Engineer | 1 | Dashboard UI |
| DevOps Engineer | 1 | K8s, cloud integrations |
| QA Engineer | 1 | Testing, benchmarking |

### Dependencies

```
┌─────────────────────────────────────────────────────────────────────────────┐
│                        Feature Dependencies                                  │
├─────────────────────────────────────────────────────────────────────────────┤
│                                                                              │
│  v0.6.3 Prerequisites:                                                       │
│  ├── Spot instance support ──────────────┐                                  │
│  ├── Budget enforcement ─────────────────┤                                  │
│  ├── OTLP exporter ──────────────────────┼──► v0.6.4 Features              │
│  └── JIT prototype ──────────────────────┘                                  │
│                                                                              │
│  v0.6.4 Internal Dependencies:                                               │
│                                                                              │
│  Cost Attribution ──► Recommendation Engine ──► Dashboard                   │
│         │                                                                    │
│         └──────────────────► FinOps Hooks                                   │
│                                                                              │
│  Predictive Scaler ──► Warm Pool ──► Resource Packer                        │
│                              │                                               │
│                              └──► Scaling Advisor                           │
│                                                                              │
│  Cost Router ──► Provider Registry ──► Cross-Cloud Federation              │
│       │                                                                      │
│       └──► Spot Arbitrage                                                   │
│                                                                              │
│  GPU Executor ──► Batch Processor ──► Agent Runtime Integration            │
│       │                                                                      │
│       └──► SIMD Executor                                                    │
│                                                                              │
└─────────────────────────────────────────────────────────────────────────────┘
```

---

## Testing Strategy

### Test Categories

| Category | Tests | Coverage Target |
|----------|-------|-----------------|
| Unit Tests | 500+ | 80% code coverage |
| Integration Tests | 100+ | All component interactions |
| Performance Tests | 50+ | Latency, throughput, cost |
| Multi-Cloud Tests | 30+ | AWS, GCP, Azure, Alibaba |
| GPU Tests | 25+ | CUDA, Metal, fallback |
| Load Tests | 20+ | Scale up/down, burst handling |

### Benchmark Suite

```yaml
# neam-gym/benchmarks/v064_cost_efficiency.yaml
benchmarks:
  - name: auto_scaling_response_time
    description: Time to scale from 2 to 20 replicas
    target: < 60 seconds

  - name: cost_per_million_invocations
    description: Total cost for 1M text agent invocations
    target: < $50

  - name: multi_cloud_routing_latency
    description: Overhead of cost-aware routing
    target: < 5ms

  - name: gpu_batch_throughput
    description: Video frames processed per second
    target: > 100 fps

  - name: simd_embedding_similarity
    description: Similarity searches per second (1000 candidates)
    target: > 10000 ops/sec

  - name: cold_start_with_warm_pool
    description: Cold start time with warm pool enabled
    target: < 100ms
```

---

## Risk Assessment

### Technical Risks

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| GPU driver compatibility | Medium | High | Support multiple backends (CUDA, Metal, OpenCL) |
| Multi-cloud API changes | Low | Medium | Abstraction layer, version pinning |
| Prediction accuracy | Medium | Medium | Fall back to reactive scaling |
| Cross-cloud latency | Medium | High | Data locality awareness, edge caching |

### Operational Risks

| Risk | Probability | Impact | Mitigation |
|------|-------------|--------|------------|
| Cost overruns during testing | Medium | Medium | Strict budget limits in test environments |
| Cloud account limits | Low | High | Pre-request limit increases |
| Spot instance interruptions | High | Low | Checkpointing, fast failover |

---

## Success Metrics

### Cost Efficiency Targets

| Metric | v0.6.3 Baseline | v0.6.4 Target | Improvement |
|--------|-----------------|---------------|-------------|
| Over-provisioning waste | 30% | 10% | 66% reduction |
| Spot instance utilization | 40% | 75% | 87% increase |
| Cross-cloud arbitrage savings | 0% | 15% | New capability |
| GPU utilization | 50% | 85% | 70% increase |
| Cost visibility | Manual | Real-time | Automated |

### Adoption Metrics

| Metric | Target |
|--------|--------|
| Auto-scaler adoption | 60% of deployments |
| Multi-cloud usage | 30% of enterprise users |
| Dashboard daily active users | 80% of ops teams |
| Recommendations applied | 50% acceptance rate |

---

## Appendix

### A. Configuration Reference

See [CONFIG_REFERENCE.md](./CONFIG_REFERENCE.md) for complete configuration options.

### B. API Documentation

See [API_REFERENCE.md](./API_REFERENCE.md) for complete API documentation.

### C. Migration Guide

See [MIGRATION_v063_to_v064.md](./MIGRATION_v063_to_v064.md) for upgrade instructions.

---

**Document Version:** 1.0
**Last Updated:** February 2026
**Authors:** Neam Core Team
**Status:** Draft for Review
