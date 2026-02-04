// SPDX-License-Identifier: Apache-2.0
#include "neamc/deploy.hpp"

#include <filesystem>
#include <fstream>
#include <iostream>

namespace neamc
{
void generate_ecs_fargate(const ProjectManifest& manifest, const std::string& output_dir,
                           bool dry_run)
{
  namespace fs = std::filesystem;
  const std::string dir = output_dir.empty() ? "deploy/ecs" : output_dir + "/ecs";
  fs::create_directories(dir);

  const auto& app_name = manifest.name;
  const auto& config = manifest.deploy;
  const auto& k8s = config.kubernetes;

  // Generate ECS Task Definition
  {
    std::ofstream file(dir + "/task-definition.json");
    file << "{\n";
    file << "  \"family\": \"" << app_name << "\",\n";
    file << "  \"networkMode\": \"awsvpc\",\n";
    file << "  \"requiresCompatibilities\": [\"FARGATE\"],\n";
    file << "  \"cpu\": \"512\",\n";
    file << "  \"memory\": \"1024\",\n";
    file << "  \"executionRoleArn\": \"arn:aws:iam::<ACCOUNT_ID>:role/" << app_name
         << "-execution-role\",\n";
    file << "  \"taskRoleArn\": \"arn:aws:iam::<ACCOUNT_ID>:role/" << app_name
         << "-task-role\",\n";
    file << "  \"containerDefinitions\": [\n";
    file << "    {\n";
    file << "      \"name\": \"" << app_name << "\",\n";
    file << "      \"image\": \"" << config.docker.image << "\",\n";
    file << "      \"essential\": true,\n";
    file << "      \"portMappings\": [\n";
    file << "        {\n";
    file << "          \"containerPort\": " << k8s.port << ",\n";
    file << "          \"protocol\": \"tcp\"\n";
    file << "        }\n";
    file << "      ],\n";
    file << "      \"environment\": [\n";
    file << "        {\n";
    file << "          \"name\": \"NEAM_ENV\",\n";
    file << "          \"value\": \"production\"\n";
    file << "        },\n";
    file << "        {\n";
    file << "          \"name\": \"NEAM_LOG_LEVEL\",\n";
    file << "          \"value\": \"info\"\n";
    file << "        }\n";
    file << "      ],\n";
    file << "      \"secrets\": [\n";
    file << "        {\n";
    file << "          \"name\": \"NEAM_API_KEY\",\n";
    file << "          \"valueFrom\": \"arn:aws:secretsmanager:<REGION>:<ACCOUNT_ID>:secret:"
         << app_name << "-api-key\"\n";
    file << "        }\n";
    file << "      ],\n";
    file << "      \"healthCheck\": {\n";
    file << "        \"command\": [\"CMD-SHELL\", \"curl -f http://localhost:" << k8s.port
         << "/health || exit 1\"],\n";
    file << "        \"interval\": 30,\n";
    file << "        \"timeout\": 5,\n";
    file << "        \"retries\": 3,\n";
    file << "        \"startPeriod\": 10\n";
    file << "      },\n";
    file << "      \"logConfiguration\": {\n";
    file << "        \"logDriver\": \"awslogs\",\n";
    file << "        \"options\": {\n";
    file << "          \"awslogs-group\": \"/ecs/" << app_name << "\",\n";
    file << "          \"awslogs-region\": \"<REGION>\",\n";
    file << "          \"awslogs-stream-prefix\": \"ecs\"\n";
    file << "        }\n";
    file << "      }\n";
    file << "    }\n";
    file << "  ]\n";
    file << "}\n";
  }

  // Generate ECS Service config
  {
    std::ofstream file(dir + "/service.json");
    file << "{\n";
    file << "  \"cluster\": \"" << app_name << "-cluster\",\n";
    file << "  \"serviceName\": \"" << app_name << "\",\n";
    file << "  \"taskDefinition\": \"" << app_name << "\",\n";
    file << "  \"desiredCount\": " << k8s.replicas << ",\n";
    file << "  \"launchType\": \"FARGATE\",\n";
    file << "  \"platformVersion\": \"LATEST\",\n";
    file << "  \"networkConfiguration\": {\n";
    file << "    \"awsvpcConfiguration\": {\n";
    file << "      \"subnets\": [\"<SUBNET_1>\", \"<SUBNET_2>\"],\n";
    file << "      \"securityGroups\": [\"<SECURITY_GROUP>\"],\n";
    file << "      \"assignPublicIp\": \"ENABLED\"\n";
    file << "    }\n";
    file << "  },\n";
    file << "  \"loadBalancers\": [\n";
    file << "    {\n";
    file << "      \"targetGroupArn\": \"arn:aws:elasticloadbalancing:<REGION>:<ACCOUNT_ID>"
         << ":targetgroup/" << app_name << "-tg/<TG_ID>\",\n";
    file << "      \"containerName\": \"" << app_name << "\",\n";
    file << "      \"containerPort\": " << k8s.port << "\n";
    file << "    }\n";
    file << "  ],\n";
    file << "  \"deploymentConfiguration\": {\n";
    file << "    \"maximumPercent\": 200,\n";
    file << "    \"minimumHealthyPercent\": 100,\n";
    file << "    \"deploymentCircuitBreaker\": {\n";
    file << "      \"enable\": true,\n";
    file << "      \"rollback\": true\n";
    file << "    }\n";
    file << "  },\n";
    file << "  \"enableExecuteCommand\": true\n";
    file << "}\n";
  }

  // Generate ALB config
  {
    std::ofstream file(dir + "/alb.json");
    file << "{\n";
    file << "  \"loadBalancerName\": \"" << app_name << "-alb\",\n";
    file << "  \"scheme\": \"internet-facing\",\n";
    file << "  \"type\": \"application\",\n";
    file << "  \"subnets\": [\"<SUBNET_1>\", \"<SUBNET_2>\"],\n";
    file << "  \"securityGroups\": [\"<ALB_SECURITY_GROUP>\"],\n";
    file << "  \"listeners\": [\n";
    file << "    {\n";
    file << "      \"port\": 80,\n";
    file << "      \"protocol\": \"HTTP\",\n";
    file << "      \"defaultActions\": [\n";
    file << "        {\n";
    file << "          \"type\": \"redirect\",\n";
    file << "          \"redirectConfig\": {\n";
    file << "            \"protocol\": \"HTTPS\",\n";
    file << "            \"port\": \"443\",\n";
    file << "            \"statusCode\": \"HTTP_301\"\n";
    file << "          }\n";
    file << "        }\n";
    file << "      ]\n";
    file << "    },\n";
    file << "    {\n";
    file << "      \"port\": 443,\n";
    file << "      \"protocol\": \"HTTPS\",\n";
    file << "      \"certificateArn\": \"<ACM_CERTIFICATE_ARN>\",\n";
    file << "      \"defaultActions\": [\n";
    file << "        {\n";
    file << "          \"type\": \"forward\",\n";
    file << "          \"targetGroupArn\": \"<TARGET_GROUP_ARN>\"\n";
    file << "        }\n";
    file << "      ]\n";
    file << "    }\n";
    file << "  ],\n";
    file << "  \"targetGroup\": {\n";
    file << "    \"name\": \"" << app_name << "-tg\",\n";
    file << "    \"port\": " << k8s.port << ",\n";
    file << "    \"protocol\": \"HTTP\",\n";
    file << "    \"targetType\": \"ip\",\n";
    file << "    \"healthCheck\": {\n";
    file << "      \"path\": \"/health\",\n";
    file << "      \"interval\": 30,\n";
    file << "      \"timeout\": 5,\n";
    file << "      \"healthyThresholdCount\": 2,\n";
    file << "      \"unhealthyThresholdCount\": 3\n";
    file << "    }\n";
    file << "  }\n";
    file << "}\n";
  }

  // Generate deploy script
  {
    std::ofstream file(dir + "/deploy.sh");
    file << "#!/usr/bin/env bash\n";
    file << "# ECS Fargate deployment script\n";
    file << "# Generated by Neam v0.6.0\n";
    file << "set -euo pipefail\n\n";
    file << "APP_NAME=\"" << app_name << "\"\n";
    file << "CLUSTER=\"${APP_NAME}-cluster\"\n";
    file << "REGION=\"${AWS_REGION:-us-east-1}\"\n\n";
    file << "echo \"==> Registering task definition...\"\n";
    file << "aws ecs register-task-definition \\\n";
    file << "  --cli-input-json file://task-definition.json \\\n";
    file << "  --region \"$REGION\"\n\n";
    file << "echo \"==> Updating ECS service...\"\n";
    file << "aws ecs update-service \\\n";
    file << "  --cluster \"$CLUSTER\" \\\n";
    file << "  --service \"$APP_NAME\" \\\n";
    file << "  --task-definition \"$APP_NAME\" \\\n";
    file << "  --force-new-deployment \\\n";
    file << "  --region \"$REGION\"\n\n";
    file << "echo \"==> Waiting for service stability...\"\n";
    file << "aws ecs wait services-stable \\\n";
    file << "  --cluster \"$CLUSTER\" \\\n";
    file << "  --services \"$APP_NAME\" \\\n";
    file << "  --region \"$REGION\"\n\n";
    file << "echo \"==> Deployment complete.\"\n";
  }

  if (dry_run)
  {
    std::cout << "[dry-run] Would generate ECS Fargate manifests in " << dir << "/\n";
  }
  else
  {
    std::cout << "Generated ECS Fargate manifests in " << dir << "/\n";
  }
}
}  // namespace neamc
