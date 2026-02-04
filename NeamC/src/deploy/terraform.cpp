// SPDX-License-Identifier: Apache-2.0
#include "neamc/deploy.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace neamc
{
void generate_terraform(const DeployConfig::Kubernetes& config, const std::string& app_name,
                        const std::string& image)
{
  namespace fs = std::filesystem;
  fs::create_directories("deploy/terraform");

  std::ofstream main("deploy/terraform/main.tf");
  main << R"(terraform {
  required_providers {
    kubernetes = {
      source  = "hashicorp/kubernetes"
      version = "~> 2.0"
    }
  }
}

provider "kubernetes" {
  config_path = "~/.kube/config"
}

resource "kubernetes_namespace" "agent" {
  metadata {
    name = var.namespace
  }
}

resource "kubernetes_deployment" "agent" {
  metadata {
    name      = var.app_name
    namespace = kubernetes_namespace.agent.metadata[0].name
  }

  spec {
    replicas = var.replicas
    selector {
      match_labels = {
        app = var.app_name
      }
    }
    template {
      metadata {
        labels = {
          app = var.app_name
        }
      }
      spec {
        container {
          name  = var.app_name
          image = var.image
          port {
            container_port = var.port
          }
        }
      }
    }
  }
}
)";

  std::ofstream vars("deploy/terraform/variables.tf");
  vars << "variable \"app_name\" {\n";
  vars << "  description = \"Application name\"\n";
  vars << "  type        = string\n";
  vars << "}\n\n";
  vars << "variable \"namespace\" {\n";
  vars << "  description = \"Kubernetes namespace\"\n";
  vars << "  type        = string\n";
  vars << "  default     = \"" << config.namespace_ << "\"\n";
  vars << "}\n\n";
  vars << "variable \"replicas\" {\n";
  vars << "  description = \"Number of replicas\"\n";
  vars << "  type        = number\n";
  vars << "  default     = " << config.replicas << "\n";
  vars << "}\n\n";
  vars << "variable \"image\" {\n";
  vars << "  description = \"Container image\"\n";
  vars << "  type        = string\n";
  vars << "  default     = \"" << image << "\"\n";
  vars << "}\n\n";
  vars << "variable \"port\" {\n";
  vars << "  description = \"Container port\"\n";
  vars << "  type        = number\n";
  vars << "  default     = " << config.port << "\n";
  vars << "}\n";

  std::cout << "Generated Terraform module in deploy/terraform/\n";
  (void)app_name;
}
}  // namespace neamc
