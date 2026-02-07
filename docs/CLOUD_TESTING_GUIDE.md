# Neam v0.6.4 Cloud Testing & Cost Comparison Guide

## Overview

This guide covers end-to-end testing of Neam agents across AWS, GCP, Azure, and Alibaba Cloud with automated cost comparison reporting.

---

## Quick Start

```bash
# 1. Configure cloud credentials
export AWS_ACCESS_KEY_ID="your-key"
export AWS_SECRET_ACCESS_KEY="your-secret"
export GCP_PROJECT_ID="your-project"
export AZURE_SUBSCRIPTION_ID="your-subscription"
export ALIBABA_ACCESS_KEY_ID="your-key"

# 2. Run tests
cd tests/cloud
chmod +x run_cloud_tests.sh
./run_cloud_tests.sh

# 3. View report
open test_results/*/cost_comparison_report.html
```

---

## Files Structure

```
tests/cloud/
├── cloud_test_config.toml           # Main configuration
├── run_cloud_tests.sh               # Test runner script
├── prompts/
│   ├── latency_test.jsonl           # Short prompts for latency
│   └── cost_test.jsonl              # Long prompts for cost analysis
├── templates/
│   └── cost_report_template.html    # HTML report template
└── sample_reports/
    └── sample_cost_report.json      # Example output

NeamC/include/neamc/testing/
├── cloud_test_runner.hpp            # C++ test runner API
└── cost_report_generator.hpp        # Report generation API
```

---

## Configuration

### Cloud Provider Settings

Edit `cloud_test_config.toml` to configure each cloud:

```toml
# Enable/disable providers
[clouds.aws]
enabled = true
[clouds.gcp]
enabled = true
[clouds.azure]
enabled = true
[clouds.alibaba]
enabled = true  # Optional for APAC
```

### Test Agents

Define agents to test:

```toml
[[agents]]
id = "agent-simple-qa"
name = "Simple Q&A Agent"
model = "gpt-4o-mini"
max_tokens = 256

[[agents]]
id = "agent-rag-knowledge"
name = "RAG Agent"
model = "gpt-4o"
embedding_model = "text-embedding-3-small"
```

### Workloads

Configure test workloads:

```toml
[[workloads]]
id = "workload-latency"
agent_id = "agent-simple-qa"
requests = 1000
concurrency = 1

[[workloads]]
id = "workload-throughput"
agent_id = "agent-rag-knowledge"
requests = 5000
concurrency = 50
```

---

## Running Tests

### Full Test Suite

```bash
./run_cloud_tests.sh
```

### Specific Providers

```bash
./run_cloud_tests.sh -p aws,gcp
```

### Specific Workloads

```bash
./run_cloud_tests.sh -w latency,cost
```

### High-Volume Test

```bash
./run_cloud_tests.sh -r 10000 -j 100
```

---

## Cost Comparison Report

### Metrics Collected

| Metric | Description |
|--------|-------------|
| `cost_per_request_usd` | Total cost per request |
| `cost_per_1k_tokens_usd` | Cost per 1000 tokens |
| `latency_p50_ms` | Median latency |
| `latency_p95_ms` | 95th percentile latency |
| `latency_p99_ms` | 99th percentile latency |
| `cold_start_ms` | Serverless cold start time |
| `requests_per_second` | Maximum throughput |
| `error_rate_percent` | Request failure rate |

### Report Sections

1. **Executive Summary**
   - Most cost-effective provider
   - Lowest latency provider
   - Highest throughput provider

2. **Provider Comparison**
   - Side-by-side cost breakdown
   - Latency distribution charts
   - Throughput comparison

3. **Compute Type Analysis**
   - Serverless vs Containers vs VMs
   - GPU instance comparison
   - Cold start analysis

4. **Monthly Projections**
   - Estimated costs at scale
   - Break-even analysis
   - Reserved capacity recommendations

5. **Recommendations**
   - Cost optimization suggestions
   - Provider selection guidance
   - Architecture recommendations

---

## Pricing Data

### AWS Pricing (USD)

| Service | Metric | Price |
|---------|--------|-------|
| Lambda | Per GB-second | $0.0000166667 |
| Lambda | Per request | $0.0000002 |
| Fargate | vCPU-hour | $0.04048 |
| Fargate | GB-hour | $0.004445 |
| EC2 t3.medium | On-demand/hour | $0.0416 |
| EC2 t3.medium | Spot/hour | $0.0125 |
| EC2 g4dn.xlarge | On-demand/hour | $0.526 |
| Bedrock Claude Sonnet | Input/1K tokens | $0.003 |
| Bedrock Claude Sonnet | Output/1K tokens | $0.015 |

### GCP Pricing (USD)

| Service | Metric | Price |
|---------|--------|-------|
| Cloud Functions | Per GB-second | $0.0000025 |
| Cloud Functions | Per invocation | $0.0000004 |
| Cloud Run | vCPU-second | $0.00002400 |
| Cloud Run | GB-second | $0.00000250 |
| Compute Engine e2-medium | On-demand/hour | $0.03351 |
| Compute Engine e2-medium | Preemptible/hour | $0.01005 |
| Vertex AI Gemini Pro | Input/1K tokens | $0.00125 |
| Vertex AI Gemini Pro | Output/1K tokens | $0.00375 |

### Azure Pricing (USD)

| Service | Metric | Price |
|---------|--------|-------|
| Functions Consumption | Per execution | $0.0000002 |
| Functions Consumption | Per GB-second | $0.000016 |
| Container Apps | vCPU-second | $0.000024 |
| Container Apps | GB-second | $0.000003 |
| D2s v3 VM | On-demand/hour | $0.096 |
| D2s v3 VM | Spot/hour | $0.0192 |
| Azure OpenAI GPT-4o | Input/1K tokens | $0.005 |
| Azure OpenAI GPT-4o | Output/1K tokens | $0.015 |

### Alibaba Cloud Pricing (CNY → USD)

| Service | Metric | Price (CNY) | Price (USD) |
|---------|--------|-------------|-------------|
| Function Compute | Per GB-second | ¥0.00011 | $0.0000153 |
| Function Compute | Per invocation | ¥0.0000002 | $0.00000003 |
| ECI | vCPU-second | ¥0.000042 | $0.0000058 |
| ECI | GB-second | ¥0.000021 | $0.0000029 |
| ECS g6.large | On-demand/hour | ¥0.63 | $0.0875 |
| ECS g6.large | Spot/hour | ¥0.19 | $0.0264 |

*Note: CNY to USD conversion at 0.139*

---

## Sample Results

### Cost Per Request Comparison

| Provider | Serverless | Container | VM | GPU |
|----------|------------|-----------|-----|-----|
| AWS | $0.00050 | $0.00065 | $0.00078 | $0.00112 |
| GCP | $0.00032 | $0.00048 | $0.00062 | $0.00098 |
| Azure | $0.00043 | $0.00058 | $0.00071 | $0.00125 |
| Alibaba | $0.00028 | $0.00042 | $0.00054 | $0.00089 |

### Latency P95 Comparison (ms)

| Provider | Serverless | Container | VM |
|----------|------------|-----------|-----|
| AWS | 142 | 87 | 68 |
| GCP | 156 | 92 | 74 |
| Azure | 138 | 85 | 65 |
| Alibaba | 198 | 124 | 98 |

### Monthly Cost Projection (10K requests/day)

| Provider | Serverless | Container | Best Option |
|----------|------------|-----------|-------------|
| AWS | $150/month | $195/month | Serverless |
| GCP | $96/month | $144/month | Serverless |
| Azure | $129/month | $174/month | Serverless |
| Alibaba | $84/month | $126/month | Serverless |

---

## Recommendations Matrix

| Use Case | Recommended Provider | Compute Type | Region |
|----------|---------------------|--------------|--------|
| Cost-optimized global | GCP | Serverless | us-central1 |
| Low latency Americas | AWS | Container | us-east-1 |
| Low latency Europe | Azure | Container | northeurope |
| Low latency APAC | Alibaba | Container | ap-southeast-1 |
| High throughput | Azure | Container | eastus |
| GPU inference | GCP | VM GPU | us-central1 |
| Batch processing | Any | Spot VMs | Cheapest region |

---

## Programmatic Usage

### C++ API

```cpp
#include <neamc/testing/cloud_test_runner.hpp>
#include <neamc/testing/cost_report_generator.hpp>

// Create test runner
auto runner = create_cloud_test_runner("cloud_test_config.toml");

// Run all tests
auto report = runner->run_all_tests();

// Generate HTML report
auto generator = create_cost_report_generator();
auto html = generator->export_html(report);

// Get recommendations
auto recommendations = generator->generate_recommendations(report);
for (const auto& rec : recommendations) {
    std::cout << rec.title << ": " << rec.estimated_monthly_savings_usd << " USD/month\n";
}
```

### Integration with CI/CD

```yaml
# .github/workflows/cloud-tests.yml
name: Multi-Cloud Tests

on:
  schedule:
    - cron: '0 0 * * 0'  # Weekly

jobs:
  cloud-test:
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v4

      - name: Configure AWS
        uses: aws-actions/configure-aws-credentials@v4
        with:
          aws-access-key-id: ${{ secrets.AWS_ACCESS_KEY_ID }}
          aws-secret-access-key: ${{ secrets.AWS_SECRET_ACCESS_KEY }}

      - name: Configure GCP
        uses: google-github-actions/auth@v2
        with:
          credentials_json: ${{ secrets.GCP_CREDENTIALS }}

      - name: Run Cloud Tests
        run: ./tests/cloud/run_cloud_tests.sh -r 100

      - name: Upload Report
        uses: actions/upload-artifact@v4
        with:
          name: cost-report
          path: tests/cloud/test_results/
```

---

## Troubleshooting

| Issue | Solution |
|-------|----------|
| AWS credentials invalid | Run `aws configure` or set environment variables |
| GCP project not found | Set `GCP_PROJECT_ID` environment variable |
| Azure subscription error | Run `az login` and `az account set` |
| Alibaba timeout | Check VPN/network access to Chinese regions |
| High cold start times | Enable provisioned concurrency for serverless |
| Cost discrepancy | Verify pricing is up-to-date in config |

---

## Next Steps

1. **Customize Workloads**: Edit `prompts/*.jsonl` with your actual use cases
2. **Add Regions**: Expand regional testing in config
3. **Set Up Monitoring**: Integrate with CloudWatch/Stackdriver/Monitor
4. **Automate Reports**: Schedule weekly cost comparison reports
5. **Optimize Based on Results**: Implement recommended changes
