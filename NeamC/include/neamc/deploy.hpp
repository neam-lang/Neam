// SPDX-License-Identifier: Apache-2.0
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

// v0.6.0 cloud deployment targets
void generate_azure_container_apps(const ProjectManifest& manifest, const std::string& output_dir,
                                    bool dry_run = false);
void generate_azure_aks(const ProjectManifest& manifest, const std::string& output_dir,
                         bool dry_run = false);
void generate_ecs_fargate(const ProjectManifest& manifest, const std::string& output_dir,
                           bool dry_run = false);
void generate_docker_push(const DeployConfig& config);
}  // namespace neamc
