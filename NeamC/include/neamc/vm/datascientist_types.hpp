#pragma once
#include "neamc/vm/object.hpp"
#include <string>
#include <vector>
#include <mutex>
#include <nlohmann/json.hpp>

namespace neamc::vm {

// ═══════════════════════════════════════════════════════════════
// v0.9.8 Data Scientist Agent Runtime Types
// ═══════════════════════════════════════════════════════════════

struct ObjProblemStatement : Obj {
  std::string name;
  std::string statement;
  std::string business_context_json;
  std::string constraints_json;
  std::string deliverables_json;
};

struct ObjHypothesisTest : Obj {
  std::string name;
  std::string null_hypothesis;
  std::string alternative;
  std::string test_type;
  double significance_level = 0.05;
  double power = 0.80;
  std::string effect_size;
  std::string data_source;
  std::string group_a;
  std::string group_b;
  std::string assumptions_json;
  std::string if_significant_json;
  std::string if_not_significant_json;
};

struct ObjFeatureEngineering : Obj {
  std::string name;
  std::string source_tables_json;
  std::string strategies_json;
  std::string selection_json;
  std::string output_json;
};

struct ObjMLExperiment : Obj {
  std::string name;
  std::string problem_type;
  std::string target;
  std::string positive_class;
  std::string dataset;
  double train_test_split = 0.8;
  bool stratify = true;
  std::string cross_validation_json;
  std::string algorithms;
  std::string metrics_json;
  std::string interpretability;
  std::string budget_ref;
};

struct ObjAutoMLConfig : Obj {
  std::string name;
  std::string algorithms;
  std::string preprocessing_search_json;
  std::string optimization_json;
  int cv_folds = 5;
  std::string primary_metric;
  bool holdout_validation = true;
  std::string selection_criteria_json;
  bool leaderboard = true;
};

struct ObjHyperparameterConfig : Obj {
  std::string name;
  std::string algorithm;
  std::string search_space_json;
  std::string optimizer_json;
  std::string early_stopping_json;
};

struct ObjStackedModel : Obj {
  std::string name;
  std::string base_learners_json;
  std::string meta_learner_json;
  std::string strategy_json;
  std::string compare_against;
  double improvement_threshold = 0.005;
};

struct ObjEvaluationConfig : Obj {
  std::string name;
  std::string classification_json;
  std::string regression_json;
  std::string clustering_json;
  std::string business_json;
  std::string cv_strategy;
  int outer_folds = 5;
  int inner_folds = 3;
  std::string model_comparison_json;
};

struct ObjDSModelRegistry : Obj {
  std::string name;
  std::string storage_json;
  std::string tracking_json;
  std::string model_card_json;
  std::string lifecycle_json;
  // Runtime state
  std::vector<std::string> registered_models;
  mutable std::mutex state_mutex;
};

struct ObjExplainabilityConfig : Obj {
  std::string name;
  std::string global_json;
  std::string local_json;
  std::string fairness_json;
};

struct ObjCodeInterpreter : Obj {
  std::string name;
  std::string runtime;
  std::string version;
  std::string venv_manager_ref;
  std::string profiles_json;
  std::string profile_selection;
  std::string sandbox_json;
  std::string auto_test_json;
  std::string data_bridge_json;
};

struct ObjVenvManager : Obj {
  std::string name;
  std::string lifecycle_json;
  std::string pool_json;
  std::string dependency_resolver_json;
};

struct ObjNLPPipeline : Obj {
  std::string name;
  std::string preprocessing_json;
  std::string tasks_json;
  std::string embedding_model;
  std::string vector_store;
};

struct ObjChurnAnalysis : Obj {
  std::string name;
  std::string churn_definition_json;
  std::string features_json;
  std::string primary_model;
  std::string calibration;
  std::string threshold_optimization;
  std::string outputs_json;
};

struct ObjCLVModel : Obj {
  std::string name;
  std::string model_type;
  std::string frequency;
  std::string recency;
  std::string monetary;
  std::string T;
  std::string prediction_periods_json;
  double discount_rate = 0.10;
  std::string segments_json;
};

struct ObjPropensityModel : Obj {
  std::string name;
  std::string target_action;
  std::string training_window;
  std::string features_json;
  std::string algorithm;
  std::string calibration_method;
  std::string score_output_json;
  std::string actions_json;
};

struct ObjRecommendationEngine : Obj {
  std::string name;
  std::string strategy;
  std::string collaborative_json;
  std::string content_based_json;
  std::string blending_json;
  std::string rules_json;
  std::string metrics;
  std::string serving_json;
};

struct ObjExperimentDesign : Obj {
  std::string name;
  std::string experiment_type;
  std::string control_json;
  std::string treatments_json;
  std::string unit;
  std::string stratify_by;
  std::string power_analysis_json;
  std::string primary_metric_json;
  std::string guardrails_json;
  std::string analysis_json;
};

struct ObjScenarioAnalysis : Obj {
  std::string name;
  std::string base_model;
  std::string scenarios_json;
  std::string simulation_json;
};

struct ObjDecisionSupport : Obj {
  std::string name;
  std::string deliverables_json;
  std::string confidence_json;
};

struct ObjEDAConfig : Obj {
  std::string name;
  std::string structural_json;
  std::string univariate_json;
  std::string bivariate_json;
  std::string multivariate_json;
  std::string temporal_json;
  std::string performance_json;
  std::string output_json;
};

struct ObjEDATechniqueSelector : Obj {
  std::string name;
  std::string rules_json;
  std::string output_json;
};

struct ObjSmartConnector : Obj {
  std::string name;
  std::string discovery_json;
  std::string metadata_cache_json;
};

struct ObjVolumeRouter : Obj {
  std::string name;
  std::string volume_probe_json;
  std::string routing_rules_json;
  std::string auto_escalate_json;
  std::string sampling_strategy_json;
};

struct ObjComputeConnector : Obj {
  std::string name;
  std::string engine;
  std::string connection;
  std::string token;
  std::string cluster_config_json;
  std::string idle_timeout;
  bool cost_tracking = true;
};

struct ObjFileConnector : Obj {
  std::string name;
  std::string base_path;
  bool auto_detect_schema = true;
  bool auto_detect_delimiter = true;
  bool auto_detect_encoding = true;
  std::string supported_formats_json;
  std::string large_file_strategy_json;
};

struct ObjDistributedComputeConfig : Obj {
  std::string name;
  std::string spark_json;
  std::string databricks_json;
  std::string snowflake_json;
  std::string hadoop_json;
  std::string gpu_json;
  std::string selection_logic_json;
};

struct ObjPerformanceConfig : Obj {
  std::string name;
  std::string phase_slas_json;
  std::string cache_json;
  std::string parallelism_json;
  std::string lazy_eval_json;
  std::string memory_json;
};

struct ObjDataQualityPipeline : Obj {
  std::string name;
  std::string profiling_json;
  std::string scoring_json;
  std::string remediation_json;
  std::string governance_ref;
};

struct ObjSelfCorrectionConfig : Obj {
  std::string name;
  std::string code_errors_json;
  std::string statistical_errors_json;
  std::string model_errors_json;
  std::string reasoning_errors_json;
};

struct ObjSelfAssessment : Obj {
  std::string name;
  std::string planning_json;
  std::string execution_json;
  std::string interpretation_json;
  std::string communication_json;
  std::string gate_json;
};

struct ObjAdaptiveKnowledgeConfig : Obj {
  std::string name;
  std::string knowledge_sources_json;
  std::string adaptation_json;
  std::string learning_json;
};

struct ObjAnalysisHistory : Obj {
  std::string name;
  std::string knowledge_base;
  std::string vector_store;
  std::string embedding_model;
  std::string retrieval_strategy;
  std::string record_fields_json;
  std::string retention;
  int max_records = 10000;
};

struct ObjObservabilityConfig : Obj {
  std::string name;
  std::string feature_monitoring_json;
  std::string prediction_monitoring_json;
  std::string alerts_json;
  std::string auto_remediation_json;
  std::string dataops_ref;
};

struct ObjDataScientistAgent : Obj {
  std::string name;
  // LLM base
  std::string provider;
  std::string llm_model;
  std::string system_prompt;
  double temperature = 0.2;
  std::string endpoint;
  std::string api_key_env;
  // Agent.MD
  std::string agent_md_path;
  // Problem
  std::string problem_ref;
  std::string problem_types;
  // Sub-agents & forge
  std::string sub_agents_json;
  std::string forge_ref;
  // Data
  std::string data_sources_json;
  // Component refs
  std::string eda_config_ref;
  std::string feature_config_ref;
  std::string experiment_ref;
  std::string automl_ref;
  std::string ensemble_ref;
  std::string hypotheses_refs;
  std::string evaluation_ref;
  std::string explainability_ref;
  std::string code_interpreter_ref;
  std::string model_registry_ref;
  std::string churn_ref;
  std::string clv_ref;
  std::string propensity_ref;
  std::string recommendation_ref;
  std::string experiment_engine_ref;
  std::string decision_framework_ref;
  std::string volume_router_ref;
  std::string distributed_compute_ref;
  std::string performance_ref;
  std::string data_quality_ref;
  std::string self_correction_ref;
  std::string self_assessment_ref;
  std::string adaptive_knowledge_ref;
  std::string deployment_ref;
  // Coordination
  std::string coordinates_with;
  std::string handoffs;
  // Identity
  std::string role;
  std::string purpose;
  std::string autonomy;
  // Budget
  std::string budget_ref;
  // Runtime state
  std::string status = "uninitialized";
  mutable std::mutex state_mutex;
};

// Factory functions
ObjProblemStatement* new_problem_statement();
ObjHypothesisTest* new_hypothesis_test();
ObjFeatureEngineering* new_feature_engineering();
ObjMLExperiment* new_ml_experiment();
ObjAutoMLConfig* new_automl_config();
ObjHyperparameterConfig* new_hyperparameter_config();
ObjStackedModel* new_stacked_model();
ObjEvaluationConfig* new_evaluation_config();
ObjDSModelRegistry* new_ds_model_registry();
ObjExplainabilityConfig* new_explainability_config();
ObjCodeInterpreter* new_code_interpreter();
ObjVenvManager* new_venv_manager();
ObjNLPPipeline* new_nlp_pipeline();
ObjChurnAnalysis* new_churn_analysis();
ObjCLVModel* new_clv_model();
ObjPropensityModel* new_propensity_model();
ObjRecommendationEngine* new_recommendation_engine();
ObjExperimentDesign* new_experiment_design();
ObjScenarioAnalysis* new_scenario_analysis();
ObjDecisionSupport* new_decision_support();
ObjEDAConfig* new_eda_config();
ObjEDATechniqueSelector* new_eda_technique_selector();
ObjSmartConnector* new_smart_connector();
ObjVolumeRouter* new_volume_router();
ObjComputeConnector* new_compute_connector();
ObjFileConnector* new_file_connector();
ObjDistributedComputeConfig* new_distributed_compute_config();
ObjPerformanceConfig* new_performance_config();
ObjDataQualityPipeline* new_data_quality_pipeline();
ObjSelfCorrectionConfig* new_self_correction_config();
ObjSelfAssessment* new_self_assessment();
ObjAdaptiveKnowledgeConfig* new_adaptive_knowledge_config();
ObjAnalysisHistory* new_analysis_history();
ObjObservabilityConfig* new_observability_config();
ObjDataScientistAgent* new_datascientist_agent();

}  // namespace neamc::vm
