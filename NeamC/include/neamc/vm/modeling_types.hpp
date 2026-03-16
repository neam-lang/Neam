//
// Neam v0.9.5 — Modeling Agent runtime types
//

#pragma once

#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "neamc/vm/data_agent_types.hpp"

namespace neamc::vm
{

// ═══════════════════════════════════════════════════════════════
// v0.9.5 Modeling Agent Runtime Objects
// ═══════════════════════════════════════════════════════════════

// --- ObjSchemaSource ---
struct ObjSchemaSource : Obj
{
  ObjString* name{nullptr};
  ObjString* source_type{nullptr};
  ObjString* connection{nullptr};
  ObjString* credentials{nullptr};
  ObjString* scan_interval{nullptr};
  ObjString* path{nullptr};
  ObjString* format{nullptr};
  ObjString* model_type{nullptr};
  ObjString* sync_direction{nullptr};
  ObjString* api_connection{nullptr};
  ObjString* dialect{nullptr};
  ObjString* project_path{nullptr};
  ObjString* manifest_path{nullptr};
  std::vector<ObjString*> databases;
  std::vector<ObjString*> prefixes;
  std::vector<ObjString*> submodel_filter;
  bool include_views{true};
  bool include_procedures{false};
  bool read_constraints{true};
  bool read_indexes{true};
  bool read_statistics{false};
  bool watch{false};
  bool detect_formats{false};
  bool apply_migrations{false};
  bool read_sources{true};
  bool read_models{true};
  bool read_tests{false};
  int sample_size{1000};
  ObjString* infer_relationships_json{nullptr};
  ObjString* schema_evolution_json{nullptr};
};

// --- ObjERModel ---
struct ObjERModel : Obj
{
  ObjString* name{nullptr};
  ObjString* version{nullptr};
  ObjString* source{nullptr};
  std::vector<ObjString*> levels;
  ObjString* notation_json{nullptr};
  ObjString* domains_json{nullptr};
  ObjString* relationship_inference_json{nullptr};
  ObjString* sync_json{nullptr};
};

// --- ObjEntity ---
struct ObjEntity : Obj
{
  ObjString* name{nullptr};
  ObjString* domain{nullptr};
  ObjString* attributes_json{nullptr};
  ObjString* relationships_json{nullptr};
  ObjString* glossary_term{nullptr};
  ObjString* owner{nullptr};
  ObjString* description{nullptr};
};

// --- ObjDimensionalModel ---
struct ObjDimensionalModel : Obj
{
  ObjString* name{nullptr};
  ObjString* methodology{nullptr};
  ObjString* source{nullptr};
  ObjString* facts_json{nullptr};
  ObjString* dimensions_json{nullptr};
  std::vector<ObjString*> conformed;
  ObjString* target_platform{nullptr};
  ObjString* target_schema{nullptr};
  ObjString* output_json{nullptr};
};

// --- ObjDataMartV095 ---
struct ObjDataMartV095 : Obj
{
  ObjString* name{nullptr};
  ObjString* dimensional_model{nullptr};
  ObjString* purpose{nullptr};
  ObjString* owner{nullptr};
  std::vector<ObjString*> facts;
  std::vector<ObjString*> dimensions;
  ObjString* additional_dimensions_json{nullptr};
  ObjString* aggregate_tables_json{nullptr};
  ObjString* materialization_json{nullptr};
  ObjString* row_level_security_json{nullptr};
  ObjString* column_masking_json{nullptr};
  ObjString* quality_json{nullptr};
};

// --- ObjNormAnalysis ---
struct ObjNormAnalysis : Obj
{
  ObjString* name{nullptr};
  ObjString* scope_json{nullptr};
  ObjString* target_nf{nullptr};
  ObjString* fd_discovery_json{nullptr};
  ObjString* report_json{nullptr};
  ObjString* governance{nullptr};
  ObjString* on_violation{nullptr};
};

// --- ObjAmendmentConfig ---
struct ObjAmendmentConfig : Obj
{
  ObjString* name{nullptr};
  ObjString* monitor_json{nullptr};
  ObjString* change_types_json{nullptr};
  ObjString* impact_scope_json{nullptr};
  ObjString* approval_json{nullptr};
  ObjString* document_json{nullptr};
};

// --- ObjAmendment ---
struct ObjAmendment : Obj
{
  ObjString* name{nullptr};
  ObjString* model{nullptr};
  ObjString* amendment_type{nullptr};
  ObjString* description{nullptr};
  ObjString* changes_json{nullptr};
  bool auto_analyze{false};
  bool require_approval{true};
};

// --- ObjDataProfile ---
struct ObjDataProfile : Obj
{
  ObjString* name{nullptr};
  ObjString* sources_json{nullptr};
  ObjString* profiling_json{nullptr};
  ObjString* output_json{nullptr};
};

// --- ObjModelingTool ---
struct ObjModelingTool : Obj
{
  ObjString* name{nullptr};
  ObjString* tool_type{nullptr};
  ObjString* path{nullptr};
  ObjString* api_url{nullptr};
  ObjString* credentials{nullptr};
  ObjString* repository{nullptr};
  std::vector<ObjString*> submodels;
  ObjString* sync_json{nullptr};
  ObjString* mapping_json{nullptr};
  ObjString* on_conflict_json{nullptr};
};

// --- ObjModelingAgent ---
struct ObjModelingAgent : ObjDataAgent
{
  // Inherited: ObjString* provider, model, endpoint, api_key_env (from ObjDataAgent)

  // Schema source references
  std::vector<ObjString*> schema_source_refs;

  // Governance integration
  ObjString* catalog_ref{nullptr};
  ObjString* governance_ref{nullptr};

  // Modeling tool references
  std::vector<ObjString*> modeling_tool_refs;

  // Capabilities
  bool reverse_engineer_enabled{false};
  bool normalization_analysis_enabled{false};
  bool dimensional_design_enabled{false};
  bool amendment_proposals_enabled{false};
  bool data_profiling_enabled{false};

  // Target methodologies
  std::vector<std::string> target_methodologies;

  // Coordination
  std::vector<ObjString*> coordinates_with_refs;
  ObjString* budget_ref{nullptr};
  bool enrich_from_governance{false};

  // Runtime state
  std::string status{"initialized"};
  nlohmann::json last_report;
  std::mutex state_mutex;
};

// --- Factory Functions ---
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

}  // namespace neamc::vm
