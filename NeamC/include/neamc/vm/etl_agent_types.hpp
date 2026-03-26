//
// Neam v0.9.1 — ETL Agent runtime types
//

#pragma once

#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "neamc/vm/data_agent_types.hpp"

namespace neamc::vm
{

// ============================================================
// ObjMart — Dimensional model definition
// ============================================================

struct MartSCDEntry
{
  std::string dimension_name;
  std::string scd_type;
};

struct MartAggregateTable
{
  std::string name;
  std::string grain;
  std::vector<std::string> group_by;
  std::vector<std::string> measures;
};

struct ObjMart : Obj
{
  std::string name;
  std::vector<std::string> facts;
  std::vector<std::string> dimensions;
  std::string grain;
  std::vector<std::string> measures;
  std::vector<MartSCDEntry> scd;
  std::vector<std::string> conformed;
  std::vector<MartAggregateTable> aggregate_tables;
  std::string materialization;
};

// ============================================================
// ObjSemanticLayer — Business glossary + metrics
// ============================================================

struct SemanticMetric
{
  std::string name;
  std::string sql;
  std::string description;
  std::string type;
  std::optional<std::string> owner;
};

struct SemanticEntity
{
  std::string name;
  std::string table;
  std::string key;
  std::optional<std::string> description;
};

struct SemanticRelationship
{
  std::string from_entity;
  std::string to_entity;
  std::string type;
  std::string join_condition;
};

struct ObjSemanticLayer : Obj
{
  std::string name;
  std::vector<SemanticMetric> metrics;
  std::vector<SemanticEntity> entities;
  std::vector<SemanticRelationship> relationships;
  std::unordered_map<std::string, std::string> synonyms;

  // Time intelligence
  std::string fiscal_year_start;
  std::string week_start;
  std::string default_timezone;
};

// ============================================================
// ObjSQLPlan — Logical SQL plan (pre-transpilation)
// ============================================================

struct ObjSQLPlan : Obj
{
  std::string logical_sql;
  std::string target_dialect;
  std::string physical_sql;
  bool validated{false};
  double estimated_cost{0.0};
  std::string explanation;
  std::string generated_by;
  std::string source_table;
  std::string target_table;
};

// ============================================================
// ObjModelProposal — AI-generated dimensional model
// ============================================================

struct ProposedFact
{
  std::string name;
  std::string grain;
  std::vector<std::string> measures;
  std::vector<std::string> dimension_keys;
  std::string source_table;
};

struct ProposedDimension
{
  std::string name;
  std::string scd_type;
  std::vector<std::string> attributes;
  std::string source_table;
  std::string surrogate_key;
};

struct ObjModelProposal : Obj
{
  std::string methodology;
  std::vector<ProposedFact> facts;
  std::vector<ProposedDimension> dimensions;
  std::vector<std::string> conformed_dimensions;
  std::string ddl_script;
  std::string explanation;

  enum class Status { Pending, Approved, Rejected, Modified };
  Status status{Status::Pending};
};

// ============================================================
// Layer execution state
// ============================================================

struct LayerExecutionState
{
  std::string layer_name;
  int tables_total{0};
  int tables_completed{0};
  std::string current_table;
  double elapsed_ms{0.0};
  double cost{0.0};
  std::vector<std::string> errors;
};

// ============================================================
// ObjETLAgent — ETL lifecycle agent (extends ObjDataAgent)
// ============================================================

struct ObjETLAgent : ObjDataAgent
{
  // ETL-specific fields
  ObjComputeEngine* warehouse{nullptr};
  std::string model_type;
  std::vector<ObjMart*> marts;
  ObjSemanticLayer* semantic{nullptr};

  // Layer config
  struct {
    std::string staging_prefix;
    std::string staging_materialization;
    std::vector<std::string> staging_operations;
    std::string integration_prefix;
    std::string integration_materialization;
    std::vector<std::string> integration_operations;
  } layer_config;

  // Incremental
  struct {
    std::string strategy;
    std::string key;
    std::string lookback;
    std::string on_schema_change;
  } incremental;

  // Self-heal
  bool self_heal_enabled{false};
  std::string on_failure;
  int max_retry{3};
  std::string retry_backoff;

  // Auto-model
  bool auto_model_enabled{false};
  std::string auto_model_methodology;
  std::string auto_model_approval;

  // Runtime state
  std::vector<LayerExecutionState> layer_states;
  ObjModelProposal* current_proposal{nullptr};
};

}  // namespace neamc::vm
