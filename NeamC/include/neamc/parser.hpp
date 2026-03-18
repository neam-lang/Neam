//
// NeamC - Minimal parser for arithmetic expressions and blocks
//

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "neamc/ast.hpp"

namespace neamc
{
enum class TokenType
{
  // Single-character tokens.
  LeftParen,
  RightParen,
  LeftBrace,
  RightBrace,
  Comma,
  Semicolon,
  Colon,
  Hash,
  Minus,
  Plus,
  Slash,
  Star,
  Dot,
  LeftBracket,
  RightBracket,
  Question,
  Dollar,

  // One or two character tokens.
  Bang,
  BangEqual,
  Equal,
  EqualEqual,
  Greater,
  GreaterEqual,
  Less,
  LessEqual,

  // Literals.
  Identifier,
  String,
  Number,
  PathLiteral,

  // Keywords.
  Let,
  If,
  Else,
  While,
  Fun,
  Return,
  Emit,
  Module,
  Import,
  Use,
  Pub,
  Crate,
  Super,
  Const,
  Type,
  Panic,
  CatchPanic,
  Test,
  Suite,
  With,
  As,
  BeforeEach,
  AfterEach,
  BeforeAll,
  AfterAll,
  AssertEq,
  AssertNe,
  AssertTrue,
  AssertFalse,
  AssertSome,
  AssertNone,
  AssertOk,
  AssertErr,
  AssertThrows,
  Ignore,
  Async,
  ShouldPanic,
  Timeout,
  Knowledge,
  Skill,
  Agent,
  Description,
  Params,
  Impl,
  VectorStore,
  EmbeddingModel,
  ChunkSize,
  ChunkOverlap,
  Sources,
  RetrievalStrategy,
  TopK,
  RelevanceThreshold,
  MmrLambda,
  NumHypothetical,
  EnableRelevanceCheck,
  EnableSupportCheck,
  EnableWebFallback,
  EnableQueryDecomposition,
  MaxCorrections,
  MaxIterations,
  EnableReflection,
  SearchDepth,
  IncludeCommunities,
  Provider,
  Model,
  Endpoint,
  ApiKeyEnv,
  Temperature,
  System,
  Skills,
  ConnectedKnowledge,
  Budget,
  Guard,
  GuardChain,
  Capability,
  Grant,
  Tool,
  Memory,
  Checkpoint,
  Rewind,
  Env,
  Connector,
  WorldModel,
  Plan,
  Subagent,
  Requires,
  Guards,
  BudgetCost,
  Capabilities,
  Returns,
  OnObservation,
  OnAction,
  OnToolInput,
  OnToolOutput,
  OnToolCall,
  OnResult,
  To,
  True,
  False,
  Nil,
  DocComment,

  // v0.6.7: External skill adoption
  Extern,
  McpServer,
  Adopt,
  Binding,
  Mcp,
  Http,
  ClaudeBuiltin,
  Method,
  Url,
  Headers,
  ResponsePath,
  Server,
  Command,
  Args,
  BodyTemplate,  // v0.6.8: HTTP body template

  // v0.6.9: Security policy keyword
  Policy,

  // v0.7.0: Data types keywords and operators
  For,
  In,
  Break,
  Continue,
  Not,
  Pipe,         // |>
  DotDotDot,    // ...

  // v0.7.1: OOP — struct, impl blocks, method dispatch
  Struct,
  Fn,
  SelfKw,
  Mut,
  Arrow,        // ->

  // v0.7.1 Phase 2-5: trait, sealed, match, extend, derive, agentic patterns
  Trait,        // "trait"
  Sealed,       // "sealed"
  Match,        // "match"
  FatArrow,     // "=>"
  Underscore,   // "_"
  Extend,       // "extend"
  Derive,       // "derive" (inside #[derive(...)])
  Pipeline,     // "pipeline"
  Dispatch,     // "dispatch"
  Parallel,     // "parallel"
  LoopPattern,  // "loop" (agentic loop pattern)
  WillSet,      // "willSet"
  DidSet,       // "didSet"

  // v0.8: Agent type keywords
  Claw,         // "claw" — prefix for claw agent
  Forge,        // "forge" — prefix for forge agent
  Channel,      // "channel" — channel declaration
  Spawn,        // "spawn" — spawn sub-agent

  // v0.9.1: NeamETL keywords
  Mart,           // "mart" — dimensional model declaration
  Semantic,       // "semantic" — semantic layer declaration
  Warehouse,      // "warehouse" — warehouse reference

  // v0.9.2: Migration Agent keywords
  Assessment,     // "assessment" — source profiling configuration block
  Cutover,        // "cutover" — traffic switching strategy block
  Migration,      // "migration" — first word in 'migration agent' two-keyword declaration
  Movement,       // "movement" — data movement strategy block
  Waves,          // "waves" — migration wave configuration block

  // v0.9: Data agent keywords (contextual — NOT reserved in keyword map)
  // "data", "schema", "source", "sink", "quality", "compute", "governance", "catalog"
  // are handled via match_identifier() to remain usable as variable names

  Eof
};

struct Token
{
  TokenType type;
  std::string lexeme;
  std::size_t position = 0;
  std::size_t line = 0;
  std::size_t column = 0;
};

class Parser
{
public:
  explicit Parser(std::string source);

  Program parse();

private:
  bool is_at_end() const;
  const Token& peek() const;
  const Token& previous() const;
  const Token& advance();
  bool check(TokenType type) const;
  bool match(TokenType type);
  [[noreturn]] void error(const std::string& message) const;

  void tokenize();

  StmtPtr parse_statement();
  StmtPtr parse_declaration();
  StmtPtr parse_return();
  StmtPtr parse_emit();
  StmtPtr parse_function(const Visibility& visibility);
  StmtPtr parse_skill(const Visibility& visibility);
  StmtPtr parse_extern_skill(const Visibility& visibility);
  StmtPtr parse_mcp_server();
  StmtPtr parse_adopt_statement();
  SkillBindingSpec parse_skill_binding();
  StmtPtr parse_knowledge(const Visibility& visibility);
  StmtPtr parse_agent(const Visibility& visibility);
  StmtPtr parse_claw_agent(const Visibility& visibility);
  StmtPtr parse_forge_agent(const Visibility& visibility);
  StmtPtr parse_channel_decl();
  // v0.9: Data agent declarations
  StmtPtr parse_data_agent(const Visibility& visibility);
  StmtPtr parse_schema(const Visibility& visibility);
  StmtPtr parse_source(const Visibility& visibility);
  StmtPtr parse_sink(const Visibility& visibility);
  StmtPtr parse_quality(const Visibility& visibility);
  StmtPtr parse_compute(const Visibility& visibility);
  StmtPtr parse_governance(const Visibility& visibility);
  StmtPtr parse_catalog(const Visibility& visibility);
  // v0.9.1: ETL agent declarations
  StmtPtr parse_etl_agent(const Visibility& visibility);
  StmtPtr parse_semantic(const Visibility& visibility);
  MartDecl parse_mart();
  // v0.9.2: Migration agent declarations
  StmtPtr parse_migration_agent(const Visibility& visibility);
  // v0.9.3: DataOps agent declarations
  StmtPtr parse_dataops_agent(const Visibility& visibility);
  StmtPtr parse_scheduler_decl(const Visibility& visibility);
  StmtPtr parse_audit_table_decl(const Visibility& visibility);
  StmtPtr parse_log_source_decl(const Visibility& visibility);
  StmtPtr parse_platform_decl(const Visibility& visibility);
  StmtPtr parse_incident_policy_decl(const Visibility& visibility);
  StmtPtr parse_correlation_decl(const Visibility& visibility);
  // v0.9.4: Governance agent declarations
  StmtPtr parse_governance_agent(const Visibility& visibility);
  StmtPtr parse_gov_catalog_source(const Visibility& visibility);
  StmtPtr parse_gov_catalog(const Visibility& visibility);
  StmtPtr parse_glossary(const Visibility& visibility);
  StmtPtr parse_classification_policy(const Visibility& visibility);
  StmtPtr parse_access_policy(const Visibility& visibility);
  StmtPtr parse_quality_policy(const Visibility& visibility);
  StmtPtr parse_lineage_policy(const Visibility& visibility);
  StmtPtr parse_compliance_policy(const Visibility& visibility);
  StmtPtr parse_lifecycle_policy(const Visibility& visibility);
  StmtPtr parse_data_product(const Visibility& visibility);
  StmtPtr parse_contract_policy(const Visibility& visibility);
  StmtPtr parse_master_data(const Visibility& visibility);
  StmtPtr parse_gov_external_tool(const Visibility& visibility);
  // v0.9.5: Modeling agent declarations
  StmtPtr parse_schema_source_decl(const Visibility& visibility);
  StmtPtr parse_er_model_decl(const Visibility& visibility);
  StmtPtr parse_modeling_entity_decl(const Visibility& visibility);
  StmtPtr parse_dimensional_model_decl(const Visibility& visibility);
  StmtPtr parse_datamart_v095_decl(const Visibility& visibility);
  StmtPtr parse_normalization_analysis_decl(const Visibility& visibility);
  StmtPtr parse_amendment_config_decl(const Visibility& visibility);
  StmtPtr parse_amendment_decl(const Visibility& visibility);
  StmtPtr parse_data_profile_decl(const Visibility& visibility);
  StmtPtr parse_modeling_tool_decl(const Visibility& visibility);
  StmtPtr parse_modeling_agent_decl(const Visibility& visibility);
  // v0.9.6: Analyst agent declarations
  StmtPtr parse_sql_connection_decl(const Visibility& visibility);
  StmtPtr parse_domain_context_decl(const Visibility& visibility);
  StmtPtr parse_query_template_decl(const Visibility& visibility);
  StmtPtr parse_query_optimizer_decl(const Visibility& visibility);
  StmtPtr parse_execution_policy_decl(const Visibility& visibility);
  StmtPtr parse_output_format_decl(const Visibility& visibility);
  StmtPtr parse_query_library_decl(const Visibility& visibility);
  StmtPtr parse_analysis_schedule_decl(const Visibility& visibility);
  StmtPtr parse_analyst_agent_decl(const Visibility& visibility);

  // v0.9.7: Data Pipeline Deployment declarations
  StmtPtr parse_deploy_target_decl(const Visibility& visibility);
  StmtPtr parse_promotion_rule_decl(const Visibility& visibility);
  StmtPtr parse_rollback_policy_decl(const Visibility& visibility);
  StmtPtr parse_artifact_registry_decl(const Visibility& visibility);
  StmtPtr parse_deploy_config_decl(const Visibility& visibility);

  // v0.9.8: Data Scientist Agent declarations
  StmtPtr parse_problem_statement_decl(const Visibility& visibility);
  StmtPtr parse_hypothesis_test_decl(const Visibility& visibility);
  StmtPtr parse_feature_engineering_decl(const Visibility& visibility);
  StmtPtr parse_ml_experiment_decl(const Visibility& visibility);
  StmtPtr parse_automl_config_decl(const Visibility& visibility);
  StmtPtr parse_hyperparameter_config_decl(const Visibility& visibility);
  StmtPtr parse_stacked_model_decl(const Visibility& visibility);
  StmtPtr parse_evaluation_config_decl(const Visibility& visibility);
  StmtPtr parse_model_registry_decl(const Visibility& visibility);
  StmtPtr parse_explainability_config_decl(const Visibility& visibility);
  StmtPtr parse_code_interpreter_decl(const Visibility& visibility);
  StmtPtr parse_venv_manager_decl(const Visibility& visibility);
  StmtPtr parse_nlp_pipeline_decl(const Visibility& visibility);
  StmtPtr parse_churn_analysis_decl(const Visibility& visibility);
  StmtPtr parse_clv_model_decl(const Visibility& visibility);
  StmtPtr parse_propensity_model_decl(const Visibility& visibility);
  StmtPtr parse_recommendation_engine_decl(const Visibility& visibility);
  StmtPtr parse_experiment_design_decl(const Visibility& visibility);
  StmtPtr parse_scenario_analysis_decl(const Visibility& visibility);
  StmtPtr parse_decision_support_decl(const Visibility& visibility);
  StmtPtr parse_eda_config_decl(const Visibility& visibility);
  StmtPtr parse_eda_technique_selector_decl(const Visibility& visibility);
  StmtPtr parse_smart_connector_decl(const Visibility& visibility);
  StmtPtr parse_volume_router_decl(const Visibility& visibility);
  StmtPtr parse_compute_connector_decl(const Visibility& visibility);
  StmtPtr parse_file_connector_decl(const Visibility& visibility);
  StmtPtr parse_distributed_compute_config_decl(const Visibility& visibility);
  StmtPtr parse_performance_config_decl(const Visibility& visibility);
  StmtPtr parse_data_quality_pipeline_decl(const Visibility& visibility);
  StmtPtr parse_self_correction_config_decl(const Visibility& visibility);
  StmtPtr parse_self_assessment_decl(const Visibility& visibility);
  StmtPtr parse_adaptive_knowledge_config_decl(const Visibility& visibility);
  StmtPtr parse_analysis_history_decl(const Visibility& visibility);
  StmtPtr parse_observability_config_decl(const Visibility& visibility);
  StmtPtr parse_datascientist_agent_decl(const Visibility& visibility);

  // v0.9.8.1: Causal Agent declarations
  StmtPtr parse_causal_discovery_decl(const Visibility& visibility);
  StmtPtr parse_scm_decl(const Visibility& visibility);
  StmtPtr parse_intervention_decl(const Visibility& visibility);
  StmtPtr parse_counterfactual_decl(const Visibility& visibility);
  StmtPtr parse_bayesian_model_decl(const Visibility& visibility);
  StmtPtr parse_causal_estimator_decl(const Visibility& visibility);
  StmtPtr parse_quasi_experiment_decl(const Visibility& visibility);
  StmtPtr parse_sensitivity_analysis_decl(const Visibility& visibility);
  StmtPtr parse_causal_data_requirements_decl(const Visibility& visibility);
  StmtPtr parse_causal_agent_decl(const Visibility& visibility);

  // v0.9.4: Governance sub-block parsers
  AutoClassifyConfig parse_auto_classify_config();
  SemanticClassifyConfig parse_semantic_classify_config();
  DriftDetectionConfig parse_drift_detection_config();
  TagPropagationConfig parse_tag_propagation_config();
  AutoSuggestConfig parse_auto_suggest_config();
  SynonymDetectionConfig parse_synonym_detection_config();
  AutoDocumentConfig parse_auto_document_config();
  OwnershipConfig parse_ownership_config();
  AccessReviewConfig parse_access_review_config();
  GovProfilingConfig parse_gov_profiling_config();
  QualityScoringConfig parse_quality_scoring_config();
  LineageDiscoveryConfig parse_lineage_discovery_config();
  ImpactAnalysisConfig parse_impact_analysis_config();
  LineageTagPropagationConfig parse_lineage_tag_propagation_config();
  ComplianceMonitoringConfig parse_compliance_monitoring_config();
  DSARConfig parse_dsar_config();
  GovTieringConfig parse_gov_tiering_config();
  LegalHoldConfig parse_legal_hold_config();
  CostOptimizationConfig parse_cost_optimization_config();
  MatchingConfig parse_matching_config();
  StewardshipConfig parse_stewardship_config();
  DataContractConfig parse_data_contract_config();
  GovernanceReportsConfig parse_governance_reports_config();
  std::string consume_nested_block_as_json();
  AuditColumnMap parse_audit_column_map();
  AuditAnomalyConfig parse_audit_anomaly_config();
  LogAlertConfig parse_log_alert_config();
  PlatformHealthConfig parse_platform_health_config();
  PlatformFinOpsConfig parse_platform_finops_config();
  std::vector<SeverityLevel> parse_severity_levels();
  AutoHealConfig parse_auto_heal_config();
  CorrelationScope parse_correlation_scope();
  CorrelationSLAConfig parse_correlation_sla_config();
  MigrationStrategy parse_migration_strategy();
  WaveConfig parse_wave_config();
  MovementConfig parse_movement_config();
  CDCConfig parse_cdc_config();
  SchemaTranslationConfig parse_schema_translation_config();
  SchemaTranslationConfig::PlatformSpecific parse_platform_specific();
  ValidationConfig parse_validation_config();
  ReconciliationConfig parse_reconciliation_config();
  ToleranceConfig parse_tolerance_config();
  CutoverConfig parse_cutover_config();
  RollbackConfig parse_rollback_config();
  SelfHealMigrationConfig parse_self_heal_migration_config();
  SelfHealGuardrails parse_guardrails_block();
  AssessmentConfig parse_assessment_config();
  GovernanceMigrationConfig parse_governance_migration_block();
  LayersBlock parse_layers_block();
  LayerConfig parse_layer_config();
  IncrementalBlock parse_incremental_block();
  SelfHealBlock parse_self_heal_block();
  AutoModelBlock parse_auto_model_block();
  bool match_keyword_as_identifier();
  PipelineBlock parse_pipeline_block();
  DataAgentComputeBlock parse_data_agent_compute_block();
  ExprPtr parse_config_block();
  std::vector<std::string> parse_string_list();
  void consume_optional_comma();
  // v0.8 config helpers
  SessionConfig parse_session_config();
  LoopConfig parse_loop_config();
  SemanticMemoryConfig parse_semantic_memory_config();
  std::vector<LaneConfig> parse_lane_configs();
  bool match_identifier(const std::string& name);
  void warning(const std::string& message) const;
  StmtPtr parse_budget(const Visibility& visibility);
  StmtPtr parse_guard(const Visibility& visibility);
  StmtPtr parse_guardchain(const Visibility& visibility);
  StmtPtr parse_policy(const Visibility& visibility);
  StmtPtr parse_capability(const Visibility& visibility);
  StmtPtr parse_tool(const Visibility& visibility);
  StmtPtr parse_memory(const Visibility& visibility);
  StmtPtr parse_env(const Visibility& visibility);
  StmtPtr parse_connector(const Visibility& visibility);
  StmtPtr parse_world_model(const Visibility& visibility);
  StmtPtr parse_plan(const Visibility& visibility);
  StmtPtr parse_subagent(const Visibility& visibility);
  StmtPtr parse_module_decl();
  StmtPtr parse_import_decl(const Visibility& visibility, bool is_reexport);
  StmtPtr parse_const_decl(const Visibility& visibility);
  StmtPtr parse_type_alias(const Visibility& visibility);
  StmtPtr parse_doc_comment(Token first_token);
  StmtPtr parse_test_decl_statement();
  StmtPtr parse_test_suite_statement();
  StmtPtr parse_with_statement();
  StmtPtr parse_assert_statement(TokenType type);
  StmtPtr parse_grant_statement();
  StmtPtr parse_checkpoint_statement();
  StmtPtr parse_rewind_statement();
  StmtPtr parse_let();
  StmtPtr parse_for_in();
  StmtPtr parse_break_stmt();
  StmtPtr parse_continue_stmt();
  StmtPtr parse_struct_decl(const Visibility& visibility);
  StmtPtr parse_impl_block();
  StmtPtr parse_trait_decl(const Visibility& visibility);
  StmtPtr parse_sealed_decl(const Visibility& visibility);
  StmtPtr parse_extend_block();
  StmtPtr parse_pipeline_decl();
  StmtPtr parse_dispatch_decl();
  StmtPtr parse_parallel_decl();
  StmtPtr parse_loop_pattern_decl();
  ExprPtr parse_match_expr();
  StmtPtr parse_block();
  BlockStmt parse_block_node();
  StmtPtr parse_if();
  StmtPtr parse_while();
  TestAttribute parse_test_attribute();
  std::vector<TestAttribute> parse_test_attributes();
  std::unique_ptr<TestDecl> parse_test_decl_node(std::vector<TestAttribute> attributes);
  std::unique_ptr<TestSuiteDecl> parse_test_suite_node();
  SkillParam parse_skill_param();
  ToolParam parse_tool_param();
  BudgetCost parse_budget_cost();
  std::unique_ptr<GuardHandler> parse_guard_handler();
  std::vector<SkillParam> parse_skill_params();
  std::vector<IdentifierRef> parse_identifier_list();
  std::vector<KnowledgeSource> parse_knowledge_sources();
  BudgetDimension parse_budget_dimension();
  Visibility parse_visibility();
  std::vector<std::string> parse_module_path();
  std::vector<std::string> parse_import_items();
  ExprPtr parse_expression();
  ExprPtr parse_assignment();
  ExprPtr parse_pipe();
  ExprPtr parse_equality();
  ExprPtr parse_contains();
  ExprPtr parse_comparison();
  ExprPtr parse_term();
  ExprPtr parse_factor();
  ExprPtr parse_unary();
  ExprPtr parse_call();
  ExprPtr parse_primary();
  ExprPtr parse_fstring(const std::string& raw);
  std::unique_ptr<TypeExpression> parse_type_expression();

  std::string source_;
  std::vector<Token> tokens_{};
  std::size_t current_ = 0;
};
}  // namespace neamc
