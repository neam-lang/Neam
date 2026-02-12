#include "neamc/project_manifest.hpp"

#include <algorithm>
#include <cctype>
#include <fstream>
#include <sstream>

namespace neamc
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

DependencySpec parse_dependency_spec(const std::string& value)
{
  DependencySpec spec;
  const auto trimmed = trim(value);
  if (!trimmed.empty() && trimmed.front() == '{')
  {
    const auto version_pos = trimmed.find("version");
    if (version_pos != std::string::npos)
    {
      const auto quote_start = trimmed.find('"', version_pos);
      const auto quote_end = trimmed.find('"', quote_start + 1);
      if (quote_start != std::string::npos && quote_end != std::string::npos)
      {
        spec.version = trimmed.substr(quote_start + 1, quote_end - quote_start - 1);
      }
    }
    const auto git_pos = trimmed.find("git");
    if (git_pos != std::string::npos)
    {
      const auto quote_start = trimmed.find('"', git_pos);
      const auto quote_end = trimmed.find('"', quote_start + 1);
      if (quote_start != std::string::npos && quote_end != std::string::npos)
      {
        spec.git = trimmed.substr(quote_start + 1, quote_end - quote_start - 1);
      }
    }
    const auto features_pos = trimmed.find("features");
    if (features_pos != std::string::npos)
    {
      const auto array_start = trimmed.find('[', features_pos);
      const auto array_end = trimmed.find(']', array_start);
      if (array_start != std::string::npos && array_end != std::string::npos)
      {
        spec.features = parse_array(trimmed.substr(array_start, array_end - array_start + 1));
      }
    }
  }
  else
  {
    spec.version = unquote(trimmed);
  }
  return spec;
}
}  // namespace

ProjectManifest parse_project_manifest(const std::string& toml)
{
  ProjectManifest manifest;
  std::string section;
  std::string array_table;  // v0.7.2: tracks [[agents]] context
  std::istringstream input(toml);
  std::string line;
  while (std::getline(input, line))
  {
    auto trimmed = trim(line);
    if (trimmed.empty() || trimmed[0] == '#')
    {
      continue;
    }

    // v0.7.2: Detect [[array-of-tables]] (double brackets)
    if (trimmed.size() >= 4 && trimmed[0] == '[' && trimmed[1] == '[' &&
        trimmed[trimmed.size() - 1] == ']' && trimmed[trimmed.size() - 2] == ']')
    {
      array_table = trimmed.substr(2, trimmed.size() - 4);
      array_table = trim(array_table);
      section.clear();  // array-of-tables overrides section
      if (array_table == "agents")
      {
        manifest.agents.emplace_back();  // Start new agent entry
      }
      continue;
    }

    if (trimmed.front() == '[' && trimmed.back() == ']')
    {
      section = trimmed.substr(1, trimmed.size() - 2);
      array_table.clear();  // regular section clears array-of-tables
      continue;
    }

    const auto eq_pos = trimmed.find('=');
    if (eq_pos == std::string::npos)
    {
      continue;
    }
    const std::string key = trim(trimmed.substr(0, eq_pos));
    const std::string value = trim(trimmed.substr(eq_pos + 1));

    if (section == "package")
    {
      if (key == "name")
      {
        manifest.name = unquote(value);
      }
      else if (key == "version")
      {
        manifest.version = unquote(value);
      }
    }
    else if (section == "dependencies")
    {
      manifest.dependencies[key] = parse_dependency_spec(value);
    }
    else if (section == "dev-dependencies")
    {
      manifest.dev_dependencies[key] = parse_dependency_spec(value);
    }
    else if (section == "scripts")
    {
      manifest.scripts[key] = unquote(value);
    }
    else if (section == "agent")
    {
      if (key == "provider")
      {
        manifest.agent.provider = unquote(value);
      }
      else if (key == "model")
      {
        manifest.agent.model = unquote(value);
      }
      else if (key == "capabilities")
      {
        manifest.agent.capabilities = parse_array(value);
      }
    }
    else if (section == "agent.limits")
    {
      if (key == "max-tokens-per-request")
      {
        manifest.agent.limits.max_tokens_per_request = parse_int(value, 4096);
      }
      else if (key == "max-concurrent-tools")
      {
        manifest.agent.limits.max_concurrent_tools = parse_int(value, 5);
      }
      else if (key == "timeout-seconds")
      {
        manifest.agent.limits.timeout_seconds = parse_int(value, 300);
      }
      else if (key == "max-retries")
      {
        manifest.agent.limits.max_retries = parse_int(value, 3);
      }
    }
    else if (section == "agent.prompts")
    {
      if (key == "system")
      {
        manifest.agent.prompts.system = unquote(value);
      }
      else if (key == "error-recovery")
      {
        manifest.agent.prompts.error_recovery = unquote(value);
      }
    }
    else if (section == "test")
    {
      if (key == "timeout")
      {
        manifest.test.timeout = parse_int(value, 60);
      }
      else if (key == "parallel")
      {
        manifest.test.parallel = parse_bool(value, true);
      }
      else if (key == "coverage")
      {
        manifest.test.coverage = parse_bool(value, false);
      }
      else if (key == "coverage-threshold")
      {
        manifest.test.coverage_threshold = parse_int(value, 80);
      }
      else if (key == "include")
      {
        manifest.test.include = parse_array(value);
      }
      else if (key == "exclude")
      {
        manifest.test.exclude = parse_array(value);
      }
    }
    else if (section == "deploy")
    {
      if (key == "default-target")
      {
        manifest.deploy.default_target = unquote(value);
      }
    }
    else if (section == "deploy.docker")
    {
      if (key == "image")
      {
        manifest.deploy.docker.image = unquote(value);
      }
      else if (key == "registry")
      {
        manifest.deploy.docker.registry = unquote(value);
      }
      else if (key == "tag-format")
      {
        manifest.deploy.docker.tag_format = unquote(value);
      }
    }
    else if (section == "deploy.kubernetes")
    {
      if (key == "namespace")
      {
        manifest.deploy.kubernetes.namespace_ = unquote(value);
      }
      else if (key == "replicas")
      {
        manifest.deploy.kubernetes.replicas = parse_int(value, 2);
      }
      else if (key == "cpu" || key == "cpu-request")
      {
        manifest.deploy.kubernetes.cpu_request = unquote(value);
      }
      else if (key == "cpu-limit")
      {
        manifest.deploy.kubernetes.cpu_limit = unquote(value);
      }
      else if (key == "memory" || key == "memory-request")
      {
        manifest.deploy.kubernetes.memory_request = unquote(value);
      }
      else if (key == "memory-limit")
      {
        manifest.deploy.kubernetes.memory_limit = unquote(value);
      }
      else if (key == "port")
      {
        manifest.deploy.kubernetes.port = parse_int(value, 8080);
      }
      else if (key == "ingress-class")
      {
        manifest.deploy.kubernetes.ingress_class = unquote(value);
      }
      else if (key == "certificate-arn")
      {
        manifest.deploy.kubernetes.certificate_arn = unquote(value);
      }
      else if (key == "hpa-min")
      {
        manifest.deploy.kubernetes.hpa_min = parse_int(value, 2);
      }
      else if (key == "hpa-max")
      {
        manifest.deploy.kubernetes.hpa_max = parse_int(value, 10);
      }
      else if (key == "hpa-cpu-target")
      {
        manifest.deploy.kubernetes.hpa_cpu_target = parse_int(value, 70);
      }
    }
    else if (section == "deploy.serverless")
    {
      if (key == "provider")
      {
        manifest.deploy.serverless.provider = unquote(value);
      }
      else if (key == "memory")
      {
        manifest.deploy.serverless.memory = parse_int(value, 1024);
      }
      else if (key == "timeout")
      {
        manifest.deploy.serverless.timeout = parse_int(value, 300);
      }
      else if (key == "workers")
      {
        manifest.deploy.serverless.workers = parse_int(value, 2);
      }
      else if (key == "readiness-path")
      {
        manifest.deploy.serverless.readiness_path = unquote(value);
      }
      else if (key == "provisioned-concurrency")
      {
        manifest.deploy.serverless.provisioned_concurrency = parse_int(value, 0);
      }
    }
    // v0.7.2: Parse [[agents]] array-of-tables entries
    else if (array_table == "agents" && !manifest.agents.empty())
    {
      auto& agent = manifest.agents.back();
      if (key == "id")
      {
        agent.id = unquote(value);
      }
      else if (key == "name")
      {
        agent.name = unquote(value);
      }
      else if (key == "provider")
      {
        agent.provider = unquote(value);
      }
      else if (key == "model")
      {
        agent.model = unquote(value);
      }
      else if (key == "system-prompt")
      {
        agent.system_prompt = unquote(value);
      }
      else if (key == "source")
      {
        agent.source = unquote(value);
      }
      else if (key == "knowledge-base")
      {
        agent.knowledge_base = unquote(value);
      }
    }
    else if (section == "features")
    {
      if (key == "default")
      {
        manifest.features.default_features = parse_array(value);
      }
      else
      {
        manifest.features.features[key] = parse_array(value);
      }
    }
  }

  return manifest;
}

ProjectManifest load_project_manifest(const std::filesystem::path& path)
{
  std::ifstream input(path);
  if (!input)
  {
    return {};
  }
  std::ostringstream buffer;
  buffer << input.rdbuf();
  return parse_project_manifest(buffer.str());
}

std::vector<DependencySpec> resolve_dependencies(const ProjectManifest& manifest,
                                                 const std::vector<std::string>& active_features)
{
  std::vector<DependencySpec> result;
  for (const auto& [name, spec] : manifest.dependencies)
  {
    bool enabled = true;
    for (const auto& [feature, deps] : manifest.features.features)
    {
      if (std::find(deps.begin(), deps.end(), name) != deps.end())
      {
        if (std::find(active_features.begin(), active_features.end(), feature) ==
            active_features.end())
        {
          enabled = false;
        }
      }
    }
    if (enabled)
    {
      result.push_back(spec);
    }
  }
  return result;
}
}  // namespace neamc
