#include "neamc/deploy.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace neamc
{
void generate_cloudrun_service(const DeployConfig& config, const std::string& app_name)
{
  namespace fs = std::filesystem;
  fs::create_directories("deploy/cloudrun");

  std::ofstream file("deploy/cloudrun/service.yaml");
  file << "apiVersion: serving.knative.dev/v1\n";
  file << "kind: Service\n";
  file << "metadata:\n";
  file << "  name: " << app_name << "\n";
  file << "spec:\n";
  file << "  template:\n";
  file << "    spec:\n";
  file << "      containers:\n";
  file << "      - image: " << config.docker.image << "\n";
  file << "        env:\n";
  file << "        - name: NEAM_ENV\n";
  file << "          value: production\n";

  std::cout << "Generated Cloud Run service in deploy/cloudrun/service.yaml\n";
}
}  // namespace neamc
