#pragma once
#include "neamc/vm/object.hpp"
#include <string>
#include <vector>
#include <mutex>
#include <nlohmann/json.hpp>

namespace neamc::vm {

// ═══════════════════════════════════════════════════════════════
// v0.9.8.2 MLOps Agent Runtime Types
// ═══════════════════════════════════════════════════════════════

struct ObjDriftMonitor : Obj {
  std::string name;
  std::string model_ref;
  std::string reference_dataset;
  std::string data_drift_json;
  std::string concept_drift_json;
  std::string prediction_drift_json;
  std::string alerts_json;
  std::string root_cause_analysis_json;
  std::string drift_status = "monitoring";
  nlohmann::json latest_drift_report;
  std::string last_check_time;
  mutable std::mutex state_mutex;
};

struct ObjRetrainingPipeline : Obj {
  std::string name;
  std::string triggers_json;
  std::string data_json;
  std::string training_json;
  std::string validation_json;
  std::string deployment_json;
  std::string notifications_json;
  std::string pipeline_status = "idle";
  int retrains_triggered = 0;
  mutable std::mutex state_mutex;
};

struct ObjMLDeployStrategy : Obj {
  std::string name;
  std::string strategy;
  std::string config_json;
  std::string deploy_status = "idle";
  int current_percentage = 0;
  mutable std::mutex state_mutex;
};

struct ObjChampionChallenger : Obj {
  std::string name;
  std::string champion_json;
  std::string challenger_json;
  std::string evaluation_json;
  std::string promotion_json;
  std::string rollback_json;
  std::string eval_status = "pending";
  nlohmann::json comparison_results;
  mutable std::mutex state_mutex;
};

struct ObjServingInfra : Obj {
  std::string name;
  std::string mode;
  std::string platform_json;
  std::string sla_json;
  std::string cost_json;
  std::string health_json;
  int current_replicas = 0;
  double current_p99_latency = 0.0;
  std::string health_status = "unknown";
  mutable std::mutex state_mutex;
};

struct ObjTrainingInfraMLOps : Obj {
  std::string name;
  std::string compute_tiers_json;
  std::string selection_json;
  std::string cost_tracking_json;
  double total_cost = 0.0;
  mutable std::mutex state_mutex;
};

struct ObjMLOpsRollback : Obj {
  std::string name;
  std::string auto_triggers_json;
  std::string strategy_json;
  std::string post_rollback_json;
  std::string recovery_json;
  int rollbacks_executed = 0;
  mutable std::mutex state_mutex;
};

struct ObjMonitoringStack : Obj {
  std::string name;
  std::string evidently_json;
  std::string prometheus_json;
  std::string whylabs_json;
};

struct ObjMLflowConfig : Obj {
  std::string name;
  std::string mcp_server_ref;
  std::string tracking_json;
  std::string registry_json;
  std::string lifecycle_json;
};

struct ObjBusinessKPITracker : Obj {
  std::string name;
  std::string model_ref;
  std::string kpis_json;
  std::string report_frequency;
  std::string compare_with;
  nlohmann::json latest_kpi_report;
  mutable std::mutex state_mutex;
};

struct ObjDatasetVersion : Obj {
  std::string name;
  std::string versioning_tool;
  std::string source_ref;
  std::string query;
  std::string hash_method;
  std::string storage;
  std::string lineage_json;
  std::string schema_validation_json;
};

struct ObjFeedbackLoop : Obj {
  std::string name;
  std::string production_metrics_json;
  std::string recommendations_json;
  std::string trigger_ds_agent_json;
};

struct ObjDecisionEngine : Obj {
  std::string name;
  std::string retrain_policy_json;
  std::string rollback_policy_ref;
  std::string scaling_policy_json;
  std::string human_in_the_loop_json;
};

struct ObjEventBus : Obj {
  std::string name;
  std::string emits_json;
  std::string listens_json;
  std::string routing_json;
};

struct ObjDriftRCA : Obj {
  std::string name;
  std::string causal_agent_ref;
  std::string investigation_json;
  std::string actions_json;
};

struct ObjMLOpsAgent : Obj {
  std::string name;
  std::string provider;
  std::string llm_model;
  std::string system_prompt;
  double temperature = 0.2;
  std::string agent_md_path;
  std::string sub_agents_json;
  std::string drift_monitor_ref;
  std::string retraining_pipeline_ref;
  std::string deployment_strategy_ref;
  std::string champion_challenger_ref;
  std::string serving_infra_ref;
  std::string training_infra_ref;
  std::string rollback_policy_ref;
  std::string monitoring_stack_ref;
  std::string mlflow_ref;
  std::string business_kpi_tracker_ref;
  std::string feedback_loop_ref;
  std::string decision_engine_ref;
  std::string event_bus_ref;
  std::string coordinates_with;
  std::string handoffs;
  std::string role;
  std::string purpose;
  std::string autonomy;
  std::string budget_ref;
  std::string status = "initialized";
  int models_monitored = 0;
  int retrains_triggered = 0;
  int rollbacks_executed = 0;
  mutable std::mutex state_mutex;
};

// Factory functions
ObjDriftMonitor*         new_drift_monitor();
ObjRetrainingPipeline*   new_retraining_pipeline();
ObjMLDeployStrategy*     new_ml_deploy_strategy();
ObjChampionChallenger*   new_champion_challenger();
ObjServingInfra*         new_serving_infra();
ObjTrainingInfraMLOps*   new_training_infra_mlops();
ObjMLOpsRollback*        new_mlops_rollback();
ObjMonitoringStack*      new_monitoring_stack();
ObjMLflowConfig*         new_mlflow_config();
ObjBusinessKPITracker*   new_business_kpi_tracker();
ObjDatasetVersion*       new_dataset_version();
ObjFeedbackLoop*         new_feedback_loop();
ObjDecisionEngine*       new_decision_engine();
ObjEventBus*             new_event_bus();
ObjDriftRCA*             new_drift_rca();
ObjMLOpsAgent*           new_mlops_agent();

}  // namespace neamc::vm
