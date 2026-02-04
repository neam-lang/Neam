// SPDX-License-Identifier: Apache-2.0
#include "neamc/deploy.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace neamc
{
void generate_helm_chart(const DeployConfig::Kubernetes& config, const std::string& app_name,
                         const std::string& image)
{
  namespace fs = std::filesystem;
  std::string chart_dir = "deploy/helm/" + app_name;
  fs::create_directories(chart_dir + "/templates");

  std::ofstream chart(chart_dir + "/Chart.yaml");
  chart << "apiVersion: v2\n";
  chart << "name: " << app_name << "\n";
  chart << "version: 0.1.0\n";
  chart << "appVersion: \"1.0.0\"\n";

  std::ofstream values(chart_dir + "/values.yaml");
  values << "replicaCount: " << config.replicas << "\n";
  values << "image:\n";
  values << "  repository: " << image << "\n";
  values << "  tag: latest\n";
  values << "service:\n";
  values << "  port: " << config.port << "\n";

  std::ofstream deployment(chart_dir + "/templates/deployment.yaml");
  deployment << "apiVersion: apps/v1\n";
  deployment << "kind: Deployment\n";
  deployment << "metadata:\n";
  deployment << "  name: {{ include \"" << app_name << ".fullname\" . }}\n";
  deployment << "spec:\n";
  deployment << "  replicas: {{ .Values.replicaCount }}\n";
  deployment << "  selector:\n";
  deployment << "    matchLabels:\n";
  deployment << "      app: {{ include \"" << app_name << ".name\" . }}\n";
  deployment << "  template:\n";
  deployment << "    metadata:\n";
  deployment << "      labels:\n";
  deployment << "        app: {{ include \"" << app_name << ".name\" . }}\n";
  deployment << "    spec:\n";
  deployment << "      containers:\n";
  deployment << "      - name: {{ include \"" << app_name << ".name\" . }}\n";
  deployment << "        image: \"{{ .Values.image.repository }}:{{ .Values.image.tag }}\"\n";
  deployment << "        ports:\n";
  deployment << "        - containerPort: {{ .Values.service.port }}\n";

  std::ofstream service(chart_dir + "/templates/service.yaml");
  service << "apiVersion: v1\n";
  service << "kind: Service\n";
  service << "metadata:\n";
  service << "  name: {{ include \"" << app_name << ".fullname\" . }}\n";
  service << "spec:\n";
  service << "  selector:\n";
  service << "    app: {{ include \"" << app_name << ".name\" . }}\n";
  service << "  ports:\n";
  service << "  - port: 80\n";
  service << "    targetPort: {{ .Values.service.port }}\n";

  std::ofstream helpers(chart_dir + "/templates/_helpers.tpl");
  helpers << "{{- define \"" << app_name << ".name\" -}}\n";
  helpers << app_name << "\n";
  helpers << "{{- end -}}\n";
  helpers << "{{- define \"" << app_name << ".fullname\" -}}\n";
  helpers << app_name << "-{{ .Release.Name }}\n";
  helpers << "{{- end -}}\n";

  std::cout << "Generated Helm chart in " << chart_dir << "\n";
}
}  // namespace neamc
