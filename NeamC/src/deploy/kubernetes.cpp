// SPDX-License-Identifier: Apache-2.0
#include "neamc/deploy.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace neamc
{
void generate_k8s_manifests(const DeployConfig::Kubernetes& config, const std::string& app_name,
                            const std::string& image)
{
  namespace fs = std::filesystem;
  fs::create_directories("deploy/kubernetes");

  std::ofstream deployment("deploy/kubernetes/deployment.yaml");
  deployment << "apiVersion: apps/v1\n";
  deployment << "kind: Deployment\n";
  deployment << "metadata:\n";
  deployment << "  name: " << app_name << "\n";
  deployment << "  namespace: " << config.namespace_ << "\n";
  deployment << "  labels:\n";
  deployment << "    app: " << app_name << "\n";
  deployment << "spec:\n";
  deployment << "  replicas: " << config.replicas << "\n";
  deployment << "  selector:\n";
  deployment << "    matchLabels:\n";
  deployment << "      app: " << app_name << "\n";
  deployment << "  template:\n";
  deployment << "    metadata:\n";
  deployment << "      labels:\n";
  deployment << "        app: " << app_name << "\n";
  deployment << "    spec:\n";
  deployment << "      containers:\n";
  deployment << "      - name: " << app_name << "\n";
  deployment << "        image: " << image << "\n";
  deployment << "        ports:\n";
  deployment << "        - containerPort: " << config.port << "\n";
  deployment << "        resources:\n";
  deployment << "          requests:\n";
  deployment << "            cpu: " << config.cpu << "\n";
  deployment << "            memory: " << config.memory << "\n";
  deployment << "          limits:\n";
  deployment << "            cpu: " << config.cpu << "\n";
  deployment << "            memory: " << config.memory << "\n";
  deployment << "        env:\n";
  deployment << "        - name: NEAM_ENV\n";
  deployment << "          valueFrom:\n";
  deployment << "            configMapKeyRef:\n";
  deployment << "              name: " << app_name << "-config\n";
  deployment << "              key: NEAM_ENV\n";
  deployment << "        envFrom:\n";
  deployment << "        - secretRef:\n";
  deployment << "            name: " << app_name << "-secrets\n";
  deployment << "        livenessProbe:\n";
  deployment << "          httpGet:\n";
  deployment << "            path: /health\n";
  deployment << "            port: " << config.port << "\n";
  deployment << "          initialDelaySeconds: 10\n";
  deployment << "          periodSeconds: 10\n";
  deployment << "        readinessProbe:\n";
  deployment << "          httpGet:\n";
  deployment << "            path: /ready\n";
  deployment << "            port: " << config.port << "\n";
  deployment << "          initialDelaySeconds: 5\n";
  deployment << "          periodSeconds: 5\n";

  std::ofstream service("deploy/kubernetes/service.yaml");
  service << "apiVersion: v1\n";
  service << "kind: Service\n";
  service << "metadata:\n";
  service << "  name: " << app_name << "\n";
  service << "  namespace: " << config.namespace_ << "\n";
  service << "spec:\n";
  service << "  selector:\n";
  service << "    app: " << app_name << "\n";
  service << "  ports:\n";
  service << "  - port: 80\n";
  service << "    targetPort: " << config.port << "\n";
  service << "  type: ClusterIP\n";

  std::ofstream configmap("deploy/kubernetes/configmap.yaml");
  configmap << "apiVersion: v1\n";
  configmap << "kind: ConfigMap\n";
  configmap << "metadata:\n";
  configmap << "  name: " << app_name << "-config\n";
  configmap << "  namespace: " << config.namespace_ << "\n";
  configmap << "data:\n";
  configmap << "  NEAM_ENV: \"production\"\n";
  configmap << "  NEAM_LOG_LEVEL: \"info\"\n";
  configmap << "  NEAM_TELEMETRY: \"true\"\n";

  std::ofstream ingress("deploy/kubernetes/ingress.yaml");
  ingress << "apiVersion: networking.k8s.io/v1\n";
  ingress << "kind: Ingress\n";
  ingress << "metadata:\n";
  ingress << "  name: " << app_name << "\n";
  ingress << "  namespace: " << config.namespace_ << "\n";
  ingress << "  annotations:\n";
  ingress << "    kubernetes.io/ingress.class: nginx\n";
  ingress << "spec:\n";
  ingress << "  rules:\n";
  ingress << "  - host: " << app_name << ".example.com\n";
  ingress << "    http:\n";
  ingress << "      paths:\n";
  ingress << "      - path: /\n";
  ingress << "        pathType: Prefix\n";
  ingress << "        backend:\n";
  ingress << "          service:\n";
  ingress << "            name: " << app_name << "\n";
  ingress << "            port:\n";
  ingress << "              number: 80\n";

  std::cout << "Generated Kubernetes manifests in deploy/kubernetes/\n";
}
}  // namespace neamc
