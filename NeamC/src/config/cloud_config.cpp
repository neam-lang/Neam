// SPDX-License-Identifier: Apache-2.0
//
// Neam v0.6.0 - Cloud Configuration TOML Parser
//

#include "neamc/config/cloud_config.hpp"

#include <algorithm>
#include <cctype>
#include <sstream>

namespace neamc::config
{
namespace
{
std::string trim(const std::string& value)
{
  std::size_t start = 0;
  while (start < value.size() && std::isspace(static_cast<unsigned char>(value[start])))
  {
    ++start;
  }
  std::size_t end = value.size();
  while (end > start && std::isspace(static_cast<unsigned char>(value[end - 1])))
  {
    --end;
  }
  return value.substr(start, end - start);
}

std::string unquote(const std::string& value)
{
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
  {
    return value.substr(1, value.size() - 2);
  }
  return value;
}

int parse_int(const std::string& value, int fallback)
{
  try
  {
    return std::stoi(value);
  }
  catch (const std::exception&)
  {
    return fallback;
  }
}

double parse_double(const std::string& value, double fallback)
{
  try
  {
    return std::stod(value);
  }
  catch (const std::exception&)
  {
    return fallback;
  }
}

bool parse_bool(const std::string& value, bool fallback)
{
  if (value == "true")
  {
    return true;
  }
  if (value == "false")
  {
    return false;
  }
  return fallback;
}

std::vector<std::string> parse_array(const std::string& value)
{
  std::vector<std::string> result;
  std::string trimmed = trim(value);
  if (trimmed.size() < 2 || trimmed.front() != '[' || trimmed.back() != ']')
  {
    return result;
  }
  std::string content = trimmed.substr(1, trimmed.size() - 2);
  std::string token;
  bool in_string = false;
  for (char ch : content)
  {
    if (ch == '"')
    {
      in_string = !in_string;
      continue;
    }
    if (ch == ',' && !in_string)
    {
      const auto cleaned = trim(token);
      if (!cleaned.empty())
      {
        result.push_back(unquote(cleaned));
      }
      token.clear();
    }
    else
    {
      token.push_back(ch);
    }
  }
  const auto cleaned = trim(token);
  if (!cleaned.empty())
  {
    result.push_back(unquote(cleaned));
  }
  return result;
}

// Extract rate-limit subsection name from section header
// e.g. "llm.rate-limits.openai" -> "openai"
std::string extract_rate_limit_name(const std::string& section)
{
  const std::string prefix = "llm.rate-limits.";
  if (section.size() > prefix.size() && section.substr(0, prefix.size()) == prefix)
  {
    return section.substr(prefix.size());
  }
  return "";
}
}  // namespace

CloudConfig parse_cloud_config(const std::string& toml_content)
{
  CloudConfig cfg;
  std::string section;
  std::istringstream input(toml_content);
  std::string line;

  while (std::getline(input, line))
  {
    auto trimmed = trim(line);
    if (trimmed.empty() || trimmed[0] == '#')
    {
      continue;
    }
    if (trimmed.front() == '[' && trimmed.back() == ']')
    {
      section = trimmed.substr(1, trimmed.size() - 2);
      continue;
    }

    const auto eq_pos = trimmed.find('=');
    if (eq_pos == std::string::npos)
    {
      continue;
    }
    const std::string key = trim(trimmed.substr(0, eq_pos));
    const std::string value = trim(trimmed.substr(eq_pos + 1));

    // --- [state] ---
    if (section == "state")
    {
      if (key == "backend") cfg.state.backend = unquote(value);
      else if (key == "bucket") cfg.state.bucket = unquote(value);
      else if (key == "prefix") cfg.state.prefix = unquote(value);
      else if (key == "lock-table") cfg.state.lock_table = unquote(value);
      else if (key == "ttl-seconds") cfg.state.ttl_seconds = parse_int(value, 3600);
    }
    // --- [llm] ---
    else if (section == "llm")
    {
      if (key == "provider") cfg.llm.provider = unquote(value);
      else if (key == "model") cfg.llm.model = unquote(value);
      else if (key == "api-key-ref") cfg.llm.api_key_ref = unquote(value);
    }
    // --- [llm.rate-limits.*] ---
    else if (section.rfind("llm.rate-limits.", 0) == 0)
    {
      const auto name = extract_rate_limit_name(section);
      if (!name.empty())
      {
        auto& rl = cfg.llm.rate_limits[name];
        if (key == "requests-per-minute") rl.requests_per_minute = parse_int(value, 60);
        else if (key == "tokens-per-minute") rl.tokens_per_minute = parse_int(value, 100000);
        else if (key == "burst") rl.burst = parse_int(value, 10);
      }
    }
    // --- [llm.circuit-breaker] ---
    else if (section == "llm.circuit-breaker")
    {
      if (key == "failure-threshold") cfg.llm.circuit_breaker.failure_threshold = parse_int(value, 5);
      else if (key == "reset-timeout-seconds") cfg.llm.circuit_breaker.reset_timeout_seconds = parse_int(value, 30);
      else if (key == "half-open-requests") cfg.llm.circuit_breaker.half_open_requests = parse_int(value, 2);
    }
    // --- [llm.cache] ---
    else if (section == "llm.cache")
    {
      if (key == "enabled") cfg.llm.cache.enabled = parse_bool(value, false);
      else if (key == "backend") cfg.llm.cache.backend = unquote(value);
      else if (key == "ttl-seconds") cfg.llm.cache.ttl_seconds = parse_int(value, 300);
      else if (key == "max-entries") cfg.llm.cache.max_entries = parse_int(value, 1000);
    }
    // --- [llm.cost] ---
    else if (section == "llm.cost")
    {
      if (key == "budget-usd") cfg.llm.cost.budget_usd = parse_double(value, 0.0);
      else if (key == "period") cfg.llm.cost.period = unquote(value);
      else if (key == "alert-enabled") cfg.llm.cost.alert_enabled = parse_bool(value, false);
      else if (key == "alert-threshold-pct") cfg.llm.cost.alert_threshold_pct = parse_double(value, 80.0);
    }
    // --- [telemetry] ---
    else if (section == "telemetry")
    {
      if (key == "enabled") cfg.telemetry.enabled = parse_bool(value, false);
      else if (key == "exporter") cfg.telemetry.exporter = unquote(value);
      else if (key == "endpoint") cfg.telemetry.endpoint = unquote(value);
      else if (key == "service-name") cfg.telemetry.service_name = unquote(value);
      else if (key == "flush-interval-ms") cfg.telemetry.flush_interval_ms = parse_int(value, 5000);
      else if (key == "sampling-rate") cfg.telemetry.sampling_rate = parse_double(value, 1.0);
    }
    // --- [secrets] ---
    else if (section == "secrets")
    {
      if (key == "provider") cfg.secrets.provider = unquote(value);
      else if (key == "vault-addr") cfg.secrets.vault_addr = unquote(value);
      else if (key == "vault-path") cfg.secrets.vault_path = unquote(value);
      else if (key == "aws-region") cfg.secrets.aws_region = unquote(value);
      else if (key == "azure-vault-url") cfg.secrets.azure_vault_url = unquote(value);
      else if (key == "gcp-project") cfg.secrets.gcp_project = unquote(value);
      else if (key == "k8s-namespace") cfg.secrets.k8s_namespace = unquote(value);
    }
    // --- [deploy.aws] ---
    else if (section == "deploy.aws")
    {
      if (key == "region") cfg.aws.region = unquote(value);
    }
    // --- [deploy.aws.lambda] ---
    else if (section == "deploy.aws.lambda")
    {
      if (key == "function-name") cfg.aws.lambda.function_name = unquote(value);
      else if (key == "memory") cfg.aws.lambda.memory = parse_int(value, 1024);
      else if (key == "timeout") cfg.aws.lambda.timeout = parse_int(value, 300);
      else if (key == "runtime") cfg.aws.lambda.runtime = unquote(value);
      else if (key == "architecture") cfg.aws.lambda.architecture = unquote(value);
    }
    // --- [deploy.aws.ecs] ---
    else if (section == "deploy.aws.ecs")
    {
      if (key == "cluster") cfg.aws.ecs.cluster = unquote(value);
      else if (key == "service") cfg.aws.ecs.service = unquote(value);
      else if (key == "desired-count") cfg.aws.ecs.desired_count = parse_int(value, 1);
      else if (key == "cpu") cfg.aws.ecs.cpu = unquote(value);
      else if (key == "memory") cfg.aws.ecs.memory = unquote(value);
    }
    // --- [deploy.gcp] ---
    else if (section == "deploy.gcp")
    {
      if (key == "project") cfg.gcp.project = unquote(value);
    }
    // --- [deploy.gcp.cloud-run] ---
    else if (section == "deploy.gcp.cloud-run")
    {
      if (key == "service") cfg.gcp.cloud_run.service = unquote(value);
      else if (key == "region") cfg.gcp.cloud_run.region = unquote(value);
      else if (key == "min-instances") cfg.gcp.cloud_run.min_instances = parse_int(value, 0);
      else if (key == "max-instances") cfg.gcp.cloud_run.max_instances = parse_int(value, 10);
      else if (key == "memory") cfg.gcp.cloud_run.memory = unquote(value);
      else if (key == "cpu") cfg.gcp.cloud_run.cpu = unquote(value);
      else if (key == "concurrency") cfg.gcp.cloud_run.concurrency = parse_int(value, 80);
    }
    // --- [deploy.azure] ---
    else if (section == "deploy.azure")
    {
      if (key == "subscription-id") cfg.azure.subscription_id = unquote(value);
      else if (key == "resource-group") cfg.azure.resource_group = unquote(value);
    }
    // --- [deploy.azure.container-apps] ---
    else if (section == "deploy.azure.container-apps")
    {
      if (key == "environment") cfg.azure.container_apps.environment = unquote(value);
      else if (key == "name") cfg.azure.container_apps.name = unquote(value);
      else if (key == "min-replicas") cfg.azure.container_apps.min_replicas = parse_int(value, 0);
      else if (key == "max-replicas") cfg.azure.container_apps.max_replicas = parse_int(value, 10);
      else if (key == "cpu") cfg.azure.container_apps.cpu = unquote(value);
      else if (key == "memory") cfg.azure.container_apps.memory = unquote(value);
    }
    // --- [deploy.azure.aks] ---
    else if (section == "deploy.azure.aks")
    {
      if (key == "cluster") cfg.azure.aks.cluster = unquote(value);
      else if (key == "resource-group") cfg.azure.aks.resource_group = unquote(value);
      else if (key == "node-pool") cfg.azure.aks.node_pool = unquote(value);
    }
    // --- [deploy.kubernetes.scaling] ---
    else if (section == "deploy.kubernetes.scaling")
    {
      if (key == "min-replicas") cfg.k8s_scaling.min_replicas = parse_int(value, 1);
      else if (key == "max-replicas") cfg.k8s_scaling.max_replicas = parse_int(value, 10);
      else if (key == "target-cpu-pct") cfg.k8s_scaling.target_cpu_pct = parse_int(value, 70);
      else if (key == "target-memory-pct") cfg.k8s_scaling.target_memory_pct = parse_int(value, 80);
    }
    // --- [deploy.kubernetes.scaling.keda] ---
    else if (section == "deploy.kubernetes.scaling.keda")
    {
      if (key == "enabled") cfg.k8s_scaling.keda.enabled = parse_bool(value, false);
      else if (key == "trigger") cfg.k8s_scaling.keda.trigger = unquote(value);
      else if (key == "query") cfg.k8s_scaling.keda.query = unquote(value);
      else if (key == "threshold") cfg.k8s_scaling.keda.threshold = parse_int(value, 100);
      else if (key == "cooldown-seconds") cfg.k8s_scaling.keda.cooldown_seconds = parse_int(value, 300);
    }
    // --- [deploy.kubernetes.persistence] ---
    else if (section == "deploy.kubernetes.persistence")
    {
      if (key == "enabled") cfg.k8s_persistence.enabled = parse_bool(value, false);
      else if (key == "storage-class") cfg.k8s_persistence.storage_class = unquote(value);
      else if (key == "size") cfg.k8s_persistence.size = unquote(value);
      else if (key == "mount-path") cfg.k8s_persistence.mount_path = unquote(value);
      else if (key == "access-mode") cfg.k8s_persistence.access_mode = unquote(value);
    }
    // --- [deploy.kubernetes.network] ---
    else if (section == "deploy.kubernetes.network")
    {
      if (key == "ingress-enabled") cfg.k8s_network.ingress_enabled = parse_bool(value, false);
      else if (key == "ingress-class") cfg.k8s_network.ingress_class = unquote(value);
      else if (key == "hostname") cfg.k8s_network.hostname = unquote(value);
      else if (key == "tls-enabled") cfg.k8s_network.tls_enabled = parse_bool(value, false);
      else if (key == "tls-secret") cfg.k8s_network.tls_secret = unquote(value);
      else if (key == "allowed-cidrs") cfg.k8s_network.allowed_cidrs = parse_array(value);
    }
    // --- [deploy.kubernetes.disruption] ---
    else if (section == "deploy.kubernetes.disruption")
    {
      if (key == "min-available") cfg.k8s_disruption.min_available = parse_int(value, 1);
      else if (key == "max-unavailable") cfg.k8s_disruption.max_unavailable = parse_int(value, 0);
    }
  }

  return cfg;
}

}  // namespace neamc::config
