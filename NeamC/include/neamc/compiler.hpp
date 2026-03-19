//
// NeamC - Compiler backend translating AST to bytecode
//

#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "neamc/ast.hpp"
#include "neamc/vm/bytecode.hpp"

namespace neamc
{
class Compiler
{
public:
  vm::Chunk compile(const Program& program);

private:
  struct Local
  {
    std::string name;
    int depth = 0;
  };

  vm::Value compile_function(const FunctionDecl& decl);
  vm::Value compile_block_function(const std::string& name,
                                   const std::vector<std::string>& parameters,
                                   const BlockStmt& body);
  void begin_scope();
  void end_scope();
  int resolve_local(const std::string& name) const;

  void emit_statement(const Statement& stmt);
  void emit_expression(const Expression& expr);
  void emit_block(const BlockStmt& block);

  // v0.8 Phase 1: Agent type tracking for trait validation
  enum class AgentKind { Stateless, Claw, Forge, Data, ETL, Migration, DataOps, Governance, Modeling, Analyst, DataScientist, Causal, MLOps, DataBA, DataTest, DIO };
  std::unordered_map<std::string, AgentKind> agent_types_;

  // v0.9: Data agent definition tracking
  std::unordered_map<std::string, bool> schema_defs_;
  std::unordered_map<std::string, bool> source_defs_;
  std::unordered_map<std::string, bool> sink_defs_;
  std::unordered_map<std::string, bool> compute_defs_;
  std::unordered_map<std::string, bool> governance_defs_;
  std::unordered_map<std::string, bool> catalog_defs_;
  std::unordered_map<std::string, bool> quality_defs_;

  // v0.9.1: ETLAgent tracking
  std::unordered_set<std::string> semantic_defs_;
  std::unordered_set<std::string> mart_defs_;

  struct ETLAgentDef {
    int source_count;
    bool has_warehouse;
    int mart_count;
  };
  std::unordered_map<std::string, ETLAgentDef> etl_agent_defs_;

  // v0.9.2: Migration agent compile-time validation
  void validate_migration_agent(const MigrationAgentDecl& decl);
  bool validate_wave_dag(const std::unordered_map<std::string, std::vector<std::string>>& deps);
  std::string serialize_wave_config(const WaveConfig& cfg);
  std::string serialize_movement_config(const MovementConfig& cfg);
  std::string serialize_schema_translation_config(const SchemaTranslationConfig& cfg);
  std::string serialize_validation_config(const ValidationConfig& cfg);
  std::string serialize_cutover_config(const CutoverConfig& cfg);
  std::string serialize_self_heal_config(const SelfHealMigrationConfig& cfg);
  std::string serialize_assessment_config(const AssessmentConfig& cfg);
  std::string serialize_governance_migration_config(const GovernanceMigrationConfig& cfg);

  // v0.9.3: DataOps agent tracking and validation
  std::unordered_set<std::string> scheduler_defs_;
  std::unordered_set<std::string> audit_table_defs_;
  std::unordered_set<std::string> log_source_defs_;
  std::unordered_set<std::string> platform_defs_;
  std::unordered_set<std::string> incident_policy_defs_;
  std::unordered_set<std::string> correlation_defs_;
  void validate_dataops_agent(const DataOpsAgentDecl& decl);

  // v0.9.4: Governance agent tracking and validation
  std::unordered_set<std::string> catalog_source_defs_;
  std::unordered_set<std::string> gov_catalog_defs_;
  std::unordered_set<std::string> glossary_defs_;
  std::unordered_set<std::string> classification_policy_defs_;
  std::unordered_set<std::string> access_policy_defs_;
  std::unordered_set<std::string> quality_policy_defs_;
  std::unordered_set<std::string> lineage_policy_defs_;
  std::unordered_set<std::string> compliance_policy_defs_;
  std::unordered_set<std::string> lifecycle_policy_defs_;
  std::unordered_set<std::string> data_product_defs_;
  std::unordered_set<std::string> contract_policy_defs_;
  std::unordered_set<std::string> master_data_defs_;
  std::unordered_set<std::string> external_tool_defs_;
  std::unordered_set<std::string> governance_agent_defs_;
  void validate_governance_agent(const GovernanceAgentDecl& decl);

  // v0.9.5: Modeling agent tracking and validation
  std::unordered_set<std::string> schema_source_defs_;
  std::unordered_set<std::string> er_model_defs_;
  std::unordered_set<std::string> modeling_entity_defs_;
  std::unordered_set<std::string> dimensional_model_defs_;
  std::unordered_set<std::string> datamart_v095_defs_;
  std::unordered_set<std::string> norm_analysis_defs_;
  std::unordered_set<std::string> amendment_config_defs_;
  std::unordered_set<std::string> amendment_defs_;
  std::unordered_set<std::string> data_profile_defs_;
  std::unordered_set<std::string> modeling_tool_defs_;
  std::unordered_set<std::string> modeling_agent_defs_;
  void validate_modeling_agent(const ModelingAgentDecl& decl);

  // v0.9.6: Analyst agent tracking and validation
  std::unordered_set<std::string> sql_connection_defs_;
  std::unordered_set<std::string> domain_context_defs_;
  std::unordered_set<std::string> query_template_defs_;
  std::unordered_set<std::string> query_optimizer_defs_;
  std::unordered_set<std::string> execution_policy_defs_;
  std::unordered_set<std::string> output_format_defs_;
  std::unordered_set<std::string> query_library_defs_;
  std::unordered_set<std::string> analysis_schedule_defs_;
  std::unordered_set<std::string> analyst_agent_defs_;
  void validate_analyst_agent(const AnalystAgentDecl& decl);

  // v0.9.7: Data Pipeline Deployment tracking and validation
  std::unordered_set<std::string> deploy_target_defs_;
  std::unordered_set<std::string> promotion_rule_defs_;
  std::unordered_set<std::string> rollback_policy_defs_;
  std::unordered_set<std::string> artifact_registry_defs_;
  std::unordered_set<std::string> deploy_config_defs_;
  void validate_deploy_config(const DeployConfigDecl& decl);

  // v0.9.8: Data Scientist Agent tracking sets
  std::unordered_set<std::string> problem_statement_defs_;
  std::unordered_set<std::string> hypothesis_test_defs_;
  std::unordered_set<std::string> feature_engineering_defs_;
  std::unordered_set<std::string> ml_experiment_defs_;
  std::unordered_set<std::string> automl_config_defs_;
  std::unordered_set<std::string> hyperparameter_config_defs_;
  std::unordered_set<std::string> stacked_model_defs_;
  std::unordered_set<std::string> evaluation_config_defs_;
  std::unordered_set<std::string> ds_model_registry_defs_;
  std::unordered_set<std::string> explainability_config_defs_;
  std::unordered_set<std::string> code_interpreter_defs_;
  std::unordered_set<std::string> venv_manager_defs_;
  std::unordered_set<std::string> nlp_pipeline_defs_;
  std::unordered_set<std::string> churn_analysis_defs_;
  std::unordered_set<std::string> clv_model_defs_;
  std::unordered_set<std::string> propensity_model_defs_;
  std::unordered_set<std::string> recommendation_engine_defs_;
  std::unordered_set<std::string> experiment_design_defs_;
  std::unordered_set<std::string> scenario_analysis_defs_;
  std::unordered_set<std::string> decision_support_defs_;
  std::unordered_set<std::string> eda_config_defs_;
  std::unordered_set<std::string> eda_technique_selector_defs_;
  std::unordered_set<std::string> smart_connector_defs_;
  std::unordered_set<std::string> volume_router_defs_;
  std::unordered_set<std::string> compute_connector_defs_;
  std::unordered_set<std::string> file_connector_defs_;
  std::unordered_set<std::string> distributed_compute_config_defs_;
  std::unordered_set<std::string> performance_config_defs_;
  std::unordered_set<std::string> data_quality_pipeline_defs_;
  std::unordered_set<std::string> self_correction_config_defs_;
  std::unordered_set<std::string> self_assessment_defs_;
  std::unordered_set<std::string> adaptive_knowledge_config_defs_;
  std::unordered_set<std::string> analysis_history_defs_;
  std::unordered_set<std::string> observability_config_defs_;
  std::unordered_set<std::string> datascientist_agent_defs_;
  void validate_datascientist_agent(const DataScientistAgentDecl& decl);

  // v0.9.8.1: Cross-cutting tracking for causal validation
  std::unordered_set<std::string> budget_defs_;
  std::unordered_set<std::string> forge_agent_defs_;

  // v0.9.8.1: Causal Agent tracking
  std::unordered_set<std::string> causal_discovery_defs_;
  std::unordered_set<std::string> scm_defs_;
  std::unordered_set<std::string> intervention_defs_;
  std::unordered_set<std::string> counterfactual_defs_;
  std::unordered_set<std::string> bayesian_model_defs_;
  std::unordered_set<std::string> causal_estimator_defs_;
  std::unordered_set<std::string> quasi_experiment_defs_;
  std::unordered_set<std::string> causal_sensitivity_defs_;
  std::unordered_set<std::string> causal_data_requirements_defs_;
  std::unordered_set<std::string> causal_agent_defs_;
  void validate_causal_agent(const CausalAgentDecl& decl);

  // v0.9.8.2: MLOps Agent tracking
  std::unordered_set<std::string> drift_monitor_defs_;
  std::unordered_set<std::string> retraining_pipeline_defs_;
  std::unordered_set<std::string> ml_deploy_strategy_defs_;
  std::unordered_set<std::string> champion_challenger_defs_;
  std::unordered_set<std::string> serving_infra_defs_;
  std::unordered_set<std::string> training_infra_defs_;
  std::unordered_set<std::string> mlops_rollback_defs_;
  std::unordered_set<std::string> monitoring_stack_defs_;
  std::unordered_set<std::string> mlflow_config_defs_;
  std::unordered_set<std::string> business_kpi_tracker_defs_;
  std::unordered_set<std::string> dataset_version_defs_;
  std::unordered_set<std::string> feedback_loop_defs_;
  std::unordered_set<std::string> decision_engine_defs_;
  std::unordered_set<std::string> event_bus_defs_;
  std::unordered_set<std::string> drift_rca_defs_;
  std::unordered_set<std::string> mlops_agent_defs_;
  void validate_mlops_agent(const MLOpsAgentDecl& decl);

  // v0.9.8.3: Data-BA Agent tracking
  std::unordered_set<std::string> requirements_elicitation_defs_;
  std::unordered_set<std::string> brd_generator_defs_;
  std::unordered_set<std::string> functional_spec_defs_;
  std::unordered_set<std::string> nonfunctional_spec_defs_;
  std::unordered_set<std::string> acceptance_criteria_gen_defs_;
  std::unordered_set<std::string> data_requirements_ba_defs_;
  std::unordered_set<std::string> impact_analysis_ba_defs_;
  std::unordered_set<std::string> traceability_matrix_defs_;
  std::unordered_set<std::string> etl_requirement_spec_defs_;
  std::unordered_set<std::string> ml_requirement_spec_defs_;
  std::unordered_set<std::string> governance_requirement_spec_defs_;
  std::unordered_set<std::string> analytics_requirement_spec_defs_;
  std::unordered_set<std::string> stakeholder_analysis_defs_;
  std::unordered_set<std::string> user_story_generator_defs_;
  std::unordered_set<std::string> scope_management_defs_;
  std::unordered_set<std::string> change_impact_analyzer_defs_;
  std::unordered_set<std::string> databa_agent_defs_;
  void validate_databa_agent(const DataBAAgentDecl& decl);

  // v0.9.8.4: Data Testing Agent tracking
  std::unordered_set<std::string> test_strategy_defs_;
  std::unordered_set<std::string> test_case_generator_defs_;
  std::unordered_set<std::string> test_case_defs_;
  std::unordered_set<std::string> etl_test_suite_defs_;
  std::unordered_set<std::string> dw_test_suite_defs_;
  std::unordered_set<std::string> ml_test_suite_defs_;
  std::unordered_set<std::string> api_test_suite_defs_;
  std::unordered_set<std::string> performance_test_suite_defs_;
  std::unordered_set<std::string> edge_case_tests_defs_;
  std::unordered_set<std::string> sit_suite_defs_;
  std::unordered_set<std::string> uat_suite_defs_;
  std::unordered_set<std::string> regression_suite_defs_;
  std::unordered_set<std::string> quality_gate_test_defs_;
  std::unordered_set<std::string> test_report_config_defs_;
  std::unordered_set<std::string> defect_management_defs_;
  std::unordered_set<std::string> datatest_agent_defs_;
  void validate_datatest_agent(const DataTestAgentDecl& decl);

  // v0.9.9: Data Intelligent Orchestrator tracking
  std::unordered_set<std::string> agent_registry_defs_;
  std::unordered_set<std::string> agent_contracts_defs_;
  std::unordered_set<std::string> raci_matrix_defs_;
  std::unordered_set<std::string> task_understanding_defs_;
  std::unordered_set<std::string> task_decomposer_defs_;
  std::unordered_set<std::string> crew_formation_defs_;
  std::unordered_set<std::string> pattern_selector_defs_;
  std::unordered_set<std::string> execution_manager_dio_defs_;
  std::unordered_set<std::string> dio_state_machine_defs_;
  std::unordered_set<std::string> dio_error_handling_defs_;
  std::unordered_set<std::string> result_synthesizer_defs_;
  std::unordered_set<std::string> infrastructure_profile_defs_;
  std::unordered_set<std::string> role_framework_defs_;
  std::unordered_set<std::string> delegation_protocol_defs_;
  std::unordered_set<std::string> dio_accountability_defs_;
  std::unordered_set<std::string> dio_agent_defs_;
  void validate_dio_agent(const DIOAgentDecl& decl);
  void validate_classification_policy(const ClassificationPolicyDecl& decl);
  void validate_access_policy(const AccessPolicyDecl& decl);
  void validate_quality_policy(const QualityPolicyDecl& decl);
  void validate_lifecycle_policy(const LifecyclePolicyDecl& decl);
  void validate_compliance_policy(const CompliancePolicyDecl& decl);
  void validate_master_data(const MasterDataDecl& decl);
  void validate_data_product(const DataProductDecl& decl);
  std::string serialize_auto_classify(const AutoClassifyConfig& cfg);
  std::string serialize_audit_column_map(const AuditColumnMap& map);
  std::string serialize_audit_anomalies(const AuditAnomalyConfig& cfg);
  std::string serialize_log_alerts(const LogAlertConfig& cfg);
  std::string serialize_health_checks(const PlatformHealthConfig& cfg);
  std::string serialize_finops(const PlatformFinOpsConfig& cfg);
  std::string serialize_severity_levels(const std::vector<SeverityLevel>& levels);
  std::string serialize_auto_heal(const AutoHealConfig& cfg);
  std::string serialize_correlation_scope(const CorrelationScope& scope);
  std::string serialize_correlation_sla(const CorrelationSLAConfig& sla);

  bool is_neamclaw_trait(const std::string& name) const;
  void validate_neamclaw_trait_compat(const std::string& trait, const std::string& type);
  void compiler_warning(const std::string& message) const;

  vm::Chunk chunk_{};
  std::vector<Local> locals_{};
  int scope_depth_ = 0;
  // v0.7.0: Loop tracking for break/continue
  std::vector<std::size_t> loop_starts_;
  std::vector<std::vector<std::size_t>> break_patches_;
  std::vector<std::size_t> loop_local_counts_;  // locals count before loop scope (for cleanup on break)
};
}  // namespace neamc
