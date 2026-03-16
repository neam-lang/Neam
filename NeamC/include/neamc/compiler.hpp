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
  enum class AgentKind { Stateless, Claw, Forge, Data, ETL, Migration, DataOps, Governance, Modeling };
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
