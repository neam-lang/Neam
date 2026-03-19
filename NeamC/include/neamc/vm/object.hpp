//
// Neam Virtual Machine - Object system
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "neamc/vm/bytecode.hpp"
#include "neamc/vm/external_skill.hpp"
#include "neamc/vm/knowledge.hpp"
#include "neamc/vm/value.hpp"
#include "neamc/vm/value_hash.hpp"

namespace neamc::vm::async
{
template <typename T>
class Future;
}

namespace neamc::vm
{
class VirtualMachine;
struct Obj
{
  ObjType type;
  bool marked{false};
  Obj* next{nullptr};
};

enum class ObjType
{
  OBJ_STRING,
  OBJ_FUNCTION,
  OBJ_NATIVE,
  OBJ_LIST,
  OBJ_MAP,
  OBJ_SKILL,
  OBJ_AGENT,
  OBJ_CONTEXT,
  OBJ_ENV,
  OBJ_KNOWLEDGE,
  OBJ_OPTION,
  OBJ_FUTURE,
  // v0.7.0: Data types
  OBJ_RANGE,
  OBJ_ITER_STATE,
  OBJ_SET,
  OBJ_TUPLE,
  // v0.7.1: OOP
  OBJ_STRUCT_DEF,
  OBJ_STRUCT,
  OBJ_IMPL_TABLE,
  // v0.7.1 Phase 2: trait + sealed
  OBJ_TRAIT_DEF,
  OBJ_SEALED_DEF,
  OBJ_VARIANT,
  // v0.8: Agent type system
  OBJ_CLAW_AGENT,
  OBJ_FORGE_AGENT,
  OBJ_CHANNEL,
  OBJ_WORKSPACE,
  OBJ_LOOP_CONTEXT,
  // v0.9: Data agent type system
  OBJ_DATA_AGENT,
  OBJ_SOURCE,
  OBJ_SINK,
  OBJ_SCHEMA,
  OBJ_QUALITY_GATE,
  OBJ_COMPUTE_ENGINE,
  OBJ_GOVERNANCE_POLICY,
  OBJ_CATALOG,
  // v0.9.1: ETLAgent object types
  OBJ_ETL_AGENT,
  OBJ_MART,
  OBJ_SEMANTIC_LAYER,
  OBJ_SQL_PLAN,
  OBJ_MODEL_PROPOSAL,
  // v0.9.2: Migration Agent runtime types
  OBJ_MIGRATION_AGENT,
  OBJ_GOVERNANCE,
  OBJ_WAVE_PLAN,
  OBJ_MIGRATION_OBJECT,
  OBJ_RECONCILIATION_RESULT,
  OBJ_SCHEMA_MAP,
  OBJ_MIGRATION_CHECKPOINT,
  // v0.9.3: DataOps Agent runtime types
  OBJ_DATAOPS_AGENT,
  OBJ_SCHEDULER,
  OBJ_AUDIT_TABLE,
  OBJ_LOG_SOURCE,
  OBJ_PLATFORM_MONITOR,
  OBJ_INCIDENT_POLICY,
  OBJ_CORRELATION,
  OBJ_INCIDENT,
  // v0.9.4: Governance Agent runtime types
  OBJ_GOVERNANCE_AGENT,
  OBJ_GOV_CATALOG_SOURCE,
  OBJ_GOV_CATALOG,
  OBJ_GLOSSARY,
  OBJ_CLASSIFICATION_POLICY,
  OBJ_ACCESS_POLICY,
  OBJ_QUALITY_POLICY,
  OBJ_LINEAGE_POLICY,
  OBJ_COMPLIANCE_POLICY,
  OBJ_LIFECYCLE_POLICY,
  OBJ_DATA_PRODUCT,
  OBJ_CONTRACT_POLICY,
  OBJ_MASTER_DATA,
  OBJ_GOV_EXTERNAL_TOOL,
  // v0.9.5: Modeling Agent runtime types
  OBJ_SCHEMA_SOURCE,
  OBJ_ER_MODEL,
  OBJ_ENTITY,
  OBJ_DIMENSIONAL_MODEL,
  OBJ_DATAMART_V095,
  OBJ_NORM_ANALYSIS,
  OBJ_AMENDMENT_CONFIG,
  OBJ_AMENDMENT,
  OBJ_DATA_PROFILE,
  OBJ_MODELING_TOOL,
  OBJ_MODELING_AGENT,
  // v0.9.6: Analyst Agent runtime types
  OBJ_SQL_CONNECTION,
  OBJ_DOMAIN_CONTEXT,
  OBJ_QUERY_TEMPLATE,
  OBJ_QUERY_OPTIMIZER,
  OBJ_EXECUTION_POLICY,
  OBJ_OUTPUT_FORMAT,
  OBJ_QUERY_LIBRARY,
  OBJ_ANALYSIS_SCHEDULE,
  OBJ_ANALYST_AGENT,
  // v0.9.7: Data Pipeline Deployment runtime types
  OBJ_DEPLOY_TARGET,
  OBJ_PROMOTION_RULE,
  OBJ_ROLLBACK_POLICY,
  OBJ_ARTIFACT_REGISTRY,
  OBJ_DEPLOY_CONFIG,

  // v0.9.8: Data Scientist Agent runtime types
  OBJ_PROBLEM_STATEMENT,
  OBJ_HYPOTHESIS_TEST,
  OBJ_FEATURE_ENGINEERING,
  OBJ_ML_EXPERIMENT,
  OBJ_AUTOML_CONFIG,
  OBJ_HYPERPARAMETER_CONFIG,
  OBJ_STACKED_MODEL,
  OBJ_EVALUATION_CONFIG,
  OBJ_DS_MODEL_REGISTRY,
  OBJ_EXPLAINABILITY_CONFIG,
  OBJ_CODE_INTERPRETER,
  OBJ_VENV_MANAGER,
  OBJ_NLP_PIPELINE,
  OBJ_CHURN_ANALYSIS,
  OBJ_CLV_MODEL,
  OBJ_PROPENSITY_MODEL,
  OBJ_RECOMMENDATION_ENGINE,
  OBJ_EXPERIMENT_DESIGN,
  OBJ_SCENARIO_ANALYSIS,
  OBJ_DECISION_SUPPORT,
  OBJ_EDA_CONFIG,
  OBJ_EDA_TECHNIQUE_SELECTOR,
  OBJ_SMART_CONNECTOR,
  OBJ_VOLUME_ROUTER,
  OBJ_COMPUTE_CONNECTOR,
  OBJ_FILE_CONNECTOR,
  OBJ_DISTRIBUTED_COMPUTE_CONFIG,
  OBJ_PERFORMANCE_CONFIG,
  OBJ_DATA_QUALITY_PIPELINE,
  OBJ_SELF_CORRECTION_CONFIG,
  OBJ_SELF_ASSESSMENT,
  OBJ_ADAPTIVE_KNOWLEDGE_CONFIG,
  OBJ_ANALYSIS_HISTORY,
  OBJ_OBSERVABILITY_CONFIG,
  OBJ_DATASCIENTIST_AGENT,

  // v0.9.8.1: Causal Agent runtime types
  OBJ_CAUSAL_DISCOVERY,
  OBJ_SCM,
  OBJ_INTERVENTION,
  OBJ_COUNTERFACTUAL,
  OBJ_BAYESIAN_MODEL,
  OBJ_CAUSAL_ESTIMATOR,
  OBJ_QUASI_EXPERIMENT,
  OBJ_CAUSAL_SENSITIVITY,
  OBJ_CAUSAL_DATA_REQUIREMENTS,
  OBJ_CAUSAL_AGENT,

  // v0.9.8.2: MLOps Agent runtime types
  OBJ_DRIFT_MONITOR,
  OBJ_RETRAINING_PIPELINE,
  OBJ_ML_DEPLOY_STRATEGY,
  OBJ_CHAMPION_CHALLENGER,
  OBJ_SERVING_INFRA,
  OBJ_TRAINING_INFRA_MLOPS,
  OBJ_MLOPS_ROLLBACK,
  OBJ_MONITORING_STACK,
  OBJ_MLFLOW_CONFIG,
  OBJ_BUSINESS_KPI_TRACKER,
  OBJ_DATASET_VERSION,
  OBJ_FEEDBACK_LOOP,
  OBJ_DECISION_ENGINE,
  OBJ_EVENT_BUS,
  OBJ_DRIFT_RCA,
  OBJ_MLOPS_AGENT,

  // v0.9.8.3: Data Business Analyst Agent runtime types
  OBJ_REQUIREMENTS_ELICITATION,
  OBJ_BRD_GENERATOR,
  OBJ_FUNCTIONAL_SPEC,
  OBJ_NONFUNCTIONAL_SPEC,
  OBJ_ACCEPTANCE_CRITERIA_GEN,
  OBJ_DATA_REQUIREMENTS_BA,
  OBJ_IMPACT_ANALYSIS_BA,
  OBJ_TRACEABILITY_MATRIX,
  OBJ_ETL_REQUIREMENT_SPEC,
  OBJ_ML_REQUIREMENT_SPEC,
  OBJ_GOVERNANCE_REQUIREMENT_SPEC,
  OBJ_ANALYTICS_REQUIREMENT_SPEC,
  OBJ_STAKEHOLDER_ANALYSIS,
  OBJ_USER_STORY_GENERATOR,
  OBJ_SCOPE_MANAGEMENT,
  OBJ_CHANGE_IMPACT_ANALYZER,
  OBJ_DATABA_AGENT,

  // v0.9.8.4: Data Testing Agent runtime types
  OBJ_TEST_STRATEGY,
  OBJ_TEST_CASE_GENERATOR,
  OBJ_TEST_CASE_DEF,
  OBJ_ETL_TEST_SUITE,
  OBJ_DW_TEST_SUITE,
  OBJ_ML_TEST_SUITE,
  OBJ_API_TEST_SUITE,
  OBJ_PERFORMANCE_TEST_SUITE,
  OBJ_EDGE_CASE_TESTS,
  OBJ_SIT_SUITE,
  OBJ_UAT_SUITE,
  OBJ_REGRESSION_SUITE,
  OBJ_QUALITY_GATE_TEST,
  OBJ_TEST_REPORT_CONFIG,
  OBJ_DEFECT_MANAGEMENT,
  OBJ_DATATEST_AGENT,

  // v0.9.9: Data Intelligent Orchestrator runtime types
  OBJ_AGENT_REGISTRY, OBJ_AGENT_CONTRACTS, OBJ_RACI_MATRIX,
  OBJ_TASK_UNDERSTANDING, OBJ_TASK_DECOMPOSER, OBJ_CREW_FORMATION,
  OBJ_PATTERN_SELECTOR, OBJ_EXECUTION_MANAGER_DIO, OBJ_DIO_STATE_MACHINE,
  OBJ_DIO_ERROR_HANDLING, OBJ_RESULT_SYNTHESIZER, OBJ_INFRASTRUCTURE_PROFILE,
  OBJ_ROLE_FRAMEWORK, OBJ_DELEGATION_PROTOCOL, OBJ_DIO_ACCOUNTABILITY,
  OBJ_DIO_AGENT,
};

struct ObjString : Obj
{
  std::size_t length{0};
  char* chars{nullptr};
  uint32_t hash{0};
};

struct ObjFunction : Obj
{
  int arity{0};
  Chunk chunk;
  ObjString* name{nullptr};
};

using NativeFn = Value (*)(VirtualMachine& vm, int argCount, Value* args);

struct ObjNative : Obj
{
  int arity{0};
  ObjString* name{nullptr};
  NativeFn function{nullptr};
};

struct ObjList : Obj
{
  std::vector<Value> items;
};

struct ObjMap : Obj
{
  std::unordered_map<std::string, Value> entries;
};

struct ObjSkill : Obj
{
  ObjString* name{nullptr};
  ObjString* description{nullptr};
  ObjMap* params{nullptr};
  std::vector<std::string> param_names;
  ObjFunction* impl{nullptr};              // Non-null for local skills
  ExternalSkillConfig* external{nullptr};   // Non-null for external skills (v0.6.7)
  bool sensitive{false};                    // v0.6.9 D10: requires human confirmation
};

struct ObjContext : Obj
{
  struct Message
  {
    std::string role;
    std::string content;
  };
  std::vector<Message> history;
};

struct ObjAgent : Obj
{
  ObjString* name{nullptr};
  ObjString* provider{nullptr};
  ObjString* model{nullptr};
  ObjString* endpoint{nullptr};
  ObjString* api_key_env{nullptr};
  ObjString* system{nullptr};
  double temperature{0.0};
  ObjList* skills{nullptr};
  ObjList* connected_knowledge{nullptr};
  ObjContext* context{nullptr};
};

struct ObjEnv : Obj
{
  std::unordered_set<std::string> allowed_skills;
  std::unordered_map<std::string, std::string> config;
};

// Retrieval strategy enum for the VM
enum class RetrievalStrategy
{
  kBasic = 0,
  kMMR = 1,
  kHybrid = 2,
  kHyDE = 3,
  kSelfRAG = 4,
  kCRAG = 5,
  kAgentic = 6,
  kGraphRAG = 7
};

// Strategy options for advanced RAG
struct RetrievalStrategyOptions
{
  std::size_t top_k{4};
  double relevance_threshold{0.5};
  double mmr_lambda{0.5};
  std::size_t num_hypothetical{1};
  bool enable_relevance_check{true};
  bool enable_support_check{true};
  bool enable_web_fallback{false};
  bool enable_query_decomposition{true};
  std::size_t max_corrections{2};
  std::size_t max_iterations{5};
  bool enable_reflection{true};
  std::size_t search_depth{2};
  bool include_communities{true};
};

struct ObjKnowledge : Obj
{
  ObjString* name{nullptr};
  ObjString* vector_store{nullptr};
  ObjString* embedding_model{nullptr};
  std::size_t chunk_size{0};
  std::size_t chunk_overlap{0};
  std::vector<knowledge::Source> sources;
  knowledge::VectorStore store{8};
  RetrievalStrategy retrieval_strategy{RetrievalStrategy::kBasic};
  RetrievalStrategyOptions strategy_options;
};

struct ObjOption : Obj
{
  bool has_value{false};
  Value value{Value::Nil()};
};

struct ObjFuture : Obj
{
  std::shared_ptr<async::Future<Value>> future;
};

// v0.7.0: Range type — represents a lazy integer sequence
struct ObjRange : Obj
{
  int64_t start{0};
  int64_t end{0};
  int64_t step{1};
};

// v0.7.0: Iterator state — internal loop iterator
struct ObjIterState : Obj
{
  Value source{Value::Nil()};
  int64_t position{0};
  int64_t end{0};
  int64_t step{1};
  std::vector<Value> snapshot;
};

// v0.7.0: Set type — unordered collection of unique hashable values
struct ObjSet : Obj
{
  std::unordered_set<Value, ValueHash, ValueEqual> items;
};

// v0.7.0: Tuple type — immutable fixed-size collection
struct ObjTuple : Obj
{
  std::vector<Value> items;
  uint32_t hash_cache{0};
  bool hash_computed{false};
};

ObjString* allocate_string(char* chars, std::size_t length, uint32_t hash);
ObjString* copy_string(const char* chars, std::size_t length);
ObjString* take_string(char* chars, std::size_t length);
ObjFunction* new_function();
ObjNative* new_native(ObjString* name, int arity, NativeFn function);
ObjList* new_list(std::vector<Value> items);
ObjMap* new_map(std::unordered_map<std::string, Value> entries);
ObjSkill* new_skill(ObjString* name, ObjString* description, ObjMap* params,
                    std::vector<std::string> param_names, ObjFunction* impl);
ObjContext* new_context();
ObjAgent* new_agent(ObjString* name, ObjString* provider, ObjString* model, ObjString* endpoint,
                    ObjString* api_key_env, ObjString* system, double temperature, ObjList* skills,
                    ObjList* connected_knowledge, ObjContext* context);
ObjEnv* new_env();
ObjKnowledge* new_knowledge(ObjString* name, ObjString* vector_store, ObjString* embedding_model,
                            std::size_t chunk_size, std::size_t chunk_overlap,
                            std::vector<knowledge::Source> sources,
                            RetrievalStrategy retrieval_strategy = RetrievalStrategy::kBasic,
                            RetrievalStrategyOptions strategy_options = {});
ObjOption* new_option(bool has_value, Value value);
ObjFuture* new_future(std::shared_ptr<async::Future<Value>> future);
// v0.7.0: New type factories
ObjRange* new_range(int64_t start, int64_t end, int64_t step);
ObjIterState* new_iter_state();
ObjSet* new_set(std::unordered_set<Value, ValueHash, ValueEqual> items);
ObjTuple* new_tuple(std::vector<Value> items);

// v0.7.1: Struct type system — forward declares (definitions in struct_type.hpp)
struct ObjStructDef;
struct ObjStruct;
struct ObjImplTable;
ObjStructDef* new_struct_def(const std::string& name, const std::vector<std::string>& field_names,
                             bool is_mutable);
ObjStruct* new_struct(ObjStructDef* def, std::vector<Value> field_values);
ObjImplTable* new_impl_table();

// v0.7.1 Phase 2: Trait + sealed type system — forward declares (definitions in sealed_type.hpp)
struct ObjTraitDef;
struct ObjSealedDef;
struct ObjVariant;
ObjTraitDef* new_trait_def(const std::string& name);
ObjSealedDef* new_sealed_def(const std::string& name);
ObjVariant* new_variant(ObjSealedDef* sealed_def, uint16_t tag, std::vector<Value> field_values);

// v0.8: Claw/Forge agent type system — forward declares (definitions in claw_agent_type.hpp / forge_agent_type.hpp)
struct ObjClawAgent;
struct ObjForgeAgent;
struct ObjChannel;
struct ObjWorkspace;
struct ObjLoopContext;
ObjClawAgent* new_claw_agent();
ObjForgeAgent* new_forge_agent();
ObjChannel* new_channel();
ObjLoopContext* new_loop_context();

// v0.9: Data agent type system — forward declares (definitions in data_agent_types.hpp)
struct ObjDataAgent;
struct ObjSource;
struct ObjSink;
struct ObjSchema;
struct ObjQualityGate;
struct ObjComputeEngine;
struct ObjGovernancePolicy;
struct ObjCatalog;
ObjDataAgent* new_data_agent();
ObjSource* new_source();
ObjSink* new_sink();
ObjSchema* new_schema_obj();
ObjQualityGate* new_quality_gate();
ObjComputeEngine* new_compute_engine();
ObjGovernancePolicy* new_governance_policy();
ObjCatalog* new_catalog_obj();

// v0.9.1: ETL agent type system — forward declares (definitions in etl_agent_types.hpp)
struct ObjETLAgent;
struct ObjMart;
struct ObjSemanticLayer;
struct ObjSQLPlan;
struct ObjModelProposal;
ObjETLAgent* new_etl_agent();
ObjMart* new_mart();
ObjSemanticLayer* new_semantic_layer();
ObjSQLPlan* new_sql_plan();
ObjModelProposal* new_model_proposal();

// v0.9.2: Migration Agent type system — forward declares (definitions in migration_types.hpp)
struct ObjMigrationAgent;
struct ObjGovernanceMig;
struct ObjWavePlan;
struct ObjMigrationObject;
struct ObjReconciliationResult;
struct ObjSchemaMap;
struct ObjMigrationCheckpoint;
ObjMigrationAgent* new_migration_agent();
ObjGovernanceMig* new_governance_mig();
ObjWavePlan* new_wave_plan();
ObjMigrationObject* new_migration_object();
ObjReconciliationResult* new_reconciliation_result();
ObjSchemaMap* new_schema_map();
ObjMigrationCheckpoint* new_migration_checkpoint();

// v0.9.3: DataOps Agent type system — forward declares (definitions in dataops_types.hpp)
struct ObjDataOpsAgent;
struct ObjScheduler;
struct ObjAuditTable;
struct ObjLogSource;
struct ObjPlatformMonitor;
struct ObjIncidentPolicy;
struct ObjCorrelation;
struct ObjIncident;
ObjDataOpsAgent* new_dataops_agent();
ObjScheduler* new_scheduler_obj();
ObjAuditTable* new_audit_table_obj();
ObjLogSource* new_log_source_obj();
ObjPlatformMonitor* new_platform_monitor();
ObjIncidentPolicy* new_incident_policy();
ObjCorrelation* new_correlation_obj();
ObjIncident* new_incident_obj();

// v0.9.4: Governance Agent type system — forward declares (definitions in governance_types.hpp)
struct ObjGovernanceAgent;
struct ObjGovCatalogSource;
struct ObjGovCatalog;
struct ObjGlossary;
struct ObjClassificationPolicy;
struct ObjAccessPolicy;
struct ObjQualityPolicy;
struct ObjLineagePolicy;
struct ObjCompliancePolicy;
struct ObjLifecyclePolicy;
struct ObjDataProduct;
struct ObjContractPolicy;
struct ObjMasterData;
struct ObjGovExternalTool;
ObjGovernanceAgent* new_governance_agent();
ObjGovCatalogSource* new_gov_catalog_source();
ObjGovCatalog* new_gov_catalog();
ObjGlossary* new_glossary_obj();
ObjClassificationPolicy* new_classification_policy();
ObjAccessPolicy* new_access_policy();
ObjQualityPolicy* new_quality_policy();
ObjLineagePolicy* new_lineage_policy();
ObjCompliancePolicy* new_compliance_policy();
ObjLifecyclePolicy* new_lifecycle_policy();
ObjDataProduct* new_data_product();
ObjContractPolicy* new_contract_policy();
ObjMasterData* new_master_data_obj();
ObjGovExternalTool* new_gov_external_tool();

// v0.9.5: Modeling Agent type system — forward declares (definitions in modeling_types.hpp)
struct ObjSchemaSource;
struct ObjERModel;
struct ObjEntity;
struct ObjDimensionalModel;
struct ObjDataMartV095;
struct ObjNormAnalysis;
struct ObjAmendmentConfig;
struct ObjAmendment;
struct ObjDataProfile;
struct ObjModelingTool;
struct ObjModelingAgent;
ObjSchemaSource* new_schema_source();
ObjERModel* new_er_model();
ObjEntity* new_entity_obj();
ObjDimensionalModel* new_dimensional_model();
ObjDataMartV095* new_datamart_v095();
ObjNormAnalysis* new_norm_analysis();
ObjAmendmentConfig* new_amendment_config();
ObjAmendment* new_amendment_obj();
ObjDataProfile* new_data_profile();
ObjModelingTool* new_modeling_tool();
ObjModelingAgent* new_modeling_agent();

uint32_t hash_string(const char* key, std::size_t length);
}  // namespace neamc::vm
