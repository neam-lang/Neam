// SPDX-License-Identifier: Apache-2.0
//
// Neam v0.6.0 - Cloud Deployment Configuration
//

#pragma once

#include <map>
#include <string>
#include <vector>

namespace neamc::config
{

struct StateConfig
{
  std::string backend = "local";
  std::string bucket;
  std::string prefix = "neam/state";
  std::string lock_table;
  int ttl_seconds = 3600;
};

struct LLMConfig
{
  struct RateLimit
  {
    int requests_per_minute = 60;
    int tokens_per_minute = 100000;
    int burst = 10;
  };

  struct CircuitBreaker
  {
    int failure_threshold = 5;
    int reset_timeout_seconds = 30;
    int half_open_requests = 2;
  };

  struct Cache
  {
    bool enabled = false;
    std::string backend = "memory";
    int ttl_seconds = 300;
    int max_entries = 1000;
  };

  struct Cost
  {
    double budget_usd = 0.0;
    std::string period = "daily";
    bool alert_enabled = false;
    double alert_threshold_pct = 80.0;
  };

  std::string provider = "openai";
  std::string model = "gpt-4";
  std::string api_key_ref;
  std::map<std::string, RateLimit> rate_limits;
  CircuitBreaker circuit_breaker;
  Cache cache;
  Cost cost;
};

struct TelemetryConfig
{
  bool enabled = false;
  std::string exporter = "otlp";
  std::string endpoint;
  std::string service_name = "neam-agent";
  int flush_interval_ms = 5000;
  double sampling_rate = 1.0;
};

struct SecretsConfig
{
  std::string provider = "env";
  std::string vault_addr;
  std::string vault_path = "secret/data/neam";
  std::string aws_region;
  std::string azure_vault_url;
  std::string gcp_project;
  std::string k8s_namespace;
};

struct AwsDeployConfig
{
  struct Lambda
  {
    std::string function_name;
    int memory = 1024;
    int timeout = 300;
    std::string runtime = "provided.al2023";
    std::string architecture = "arm64";
  };

  struct Ecs
  {
    std::string cluster;
    std::string service;
    int desired_count = 1;
    std::string cpu = "512";
    std::string memory = "1024";
  };

  std::string region = "us-east-1";
  Lambda lambda;
  Ecs ecs;
};

struct GcpDeployConfig
{
  struct CloudRun
  {
    std::string service;
    std::string region = "us-central1";
    int min_instances = 0;
    int max_instances = 10;
    std::string memory = "1Gi";
    std::string cpu = "1";
    int concurrency = 80;
  };

  std::string project;
  CloudRun cloud_run;
};

struct AzureDeployConfig
{
  struct ContainerApps
  {
    std::string environment;
    std::string name;
    int min_replicas = 0;
    int max_replicas = 10;
    std::string cpu = "0.5";
    std::string memory = "1Gi";
  };

  struct Aks
  {
    std::string cluster;
    std::string resource_group;
    std::string node_pool = "default";
  };

  std::string subscription_id;
  std::string resource_group;
  ContainerApps container_apps;
  Aks aks;
};

struct KubernetesScalingConfig
{
  struct Keda
  {
    bool enabled = false;
    std::string trigger = "prometheus";
    std::string query;
    int threshold = 100;
    int cooldown_seconds = 300;
  };

  int min_replicas = 1;
  int max_replicas = 10;
  int target_cpu_pct = 70;
  int target_memory_pct = 80;
  Keda keda;
};

struct KubernetesPersistenceConfig
{
  bool enabled = false;
  std::string storage_class = "standard";
  std::string size = "10Gi";
  std::string mount_path = "/data";
  std::string access_mode = "ReadWriteOnce";
};

struct KubernetesNetworkConfig
{
  bool ingress_enabled = false;
  std::string ingress_class = "nginx";
  std::string hostname;
  bool tls_enabled = false;
  std::string tls_secret;
  std::vector<std::string> allowed_cidrs;
};

struct KubernetesDisruptionConfig
{
  int min_available = 1;
  int max_unavailable = 0;
};

struct CloudConfig
{
  StateConfig state;
  LLMConfig llm;
  TelemetryConfig telemetry;
  SecretsConfig secrets;
  AwsDeployConfig aws;
  GcpDeployConfig gcp;
  AzureDeployConfig azure;
  KubernetesScalingConfig k8s_scaling;
  KubernetesPersistenceConfig k8s_persistence;
  KubernetesNetworkConfig k8s_network;
  KubernetesDisruptionConfig k8s_disruption;
};

CloudConfig parse_cloud_config(const std::string& toml_content);

}  // namespace neamc::config
