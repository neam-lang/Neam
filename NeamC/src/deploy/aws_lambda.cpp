// SPDX-License-Identifier: Apache-2.0
#include "neamc/deploy.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace neamc
{
void generate_sam_template(const DeployConfig& config, const std::string& app_name)
{
  namespace fs = std::filesystem;
  fs::create_directories("deploy/aws");

  std::ofstream file("deploy/aws/template.yaml");
  file << "AWSTemplateFormatVersion: '2010-09-09'\n";
  file << "Transform: AWS::Serverless-2016-10-31\n\n";
  file << "Globals:\n";
  file << "  Function:\n";
  file << "    Timeout: " << config.serverless.timeout << "\n";
  file << "    MemorySize: " << config.serverless.memory << "\n\n";
  file << "Resources:\n";
  file << "  AgentFunction:\n";
  file << "    Type: AWS::Serverless::Function\n";
  file << "    Properties:\n";
  file << "      Handler: bootstrap\n";
  file << "      Runtime: provided.al2\n";
  file << "      CodeUri: ../../target/release/\n";
  file << "      Events:\n";
  file << "        Api:\n";
  file << "          Type: Api\n";
  file << "          Properties:\n";
  file << "            Path: /{proxy+}\n";
  file << "            Method: ANY\n";

  std::cout << "Generated AWS SAM template in deploy/aws/template.yaml\n";
  (void)app_name;
}
}  // namespace neamc
