#pragma once

#include <string>

#include "neamc/project_manifest.hpp"

namespace neamc
{
void generate_dockerfile(const DeployConfig& config);
void generate_k8s_manifests(const DeployConfig::Kubernetes& config, const std::string& app_name,
                            const std::string& image);
void generate_helm_chart(const DeployConfig::Kubernetes& config, const std::string& app_name,
                         const std::string& image);
void generate_terraform(const DeployConfig::Kubernetes& config, const std::string& app_name,
                        const std::string& image);
void generate_sam_template(const DeployConfig& config, const std::string& app_name);
void generate_cloudrun_service(const DeployConfig& config, const std::string& app_name);
}  // namespace neamc
