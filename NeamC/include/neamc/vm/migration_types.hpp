//
// Neam v0.9.2 — Migration Agent runtime types
//

#pragma once

#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "neamc/vm/data_agent_types.hpp"
#include "neamc/vm/object.hpp"

namespace neamc::vm
{

// Migration lifecycle state
enum class MigrationPhase
{
  ASSESS,
  PLAN,
  EXECUTE,
  VALIDATE,
  CUTOVER,
  ROLLBACK,
  DECOMMISSION,
  COMPLETED,
  FAILED
};

// Per-object migration state
enum class ObjectMigrationState
{
  PENDING,
  PROFILING,
  TRANSLATING,
  EXTRACTING,
  LOADING,
  VALIDATING,
  VALIDATED,
  CUTOVER,
  FAILED,
  REMEDIATING,
  ESCALATED,
  SKIPPED
};

// Per-object migration tracking
struct ObjMigrationObject : public Obj
{
  ObjMigrationObject() { type = ObjType::OBJ_MIGRATION_OBJECT; }

  std::string object_name;
  std::string object_type;
  std::string source_schema;
  std::string target_schema;
  ObjectMigrationState state = ObjectMigrationState::PENDING;

  int64_t row_count = 0;
  int64_t size_bytes = 0;
  int column_count = 0;
  std::vector<std::string> dependencies;
  double risk_score = 0.0;

  std::string source_ddl;
  std::string target_ddl;
  std::vector<std::string> type_warnings;

  int64_t rows_extracted = 0;
  int64_t rows_loaded = 0;
  int64_t bytes_staged = 0;
  std::string last_checkpoint;
  int retry_count = 0;

  bool row_count_match = false;
  bool aggregate_match = false;
  bool hash_match = false;
  std::string last_error;
  std::vector<std::string> remediation_log;

  std::chrono::system_clock::time_point started_at;
  std::chrono::system_clock::time_point completed_at;
  int wave_number = -1;
};

// Wave within a migration plan
struct MigrationWave
{
  int wave_number{0};
  std::string wave_name;
  std::vector<std::string> object_names;
  std::vector<std::string> dependencies;
  bool completed = false;
  bool failed = false;
  std::chrono::system_clock::time_point started_at;
  std::chrono::system_clock::time_point completed_at;
};

// Computed wave plan
struct ObjWavePlan : public Obj
{
  ObjWavePlan() { type = ObjType::OBJ_WAVE_PLAN; }

  std::vector<MigrationWave> waves;
  int current_wave = 0;
  std::string generation_mode;
  int total_objects = 0;
  int max_tables_per_wave = 50;
  int max_parallel_extractions = 4;
};

// Column mapping for schema translation
struct ColumnMapping
{
  std::string source_name;
  std::string target_name;
  std::string source_type;
  std::string target_type;
  std::string conversion_note;
  bool requires_transform = false;
  std::string transform_expression;
};

// Table mapping for schema translation
struct TableMapping
{
  std::string source_table;
  std::string target_table;
  std::vector<ColumnMapping> columns;
  std::string source_ddl;
  std::string target_ddl;
  std::vector<std::string> index_mappings;
  std::vector<std::string> constraint_mappings;
  std::string partition_strategy;
};

// Schema map object
struct ObjSchemaMap : public Obj
{
  ObjSchemaMap() { type = ObjType::OBJ_SCHEMA_MAP; }

  std::string source_platform;
  std::string target_platform;
  std::unordered_map<std::string, TableMapping> table_mappings;
  std::unordered_map<std::string, std::string> view_translations;
  std::unordered_map<std::string, std::string> sp_translations;
  std::unordered_map<std::string, int64_t> sequence_values;
  int total_warnings = 0;
  int total_errors = 0;
};

// Reconciliation check result
struct ReconciliationCheck
{
  std::string check_type;
  std::string object_name;
  bool passed = false;
  std::string source_value;
  std::string target_value;
  double deviation = 0.0;
  std::string detail;
};

// Reconciliation result object
struct ObjReconciliationResult : public Obj
{
  ObjReconciliationResult() { type = ObjType::OBJ_RECONCILIATION_RESULT; }

  std::vector<ReconciliationCheck> checks;
  int total_checks = 0;
  int passed_checks = 0;
  int failed_checks = 0;
  int warning_checks = 0;
  bool overall_passed = false;
  std::chrono::system_clock::time_point run_at;
  std::string run_mode;
};

// Migration checkpoint for resume
struct ObjMigrationCheckpoint : public Obj
{
  ObjMigrationCheckpoint() { type = ObjType::OBJ_MIGRATION_CHECKPOINT; }

  std::string agent_name;
  std::string object_name;
  std::string phase;
  int64_t rows_completed = 0;
  int64_t bytes_completed = 0;
  std::string last_partition;
  std::string last_offset;
  nlohmann::json metadata;
  std::chrono::system_clock::time_point created_at;
};

// Governance object (migration-specific)
struct ObjGovernanceMig : public Obj
{
  ObjGovernanceMig() { type = ObjType::OBJ_GOVERNANCE; }

  std::string name;
  bool preserve_classification = false;
  bool pii_detection = false;
  std::unordered_map<std::string, std::string> residency;
  bool log_all_sql = false;
  bool log_all_data_movement = false;
  std::string audit_retention;
};

// Top-level migration agent runtime object
struct ObjMigrationAgent : public ObjDataAgent
{
  ObjMigrationAgent() { type = ObjType::OBJ_MIGRATION_AGENT; }

  // Configuration
  std::string source_name;
  std::string target_name;
  std::string staging_name;
  std::string strategy;

  // Sub-objects
  ObjWavePlan* wave_plan = nullptr;
  ObjSchemaMap* schema_map_obj = nullptr;
  ObjGovernanceMig* governance_obj = nullptr;

  // Per-object tracking
  std::unordered_map<std::string, ObjMigrationObject*> objects;

  // Lifecycle state
  MigrationPhase phase = MigrationPhase::ASSESS;
  bool rollback_available = true;
  std::chrono::system_clock::time_point migration_started;
  std::chrono::system_clock::time_point migration_completed;

  // Configuration JSON blobs
  nlohmann::json movement_config;
  nlohmann::json schema_translation_config;
  nlohmann::json validation_config;
  nlohmann::json cutover_config;
  nlohmann::json self_heal_config;
  nlohmann::json assessment_config;

  // Statistics
  int64_t total_rows_migrated = 0;
  int64_t total_bytes_staged = 0;
  int total_objects_completed = 0;
  int total_objects_failed = 0;
  int total_remediations = 0;
  double total_cost = 0.0;

  // Thread safety for concurrent wave execution
  mutable std::mutex state_mutex;
};

}  // namespace neamc::vm
