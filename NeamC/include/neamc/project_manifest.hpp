#pragma once

#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace neamc
{
struct AgentConfig
{
  struct Limits
  {
    int max_tokens_per_request = 4096;
    int max_concurrent_tools = 5;
    int timeout_seconds = 300;
    int max_retries = 3;
  } limits;

  struct Prompts
  {
    std::string system;
    std::string error_recovery;
  } prompts;

  std::string provider;
  std::string model;
  std::vector<std::string> capabilities;
};

struct TestConfig
{
  int timeout = 60;
  bool parallel = true;
  bool coverage = false;
  int coverage_threshold = 80;
  std::vector<std::string> include = {"tests/**/*.neam"};
  std::vector<std::string> exclude;
};

struct DeployConfig
{
  std::string default_target = "local";

  struct Docker
  {
    std::string image;
    std::string registry;
    std::string tag_format = "v{version}";
  } docker;

  struct Kubernetes
  {
    std::string namespace_ = "default";
    int replicas = 1;
    std::string cpu = "500m";
    std::string memory = "1Gi";
    int port = 8080;
  } kubernetes;

  struct Serverless
  {
    std::string provider;
    int memory = 1024;
    int timeout = 300;
  } serverless;
};

struct FeatureConfig
{
  std::vector<std::string> default_features;
  std::map<std::string, std::vector<std::string>> features;
};

struct DependencySpec
{
  std::string version;
  std::string git;
  std::vector<std::string> features;
};

struct ProjectManifest
{
  std::string name;
  std::string version;
  std::map<std::string, DependencySpec> dependencies;
  std::map<std::string, DependencySpec> dev_dependencies;
  std::map<std::string, std::string> scripts;
  AgentConfig agent;
  TestConfig test;
  DeployConfig deploy;
  FeatureConfig features;
};

ProjectManifest parse_project_manifest(const std::string& toml);
ProjectManifest load_project_manifest(const std::filesystem::path& path);

std::vector<DependencySpec> resolve_dependencies(const ProjectManifest& manifest,
                                                 const std::vector<std::string>& active_features);
}  // namespace neamc
