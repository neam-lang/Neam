// Deployment generation (dry-run to temp dir) tests.
#include <gtest/gtest.h>
#include "neamc/deploy.hpp"
#include "neamc/project_manifest.hpp"
#include <filesystem>
#include <fstream>

using namespace neamc;
namespace fs = std::filesystem;

// ===========================================================================
// Helpers
// ===========================================================================

class DeployTest : public ::testing::Test {
protected:
    void SetUp() override {
        output_dir_ = fs::temp_directory_path() / "neam_deploy_test";
        fs::create_directories(output_dir_);
    }

    void TearDown() override {
        fs::remove_all(output_dir_);
    }

    bool file_exists(const std::string& name) {
        return fs::exists(output_dir_ / name);
    }

    std::string read_file(const std::string& name) {
        std::ifstream f(output_dir_ / name);
        return std::string((std::istreambuf_iterator<char>(f)),
                           std::istreambuf_iterator<char>());
    }

    DeployConfig make_deploy_config() {
        DeployConfig config;
        config.docker.image = "myapp";
        config.docker.registry = "registry.example.com";
        config.docker.tag_format = "v{version}";
        config.kubernetes.namespace_ = "default";
        config.kubernetes.replicas = 2;
        config.kubernetes.cpu = "500m";
        config.kubernetes.memory = "1Gi";
        config.kubernetes.port = 8080;
        return config;
    }

    ProjectManifest make_manifest() {
        ProjectManifest manifest;
        manifest.name = "myapp";
        manifest.version = "1.0.0";
        manifest.deploy = make_deploy_config();
        return manifest;
    }

    fs::path output_dir_;
};

// ===========================================================================
// Dockerfile
// ===========================================================================

TEST_F(DeployTest, GenerateDockerfile) {
    auto config = make_deploy_config();
    generate_dockerfile(config);
    // Should not crash; actual file location depends on impl
}

// ===========================================================================
// Kubernetes
// ===========================================================================

TEST_F(DeployTest, GenerateK8sManifests) {
    auto config = make_deploy_config();
    generate_k8s_manifests(config.kubernetes, "myapp", "myapp:latest");
    // Should not crash
}

// ===========================================================================
// Helm chart
// ===========================================================================

TEST_F(DeployTest, GenerateHelmChart) {
    auto config = make_deploy_config();
    generate_helm_chart(config.kubernetes, "myapp", "myapp:latest");
}

// ===========================================================================
// Terraform
// ===========================================================================

TEST_F(DeployTest, GenerateTerraform) {
    auto config = make_deploy_config();
    generate_terraform(config.kubernetes, "myapp", "myapp:latest");
}

// ===========================================================================
// SAM template
// ===========================================================================

TEST_F(DeployTest, GenerateSamTemplate) {
    auto config = make_deploy_config();
    generate_sam_template(config, "myapp");
}

// ===========================================================================
// Cloud Run
// ===========================================================================

TEST_F(DeployTest, GenerateCloudRun) {
    auto config = make_deploy_config();
    generate_cloudrun_service(config, "myapp");
}

// ===========================================================================
// Azure Container Apps
// ===========================================================================

TEST_F(DeployTest, GenerateAzureContainerApps) {
    auto manifest = make_manifest();
    generate_azure_container_apps(manifest, output_dir_.string(), true);
}

// ===========================================================================
// Azure AKS
// ===========================================================================

TEST_F(DeployTest, GenerateAzureAKS) {
    auto manifest = make_manifest();
    generate_azure_aks(manifest, output_dir_.string(), true);
}

// ===========================================================================
// ECS Fargate
// ===========================================================================

TEST_F(DeployTest, GenerateEcsFargate) {
    auto manifest = make_manifest();
    generate_ecs_fargate(manifest, output_dir_.string(), true);
}

// ===========================================================================
// Docker push config
// ===========================================================================

TEST_F(DeployTest, GenerateDockerPush) {
    auto config = make_deploy_config();
    generate_docker_push(config);
}

// ===========================================================================
// Edge cases
// ===========================================================================

TEST_F(DeployTest, EmptyAppName) {
    DeployConfig config;
    EXPECT_NO_THROW(generate_dockerfile(config));
}

TEST_F(DeployTest, SpecialCharsInImageName) {
    auto config = make_deploy_config();
    config.docker.image = "registry.io/org/my-app";
    EXPECT_NO_THROW(generate_k8s_manifests(config.kubernetes, "my-app_v2",
                                            "registry.io/org/my-app:v1.0.0-rc1"));
}
