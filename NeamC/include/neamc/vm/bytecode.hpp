//
// Neam Virtual Machine - Bytecode definition
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <limits>
#include <string>
#include <vector>

#include "neamc/vm/value.hpp"

namespace neamc::vm
{
enum class OpCode : uint16_t
{
  OP_CONST = 0,
  OP_NIL,
  OP_TRUE,
  OP_FALSE,
  OP_POP,
  OP_DUP,
  OP_GET_LOCAL,
  OP_SET_LOCAL,
  OP_DEFINE_GLOBAL,
  OP_GET_GLOBAL,
  OP_SET_GLOBAL,

  OP_NEGATE,
  OP_NOT,

  OP_ADD,
  OP_SUB,
  OP_MUL,
  OP_DIV,
  OP_EQUAL,
  OP_GREATER,
  OP_LESS,

  OP_JUMP,
  OP_JUMP_IF_FALSE,
  OP_LOOP,

  OP_CALL,
  OP_CALL_NATIVE,
  OP_GET_PROPERTY,
  OP_GET_INDEX,
  OP_INVOKE,
  OP_AWAIT,
  OP_RETURN,

  OP_EMIT,
  OP_TRACE,

  OP_BUILD_LIST,
  OP_BUILD_MAP,
  OP_DEFINE_SKILL,
  OP_DEFINE_KNOWLEDGE,
  OP_DEFINE_AGENT,
  OP_GRANT,
  OP_CHECKPOINT,
  OP_REWIND,

  // v0.6.7: External skill adoption
  OP_DEFINE_EXTERN_SKILL,  // Stack: [name, desc, param_names, params, binding_type, binding_config]
  OP_DEFINE_MCP_SERVER,    // Stack: [name, config_map]
  OP_ADOPT_MCP_TOOLS,      // Stack: [server_name, filter_list, alias_or_nil]

  // v0.7.0: Data types & data processing
  OP_SET_INDEX,       // Stack: [base, index, value] -> [value]
  OP_GET_ITER,        // Stack: [collection] -> [iter_state]
  OP_FOR_ITER,        // Operand: uint16 exit_offset. Stack: [iter_state] -> [iter_state, next_val] or jump
  OP_BUILD_SET,       // Operand: uint16 count
  OP_BUILD_TUPLE,     // Operand: uint16 count
  OP_CONTAINS,        // Stack: [element, collection] -> [bool]
  OP_FORMAT_STRING,   // Operand: uint16 part_count
  OP_UNPACK,          // Operand: uint16 count
  OP_UNPACK_REST,     // Operands: uint16 before_count, uint16 after_count
  OP_SLICE,           // Operand: uint8 flags (bit0=has_start, bit1=has_end, bit2=has_step)

  // v0.7.1: OOP — struct + impl + method dispatch
  OP_DEFINE_STRUCT,   // Operand: uint16 name_const, uint8 field_count, uint8 is_mutable
  OP_IMPL_METHOD,     // Operand: uint16 type_name_const, uint16 method_name_const, uint8 is_static
  OP_CONSTRUCT_NAMED, // Operand: uint16 type_name_const, uint8 field_count
  OP_COPY_WITH,       // Operand: uint8 override_count
  OP_SET_PROPERTY,    // Operand: uint16 name_const

  // v0.7.1 Phase 2: trait + sealed + match
  OP_DEFINE_TRAIT,    // Operand: uint16 name_const, uint8 required_count, uint8 default_count
  OP_IMPL_TRAIT,      // Operand: uint16 trait_name_const, uint16 type_name_const, uint8 method_count
  OP_DEFINE_SEALED,   // Operand: uint16 name_const, uint8 variant_count
  OP_CONSTRUCT_VARIANT, // Operand: uint16 sealed_name_const, uint16 variant_name_const, uint8 arg_count
  OP_MATCH_START,     // Operand: uint8 arm_count (sets up match context)
  OP_MATCH_VARIANT,   // Operand: uint16 variant_name_const, uint8 bind_count, uint16 skip_offset
  OP_MATCH_WILDCARD,  // No operands (always matches)
  OP_MATCH_END,       // Cleanup match context

  // v0.7.1 Phase 5: Property observers
  OP_SET_FIELD_OBSERVER,  // Operand: uint16 type_name, uint16 field_name, uint8 kind (0=willSet, 1=didSet, 2=guard)

  // v0.8: Agent type system
  OP_DEFINE_CLAW_AGENT,   // Stack: [name, provider, model, endpoint|nil, api_key_env|nil, temperature|nil, system|nil, skills_list, knowledge_list, guardchains_list, policy|nil, budget|nil, env|nil, workspace|nil, session_config_map, channels_list, lanes_list, semantic_memory|nil]
  OP_DEFINE_FORGE_AGENT,  // Stack: [name, provider, model, endpoint|nil, api_key_env|nil, temperature|nil, system|nil, skills_list, guardchains_list, policy|nil, budget|nil, env|nil, workspace|nil, loop_config_map, verify_fn, checkpoint|nil]

  // v0.8 Phase 1: Runtime operation opcodes
  OP_DEFINE_CHANNEL,    // Stack: [name, config_map] -> []
  OP_WORKSPACE_READ,    // Stack: [path] -> [content]
  OP_WORKSPACE_WRITE,   // Stack: [path, content] -> [bool]
  OP_MEMORY_SEARCH,     // Stack: [query, top_k] -> [results_list]
  OP_SPAWN_AGENT,       // Stack: [agent_name, config_map] -> [result]
  OP_FORGE_ITERATE,     // Start next forge loop iteration -> []
  OP_VERIFY,            // Run forge verify callback -> [VerifyResult]
  OP_COMPACT,           // Trigger claw session compaction -> []
  OP_FLUSH,             // Flush claw memory to workspace -> []
  OP_SESSION_HISTORY,   // Stack: [key, limit] -> [history_list]
  OP_FORGE_RUN,         // Stack: [agent_name, config_map] -> [LoopOutcome]

  // v0.9: Data agent opcodes
  OP_DEFINE_DATA_AGENT,   // Stack: [field_count × (key, value)] -> register in globals
  OP_DEFINE_SOURCE,       // Stack: [field_count × (key, value)] -> register in globals
  OP_DEFINE_SINK,         // Stack: [field_count × (key, value)] -> register in globals
  OP_DEFINE_SCHEMA,       // Stack: [field_count × (key, value)] -> register in globals
  OP_DEFINE_COMPUTE,      // Stack: [field_count × (key, value)] -> register in globals
  OP_DEFINE_QUALITY,      // Stack: [field_count × (key, value)] -> register in globals
  OP_DEFINE_GOVERNANCE,   // Stack: [field_count × (key, value)] -> register in globals
  OP_DEFINE_CATALOG,      // Stack: [field_count × (key, value)] -> register in globals

  // v0.9.1: NeamETL
  OP_DEFINE_ETL_AGENT,
  OP_DEFINE_MART,
  OP_DEFINE_SEMANTIC,
  OP_SQL_TRANSPILE,
  OP_SQL_PUSHDOWN,
  OP_NL2SQL,
  OP_AUTO_MODEL,
  OP_SELF_HEAL,

  // v0.9.2: Migration Agent opcodes
  OP_DEFINE_MIGRATION_AGENT,
  OP_MIGRATION_ASSESS,
  OP_MIGRATION_PLAN_WAVES,
  OP_MIGRATION_TRANSLATE_SCHEMA,
  OP_MIGRATION_MOVE_DATA,
  OP_MIGRATION_RECONCILE,
  OP_MIGRATION_CUTOVER,
  OP_MIGRATION_ROLLBACK,
  OP_MIGRATION_SELF_HEAL,

  // v0.9.3: DataOps Agent opcodes
  OP_DEFINE_DATAOPS_AGENT,
  OP_DEFINE_SCHEDULER,
  OP_DEFINE_AUDIT_TABLE,
  OP_DEFINE_LOG_SOURCE,
  OP_DEFINE_PLATFORM,
  OP_DEFINE_INCIDENT_POLICY,
  OP_DEFINE_CORRELATION,
  OP_DATAOPS_START_MONITOR,
  OP_DATAOPS_STOP_MONITOR,
  OP_DATAOPS_TRIAGE,
  OP_DATAOPS_INVESTIGATE,
  OP_DATAOPS_REMEDIATE,

  // v0.9.4: Governance Agent opcodes
  OP_DEFINE_GOVERNANCE_AGENT,
  OP_DEFINE_CATALOG_SOURCE,
  OP_DEFINE_GOV_CATALOG,
  OP_DEFINE_GLOSSARY,
  OP_DEFINE_CLASSIFICATION_POLICY,
  OP_DEFINE_ACCESS_POLICY,
  OP_DEFINE_QUALITY_POLICY,
  OP_DEFINE_LINEAGE_POLICY,
  OP_DEFINE_COMPLIANCE_POLICY,
  OP_DEFINE_LIFECYCLE_POLICY,
  OP_DEFINE_DATA_PRODUCT,
  OP_DEFINE_CONTRACT_POLICY,
  OP_DEFINE_MASTER_DATA,
  OP_DEFINE_EXTERNAL_TOOL,
  OP_GOVERNANCE_CLASSIFY,
  OP_GOVERNANCE_TRACE_LINEAGE,

  // v0.9.5: Modeling Agent opcodes
  OP_DEFINE_SCHEMA_SOURCE,
  OP_DEFINE_ER_MODEL,
  OP_DEFINE_ENTITY_MODEL,
  OP_DEFINE_DIMENSIONAL_MODEL,
  OP_DEFINE_DATAMART_V095,
  OP_DEFINE_NORM_ANALYSIS,
  OP_DEFINE_AMENDMENT_CONFIG,
  OP_DEFINE_AMENDMENT,
  OP_DEFINE_DATA_PROFILE,
  OP_DEFINE_MODELING_TOOL,
  OP_DEFINE_MODELING_AGENT,

  // v0.9.6: Analyst Agent opcodes
  OP_DEFINE_SQL_CONNECTION,
  OP_DEFINE_DOMAIN_CONTEXT,
  OP_DEFINE_QUERY_TEMPLATE,
  OP_DEFINE_QUERY_OPTIMIZER,
  OP_DEFINE_EXECUTION_POLICY,
  OP_DEFINE_OUTPUT_FORMAT,
  OP_DEFINE_QUERY_LIBRARY,
  OP_DEFINE_ANALYSIS_SCHEDULE,
  OP_DEFINE_ANALYST_AGENT,
  OP_ANALYST_QUERY,
  OP_ANALYST_EXECUTE,
  OP_ANALYST_FORMAT,
  OP_ANALYST_OPTIMIZE,

  // v0.9.7: Data Pipeline Deployment opcodes
  OP_DEFINE_DEPLOY_TARGET,
  OP_DEFINE_PROMOTION_RULE,
  OP_DEFINE_ROLLBACK_POLICY,
  OP_DEFINE_ARTIFACT_REGISTRY,
  OP_DEFINE_DEPLOY_CONFIG,
  OP_DEPLOY_EXECUTE,
  OP_DEPLOY_ROLLBACK,

  // v0.9.8: Data Scientist Agent opcodes — DEFINE
  OP_DEFINE_PROBLEM_STATEMENT,
  OP_DEFINE_HYPOTHESIS_TEST,
  OP_DEFINE_FEATURE_ENGINEERING,
  OP_DEFINE_ML_EXPERIMENT,
  OP_DEFINE_AUTOML_CONFIG,
  OP_DEFINE_HYPERPARAMETER_CONFIG,
  OP_DEFINE_STACKED_MODEL,
  OP_DEFINE_EVALUATION_CONFIG,
  OP_DEFINE_DS_MODEL_REGISTRY,
  OP_DEFINE_EXPLAINABILITY_CONFIG,
  OP_DEFINE_CODE_INTERPRETER,
  OP_DEFINE_VENV_MANAGER,
  OP_DEFINE_NLP_PIPELINE,
  OP_DEFINE_CHURN_ANALYSIS,
  OP_DEFINE_CLV_MODEL,
  OP_DEFINE_PROPENSITY_MODEL,
  OP_DEFINE_RECOMMENDATION_ENGINE,
  OP_DEFINE_EXPERIMENT_DESIGN,
  OP_DEFINE_SCENARIO_ANALYSIS,
  OP_DEFINE_DECISION_SUPPORT,
  OP_DEFINE_EDA_CONFIG,
  OP_DEFINE_EDA_TECHNIQUE_SELECTOR,
  OP_DEFINE_SMART_CONNECTOR,
  OP_DEFINE_VOLUME_ROUTER,
  OP_DEFINE_COMPUTE_CONNECTOR,
  OP_DEFINE_FILE_CONNECTOR,
  OP_DEFINE_DISTRIBUTED_COMPUTE_CONFIG,
  OP_DEFINE_PERFORMANCE_CONFIG,
  OP_DEFINE_DATA_QUALITY_PIPELINE,
  OP_DEFINE_SELF_CORRECTION_CONFIG,
  OP_DEFINE_SELF_ASSESSMENT,
  OP_DEFINE_ADAPTIVE_KNOWLEDGE_CONFIG,
  OP_DEFINE_ANALYSIS_HISTORY,
  OP_DEFINE_OBSERVABILITY_CONFIG,
  OP_DEFINE_DATASCIENTIST_AGENT,

  // v0.9.8: Data Scientist Agent opcodes — ACTION
  OP_DS_DISCOVER_DATA,
  OP_DS_SENSE_VOLUME,
  OP_DS_RUN_EDA,
  OP_DS_FRAME_PROBLEM,
  OP_DS_TEST_HYPOTHESIS,
  OP_DS_ENGINEER_FEATURES,
  OP_DS_TRAIN_MODEL,
  OP_DS_EVALUATE_MODEL,
  OP_DS_EXPLAIN_MODEL,
  OP_DS_PREDICT,
  OP_DS_RECOMMEND,
  OP_DS_RUN_EXPERIMENT,
  OP_DS_SCENARIO,
  OP_DS_BUILD_VENV,
  OP_DS_EXEC_PYTHON,
  OP_DS_SUBMIT_SPARK,
  OP_DS_PUSHDOWN_SQL,

  // v0.9.8.1: Causal Agent opcodes — DEFINE
  OP_DEFINE_CAUSAL_DISCOVERY,
  OP_DEFINE_SCM,
  OP_DEFINE_INTERVENTION,
  OP_DEFINE_COUNTERFACTUAL,
  OP_DEFINE_BAYESIAN_MODEL,
  OP_DEFINE_CAUSAL_ESTIMATOR,
  OP_DEFINE_QUASI_EXPERIMENT,
  OP_DEFINE_CAUSAL_SENSITIVITY,
  OP_DEFINE_CAUSAL_DATA_REQUIREMENTS,
  OP_DEFINE_CAUSAL_AGENT,

  // v0.9.8.1: Causal Agent opcodes — ACTION
  OP_CAUSAL_DISCOVER,
  OP_CAUSAL_BUILD_SCM,
  OP_CAUSAL_DO,
  OP_CAUSAL_IDENTIFY,
  OP_CAUSAL_ESTIMATE,
  OP_CAUSAL_COUNTERFACTUAL,
  OP_CAUSAL_BAYESIAN_FIT,
  OP_CAUSAL_SENSITIVITY,
  OP_CAUSAL_EXPLAIN,
  OP_CAUSAL_VISUALIZE_DAG,

  // v0.9.8.2: MLOps Agent opcodes — DEFINE
  OP_DEFINE_DRIFT_MONITOR,
  OP_DEFINE_RETRAINING_PIPELINE,
  OP_DEFINE_ML_DEPLOY_STRATEGY,
  OP_DEFINE_CHAMPION_CHALLENGER,
  OP_DEFINE_SERVING_INFRA,
  OP_DEFINE_TRAINING_INFRA_MLOPS,
  OP_DEFINE_MLOPS_ROLLBACK,
  OP_DEFINE_MONITORING_STACK,
  OP_DEFINE_MLFLOW_CONFIG,
  OP_DEFINE_BUSINESS_KPI_TRACKER,
  OP_DEFINE_DATASET_VERSION,
  OP_DEFINE_FEEDBACK_LOOP,
  OP_DEFINE_DECISION_ENGINE,
  OP_DEFINE_EVENT_BUS,
  OP_DEFINE_DRIFT_RCA,
  OP_DEFINE_MLOPS_AGENT,

  // v0.9.8.2: MLOps Agent ACTION opcodes omitted — handled via native functions

  // v0.9.8.3: Data-BA Agent — consolidated opcode (saves opcode budget)
  // Sub-type byte follows: 0=elicitation, 1=brd, 2=functional, 3=nonfunctional,
  // 4=acceptance, 5=data_req, 6=impact, 7=trace, 8=etl_spec, 9=ml_spec,
  // 10=gov_spec, 11=analytics_spec, 12=stakeholder, 13=user_story,
  // 14=scope, 15=change_impact, 16=databa_agent
  OP_DEFINE_BA_DECLARATION,

  // v0.9.8.4: Data Testing Agent — consolidated opcode
  // Sub-type byte: 0=TestStrategy, 1=TestCaseGenerator, 2=TestCase, 3=ETLTestSuite,
  // 4=DWTestSuite, 5=MLTestSuite, 6=APITestSuite, 7=PerformanceTestSuite,
  // 8=EdgeCaseTests, 9=SITSuite, 10=UATSuite, 11=RegressionSuite,
  // 12=QualityGate, 13=TestReportConfig, 14=DefectManagement, 15=DataTestAgent
  OP_DEFINE_TEST_DECLARATION,

  // v0.9.9: Data Intelligent Orchestrator
  OP_DEFINE_DIO_DECLARATION,

  // ═══ v1.0: Enum widened to uint16_t for type safety ═══
  // Bytecode encoding: opcodes 0-255 remain single-byte for backward compat.
  // v1.0 uses enum values > 255 ONLY as internal identifiers.
  // In bytecode, v1.0 constructs are emitted using consolidated opcodes
  // from v0.9.8.3+ pattern (existing opcode + sub-type + field_count).

  // v1.0: OWASP Security (sub-types of OP_DEFINE_BA_DECLARATION or new consolidated)
  OP_DEFINE_GOAL_INTEGRITY = 256,
  OP_DEFINE_TOOL_VALIDATOR,
  OP_DEFINE_AGENT_IDENTITY,
  OP_DEFINE_SUPPLY_CHAIN_POLICY,
  OP_DEFINE_CODE_SANDBOX,
  OP_DEFINE_MEMORY_INTEGRITY,
  OP_DEFINE_MESSAGE_SECURITY,
  OP_DEFINE_CIRCUIT_BREAKER_DECL,
  OP_DEFINE_HUMAN_GATE,
  OP_DEFINE_AGENT_ATTESTATION,

  // v1.0: MCP Security
  OP_DEFINE_MCP_ALLOWLIST,
  OP_DEFINE_TOOL_PINNING,
  OP_DEFINE_CONTEXT_GUARD,

  // v1.0: NHI + AIBOM
  OP_DEFINE_AIBOM_CONFIG,

  // v1.0: Evaluation
  OP_DEFINE_GYM_EVALUATOR,

  // v1.0: Cloud Stack
  OP_DEFINE_GATEWAY,
  OP_DEFINE_MODEL_ROUTER,
  OP_DEFINE_MARKETPLACE_DECL,

  // v1.0: Special Agents
  OP_DEFINE_SECURITY_SENTINEL,
  OP_DEFINE_PROTOCOL_BRIDGE,
  OP_DEFINE_COST_GUARDIAN,
};

class Bytecode
{
public:
  struct SourceMapEntry
  {
    std::size_t offset = 0;
    std::size_t line = 0;
  };

  std::size_t add_constant(Value value);
  void write_op(OpCode op);
  void write_byte(uint8_t value);
  void write_short(uint16_t value);
  void emit_constant(Value value);
  void patch_short(std::size_t index, uint16_t value);

  void set_manifest(std::string manifest) { manifest_ = std::move(manifest); }
  const std::string& manifest() const { return manifest_; }
  void set_current_line(std::size_t line) { current_line_ = line; }
  void clear_source_map() { source_map_.clear(); }
  const std::vector<SourceMapEntry>& source_map() const { return source_map_; }

  void serialize(std::ostream& out) const;
  static Bytecode deserialize(std::istream& in);

  const std::vector<uint8_t>& code() const { return code_; }
  const std::vector<Value>& constants() const { return constants_; }

private:
  std::vector<uint8_t> code_{};
  std::vector<Value> constants_{};
  std::string manifest_{};
  std::vector<SourceMapEntry> source_map_{};
  std::size_t current_line_ = std::numeric_limits<std::size_t>::max();
};

using Chunk = Bytecode;
}  // namespace neamc::vm
