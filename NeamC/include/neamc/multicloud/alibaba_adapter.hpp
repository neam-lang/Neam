// SPDX-License-Identifier: Apache-2.0
// Copyright (c) 2024-2026 Neam Language Contributors
//
// Neam v0.6.4 - Alibaba Cloud Adapter
// Native support for Alibaba Cloud services for APAC deployments

#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>
#include <map>
#include <optional>

#include <nlohmann/json.hpp>

namespace neamc::multicloud {

/**
 * Alibaba Cloud configuration
 */
struct AlibabaConfig {
    std::string access_key_id;
    std::string access_key_secret;
    std::string default_region = "cn-hangzhou";
    bool use_internal_network = true;  // For lower latency within Alibaba
    std::string security_token;  // Optional STS token
    int timeout_seconds = 30;
    int max_retries = 3;
};

/**
 * Function Compute (serverless) configuration
 */
struct AliFunctionConfig {
    std::string function_name;
    std::string service_name;
    int memory_mb = 128;
    int timeout_seconds = 60;
    std::string runtime = "custom.debian10";
    std::string handler = "neam-runtime";
    std::map<std::string, std::string> environment;
    std::string code_zip_path;  // Path to deployment package
    int instance_concurrency = 1;
    bool enable_instance_metrics = true;

    // Provisioned concurrency for warm starts
    int provisioned_concurrency = 0;
};

/**
 * ECS (Elastic Compute Service) configuration
 */
struct AliECSConfig {
    std::string instance_type = "ecs.g6.large";
    std::string image_id;  // Neam base image
    bool use_spot = true;
    double spot_price_limit = 0.0;  // 0 = market price
    std::string security_group_id;
    std::string vswitch_id;
    std::string instance_name;
    std::string key_pair_name;
    int system_disk_size_gb = 40;
    std::string system_disk_category = "cloud_efficiency";
    std::map<std::string, std::string> tags;

    // Auto-release for spot instances
    std::optional<std::chrono::system_clock::time_point> auto_release_time;
};

/**
 * ACK (Container Service for Kubernetes) configuration
 */
struct AliACKConfig {
    std::string cluster_id;
    std::string namespace_name = "default";
    int replicas = 1;
    std::string image;
    std::map<std::string, std::string> resources;  // cpu, memory limits
    std::map<std::string, std::string> node_selector;
    bool use_spot_nodes = true;
};

/**
 * Function invocation result
 */
struct AliFunctionResult {
    bool success;
    std::string request_id;
    std::string result;
    std::string error_message;
    int status_code;
    double duration_ms;
    double billed_duration_ms;
    int memory_used_mb;
};

/**
 * ECS instance information
 */
struct AliECSInstance {
    std::string instance_id;
    std::string instance_name;
    std::string instance_type;
    std::string status;  // Running, Stopped, etc.
    std::string public_ip;
    std::string private_ip;
    std::string region_id;
    std::string zone_id;
    bool is_spot;
    double spot_price;
    std::chrono::system_clock::time_point created_at;
};

/**
 * Spot price information
 */
struct AliSpotPrice {
    std::string instance_type;
    std::string zone_id;
    double spot_price;
    double origin_price;
    std::chrono::system_clock::time_point timestamp;
};

/**
 * Alibaba Cloud Adapter - Native integration for APAC deployments
 *
 * Features:
 * - Function Compute (serverless) deployment
 * - ECS instance management with spot support
 * - ACK (Kubernetes) deployment
 * - Real-time spot pricing
 * - GPU instance support (GN6i series)
 */
class AlibabaAdapter {
public:
    explicit AlibabaAdapter(AlibabaConfig config);
    ~AlibabaAdapter();

    // Prevent copying
    AlibabaAdapter(const AlibabaAdapter&) = delete;
    AlibabaAdapter& operator=(const AlibabaAdapter&) = delete;

    /**
     * Test connection to Alibaba Cloud
     */
    bool test_connection();

    // ==================== Function Compute ====================

    /**
     * Deploy a function
     */
    std::string deploy_function(const AliFunctionConfig& config);

    /**
     * Update an existing function
     */
    void update_function(
        const std::string& service_name,
        const std::string& function_name,
        const AliFunctionConfig& config
    );

    /**
     * Delete a function
     */
    void delete_function(
        const std::string& service_name,
        const std::string& function_name
    );

    /**
     * Invoke a function
     */
    AliFunctionResult invoke_function(
        const std::string& service_name,
        const std::string& function_name,
        const std::string& payload
    );

    /**
     * List functions in a service
     */
    std::vector<std::string> list_functions(const std::string& service_name);

    /**
     * Set provisioned concurrency
     */
    void set_provisioned_concurrency(
        const std::string& service_name,
        const std::string& function_name,
        int concurrency
    );

    // ==================== ECS ====================

    /**
     * Create ECS instance
     */
    std::string create_ecs_instance(const AliECSConfig& config);

    /**
     * Start ECS instance
     */
    void start_ecs_instance(const std::string& instance_id);

    /**
     * Stop ECS instance
     */
    void stop_ecs_instance(const std::string& instance_id);

    /**
     * Terminate ECS instance
     */
    void terminate_ecs_instance(const std::string& instance_id);

    /**
     * Get ECS instance info
     */
    std::optional<AliECSInstance> get_ecs_instance(const std::string& instance_id);

    /**
     * List ECS instances
     */
    std::vector<AliECSInstance> list_ecs_instances(
        const std::string& region = ""
    );

    // ==================== ACK (Kubernetes) ====================

    /**
     * Deploy to ACK cluster
     */
    std::string deploy_to_ack(
        const std::string& cluster_id,
        const std::string& manifest_yaml
    );

    /**
     * Scale ACK deployment
     */
    void scale_ack_deployment(
        const std::string& cluster_id,
        const std::string& deployment_name,
        int replicas
    );

    /**
     * Delete ACK deployment
     */
    void delete_ack_deployment(
        const std::string& cluster_id,
        const std::string& deployment_name
    );

    // ==================== Pricing ====================

    /**
     * Get spot price for instance type
     */
    double get_spot_price(
        const std::string& region,
        const std::string& instance_type
    );

    /**
     * Get spot price history
     */
    std::vector<AliSpotPrice> get_spot_price_history(
        const std::string& region,
        const std::string& instance_type,
        int hours = 24
    );

    /**
     * Get Function Compute pricing
     */
    double get_function_price_per_invocation(int memory_mb);
    double get_function_price_per_gb_second();

    /**
     * List available GPU instance types
     */
    std::vector<std::string> list_gpu_instance_types(
        const std::string& region
    );

    /**
     * Get GPU spot price
     */
    double get_gpu_spot_price(
        const std::string& region,
        const std::string& instance_type
    );

    // ==================== Regions ====================

    /**
     * List available regions
     */
    std::vector<std::string> list_regions();

    /**
     * List zones in a region
     */
    std::vector<std::string> list_zones(const std::string& region);

    /**
     * Get region endpoint
     */
    std::string get_region_endpoint(const std::string& region);

    // ==================== Configuration ====================

    /**
     * Update configuration
     */
    void set_config(AlibabaConfig config);
    AlibabaConfig get_config() const;

    /**
     * Set default region
     */
    void set_default_region(const std::string& region);

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// ==================== Template Generators ====================

/**
 * Generate Alibaba Function Compute template
 */
std::string generate_alibaba_fc_template(
    const std::string& agent_name,
    const AliFunctionConfig& config
);

/**
 * Generate Alibaba ECS template (Terraform)
 */
std::string generate_alibaba_ecs_terraform(
    const std::string& agent_name,
    const AliECSConfig& config
);

/**
 * Generate Alibaba ACK manifest (Kubernetes YAML)
 */
std::string generate_alibaba_ack_manifest(
    const std::string& agent_name,
    const AliACKConfig& config
);

/**
 * Generate ROS (Resource Orchestration Service) template
 */
std::string generate_alibaba_ros_template(
    const std::string& stack_name,
    const std::vector<AliFunctionConfig>& functions,
    const std::vector<AliECSConfig>& instances
);

/**
 * Create Alibaba adapter
 */
std::unique_ptr<AlibabaAdapter> create_alibaba_adapter(AlibabaConfig config);

}  // namespace neamc::multicloud
