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
    int replicas = 2;
    std::string cpu_request = "250m";
    std::string cpu_limit = "1";
    std::string memory_request = "512Mi";
    std::string memory_limit = "1Gi";
    int port = 8080;
    // v0.6.8: EKS/ALB support
    std::string ingress_class = "alb";  // "alb" for AWS, "nginx" for generic
    std::string certificate_arn;        // ACM cert ARN for HTTPS
    int hpa_min = 2;
    int hpa_max = 10;
    int hpa_cpu_target = 70;
  } kubernetes;

  struct Serverless
  {
    std::string provider = "aws";
    int memory = 1024;
    int timeout = 300;
    int workers = 2;
    // v0.6.8: Lambda Web Adapter support
    std::string readiness_path = "/health";
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
