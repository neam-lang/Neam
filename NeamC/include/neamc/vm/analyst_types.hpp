//
// Neam v0.9.6 — Analyst Agent runtime types
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
// v0.9.6 Analyst Agent Runtime Objects
// ═══════════════════════════════════════════════════════════════

// --- ObjSQLConnection ---
struct ObjSQLConnection : Obj
{
  ObjString* name{nullptr};
  ObjString* platform{nullptr};
  ObjString* connection{nullptr};
  ObjString* credentials{nullptr};
  ObjString* warehouse{nullptr};
  ObjString* database{nullptr};
  ObjString* schema{nullptr};
  ObjString* project{nullptr};
  ObjString* dataset{nullptr};
  ObjString* catalog{nullptr};
  ObjString* cluster{nullptr};
  int timeout{300};
  int max_rows{10000};
  double cost_limit{0.0};
  ObjString* queue{nullptr};
  bool prefer_materialized_views{true};
  bool use_result_cache{true};
  bool partition_pruning{true};
  ObjString* schema_source{nullptr};
  ObjString* semantic_layer{nullptr};
};

// --- ObjDomainContext ---
struct ObjDomainContext : Obj
{
  ObjString* name{nullptr};
  std::vector<ObjString*> model_refs;
  std::vector<ObjString*> dimensional_model_refs;
  std::vector<ObjString*> mart_refs;
  std::vector<ObjString*> schema_source_refs;
  ObjString* glossary{nullptr};
  std::vector<ObjString*> data_product_refs;
  ObjString* classification{nullptr};
  ObjString* access_policy{nullptr};
  std::vector<ObjString*> semantic_layer_refs;
  bool query_history{false};
  bool feedback_loop{false};
};

// --- ObjQueryTemplate ---
struct ObjQueryTemplate : Obj
{
  ObjString* name{nullptr};
  ObjString* description{nullptr};
  ObjString* category{nullptr};
  ObjString* params_json{nullptr};
  ObjString* sql{nullptr};
  ObjString* default_format{nullptr};
  ObjString* chart_json{nullptr};
  ObjString* classification{nullptr};
  bool audit{true};
};

// --- ObjQueryOptimizer ---
struct ObjQueryOptimizer : Obj
{
  ObjString* name{nullptr};
  ObjString* cost_model{nullptr};
  double max_cost_per_query{0.0};
  double max_scan_gb{0.0};
  int max_execution_time{300};
  ObjString* rules_json{nullptr};
  bool explain_optimizations{true};
  bool show_cost_comparison{true};
};

// --- ObjExecutionPolicy ---
struct ObjExecutionPolicy : Obj
{
  ObjString* name{nullptr};
  int max_rows{10000};
  double max_cost{0.0};
  int timeout{300};
  bool read_only{true};
  bool apply_masking{true};
  bool apply_row_level_security{true};
  bool audit_all_queries{true};
  bool retry_on_timeout{false};
  bool retry_with_smaller_warehouse{false};
  bool cache_results{true};
  ObjString* cache_ttl{nullptr};
  ObjString* cache_key{nullptr};
};

// --- ObjOutputFormat ---
struct ObjOutputFormat : Obj
{
  ObjString* name{nullptr};
  ObjString* format_type{nullptr};
  ObjString* excel_json{nullptr};
  ObjString* pdf_json{nullptr};
  ObjString* html_json{nullptr};
  ObjString* csv_json{nullptr};
  ObjString* json_config_json{nullptr};
  ObjString* slack_json{nullptr};
};

// --- ObjQueryLibrary ---
struct ObjQueryLibrary : Obj
{
  ObjString* name{nullptr};
  ObjString* storage{nullptr};
  ObjString* path{nullptr};
  std::vector<ObjString*> category_refs;
  bool tags{false};
  ObjString* library_visibility{nullptr};
  bool approval_required{false};
  bool track_usage{false};
  bool track_performance{false};
  bool suggest_similar{false};
  bool auto_optimize{false};
};

// --- ObjAnalysisSchedule ---
struct ObjAnalysisSchedule : Obj
{
  ObjString* name{nullptr};
  ObjString* query{nullptr};
  ObjString* cron{nullptr};
  ObjString* connection{nullptr};
  ObjString* format{nullptr};
  ObjString* output_path{nullptr};
  ObjString* delivery_json{nullptr};
  bool audit{true};
  ObjString* budget{nullptr};
};

// --- ObjAnalystAgent ---
struct ObjAnalystAgent : ObjDataAgent
{
  // Inherited: ObjString* provider, model, endpoint, api_key_env (from ObjDataAgent)

  // SQL connections
  std::vector<ObjString*> connection_refs;

  // Domain context
  ObjString* domain_context_ref{nullptr};
  std::vector<ObjString*> model_refs;
  std::vector<ObjString*> dimensional_model_refs;
  std::vector<ObjString*> mart_refs;
  ObjString* glossary_ref{nullptr};
  std::vector<ObjString*> semantic_layer_refs;

  // Governance
  ObjString* governance_ref{nullptr};
  ObjString* classification_ref{nullptr};
  ObjString* access_policy_ref{nullptr};

  // Query behavior
  ObjString* optimizer_ref{nullptr};
  ObjString* execution_policy_ref{nullptr};
  ObjString* query_library_ref{nullptr};

  // Output
  ObjString* default_output{nullptr};
  std::vector<ObjString*> output_format_refs;

  // Skills
  std::vector<ObjString*> skill_refs;
  std::vector<ObjString*> extern_skill_refs;

  // Coordination
  std::vector<ObjString*> coordinates_with_refs;
  std::vector<ObjString*> handoff_refs;
  ObjString* budget_ref{nullptr};

  // Runtime state
  double temperature{0.2};
  std::string status{"initialized"};
  nlohmann::json last_report;
  nlohmann::json query_cache;
  std::mutex state_mutex;
};

// --- Factory Functions ---
ObjSQLConnection* new_sql_connection();
ObjDomainContext* new_domain_context();
ObjQueryTemplate* new_query_template();
ObjQueryOptimizer* new_query_optimizer();
ObjExecutionPolicy* new_execution_policy();
ObjOutputFormat* new_output_format();
ObjQueryLibrary* new_query_library();
ObjAnalysisSchedule* new_analysis_schedule();
ObjAnalystAgent* new_analyst_agent();

}  // namespace neamc::vm
