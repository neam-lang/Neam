//
// Neam v0.9.2 — Phase 4: Data Movement Orchestration implementation
//

#include "data_movement.hpp"
#include "audit_trail.hpp"

#include <algorithm>
#include <chrono>
#include <sstream>
#include <stdexcept>
#include <thread>

namespace neamc::vm::migration {

DataMovementEngine::DataMovementEngine(ObjMigrationAgent* agent)
    : agent_(agent),
      extraction_threads_(agent->movement_config.value("extraction_threads", 4)),
      load_threads_(agent->movement_config.value("load_threads", 4)) {}

bool DataMovementEngine::execute_wave(const MigrationWave& wave) {
  total_tables_ = static_cast<int>(wave.object_names.size());
  tables_completed_.store(0);

  // Check for resume points
  for (const auto& obj_name : wave.object_names) {
    if (resume_from_checkpoint(obj_name)) continue;
    auto it = agent_->objects.find(obj_name);
    if (it != agent_->objects.end()) {
      it->second->state = ObjectMigrationState::EXTRACTING;
      it->second->wave_number = wave.wave_number;
    }
  }

  // Phase 1: Parallel extraction
  {
    std::vector<std::thread> extract_threads;
    int batch_size = extraction_threads_;
    for (size_t i = 0; i < wave.object_names.size(); i += batch_size) {
      for (size_t j = i; j < std::min(i + static_cast<size_t>(batch_size),
           wave.object_names.size()); j++) {
        const auto& obj_name = wave.object_names[j];
        auto it = agent_->objects.find(obj_name);
        if (it == agent_->objects.end() ||
            it->second->state != ObjectMigrationState::EXTRACTING) continue;
        extract_threads.emplace_back([this, obj_name]() { extract_table(obj_name); });
      }
      for (auto& t : extract_threads) {
        if (t.joinable()) t.join();
      }
      extract_threads.clear();
    }
  }

  // Phase 2: Parallel loading
  {
    std::vector<std::thread> load_threads;
    int batch_size = load_threads_;
    for (size_t i = 0; i < wave.object_names.size(); i += batch_size) {
      for (size_t j = i; j < std::min(i + static_cast<size_t>(batch_size),
           wave.object_names.size()); j++) {
        const auto& obj_name = wave.object_names[j];
        auto it = agent_->objects.find(obj_name);
        if (it == agent_->objects.end() ||
            it->second->state == ObjectMigrationState::FAILED) continue;
        load_threads.emplace_back([this, obj_name]() { load_table(obj_name); });
      }
      for (auto& t : load_threads) {
        if (t.joinable()) t.join();
      }
      load_threads.clear();
    }
  }

  // Check wave result
  bool all_success = true;
  for (const auto& obj_name : wave.object_names) {
    auto it = agent_->objects.find(obj_name);
    if (it != agent_->objects.end() &&
        it->second->state == ObjectMigrationState::FAILED) {
      all_success = false;
    }
  }
  return all_success;
}

bool DataMovementEngine::execute_all_waves() {
  if (!agent_->wave_plan) return false;

  for (auto& wave : agent_->wave_plan->waves) {
    audit_log(agent_, "wave_start", agent_name_str(agent_),
              "Starting wave " + std::to_string(wave.wave_number),
              {{"objects", static_cast<int>(wave.object_names.size())}});

    wave.started_at = std::chrono::system_clock::now();
    bool ok = execute_wave(wave);
    wave.completed_at = std::chrono::system_clock::now();
    wave.completed = ok;
    wave.failed = !ok;

    if (!ok) {
      audit_log(agent_, "wave_failed", agent_name_str(agent_),
                "Wave " + std::to_string(wave.wave_number) + " failed", {});
      return false;
    }
  }
  return true;
}

bool DataMovementEngine::extract_table(const std::string& table_name) {
  auto it = agent_->objects.find(table_name);
  if (it == agent_->objects.end()) return false;
  auto* obj = it->second;

  obj->state = ObjectMigrationState::EXTRACTING;
  obj->started_at = std::chrono::system_clock::now();

  audit_log(agent_, "extract_start", table_name,
            "Starting extraction", {{"row_count", obj->row_count}});

  // Generate extraction query (would be submitted to source DB)
  // In production, this connects to the source and runs the query
  obj->rows_extracted = obj->row_count;  // Simulated
  obj->state = ObjectMigrationState::LOADING;

  total_rows_moved_.fetch_add(obj->row_count);
  tables_completed_.fetch_add(1);

  audit_log(agent_, "extract_complete", table_name,
            "Extraction complete",
            {{"rows", obj->rows_extracted}});
  return true;
}

bool DataMovementEngine::load_table(const std::string& table_name) {
  auto it = agent_->objects.find(table_name);
  if (it == agent_->objects.end()) return false;
  auto* obj = it->second;

  obj->state = ObjectMigrationState::LOADING;

  audit_log(agent_, "load_start", table_name, "Starting load", {});

  // Generate and submit load command
  obj->rows_loaded = obj->rows_extracted;  // Simulated
  obj->state = ObjectMigrationState::VALIDATING;

  audit_log(agent_, "load_complete", table_name,
            "Load complete", {{"rows", obj->rows_loaded}});
  return true;
}

bool DataMovementEngine::extract_and_load_table(const std::string& table_name) {
  return extract_table(table_name) && load_table(table_name);
}

bool DataMovementEngine::extract_table_partitioned(
    const std::string& table_name,
    const std::string& partition_column,
    const std::vector<std::string>& partition_values) {
  for (const auto& pv : partition_values) {
    audit_log(agent_, "extract_partition", table_name,
              "Extracting partition " + pv, {});
  }
  return extract_table(table_name);
}

void DataMovementEngine::save_checkpoint(
    const std::string& table_name, const std::string& phase,
    int64_t rows_completed, int64_t bytes_completed,
    const std::string& last_partition) {
  auto* cp = new_migration_checkpoint();
  cp->agent_name = agent_name_str(agent_);
  cp->object_name = table_name;
  cp->phase = phase;
  cp->rows_completed = rows_completed;
  cp->bytes_completed = bytes_completed;
  cp->last_partition = last_partition;
  cp->created_at = std::chrono::system_clock::now();

  std::lock_guard<std::mutex> lock(agent_->state_mutex);
  auto it = agent_->objects.find(table_name);
  if (it != agent_->objects.end()) {
    it->second->last_checkpoint = last_partition;
    it->second->rows_extracted = rows_completed;
  }
}

ObjMigrationCheckpoint* DataMovementEngine::load_checkpoint(
    const std::string& table_name) {
  // In production: read from workspace file
  return nullptr;
}

bool DataMovementEngine::resume_from_checkpoint(const std::string& table_name) {
  auto* cp = load_checkpoint(table_name);
  return cp != nullptr;
}

double DataMovementEngine::progress() const {
  if (total_tables_ == 0) return 0.0;
  return static_cast<double>(tables_completed_.load()) /
         static_cast<double>(total_tables_);
}

int64_t DataMovementEngine::rows_moved() const { return total_rows_moved_.load(); }
int64_t DataMovementEngine::bytes_staged() const { return total_bytes_staged_.load(); }

// ─── SQL Generation ─────────────────────────────────────────────────

std::string DataMovementEngine::generate_extract_query(
    const std::string& table_name, const TableMapping& mapping) {
  std::string platform = agent_->schema_translation_config.value("source_platform", "oracle");
  if (platform == "oracle") return generate_oracle_extract(table_name, mapping);
  if (platform == "teradata") return generate_teradata_extract(table_name, mapping);
  if (platform == "sqlserver") return generate_sqlserver_extract(table_name, mapping);

  // Default: simple SELECT *
  return "SELECT * FROM " + mapping.source_table;
}

std::string DataMovementEngine::generate_oracle_extract(
    const std::string& table_name, const TableMapping& mapping) {
  std::string select_cols;
  for (const auto& col : mapping.columns) {
    if (col.requires_transform) {
      select_cols += col.transform_expression + " AS " + col.target_name + ", ";
    } else {
      select_cols += col.source_name + ", ";
    }
  }
  if (!select_cols.empty()) {
    select_cols = select_cols.substr(0, select_cols.length() - 2);
  } else {
    select_cols = "*";
  }
  std::string query = "SELECT " + select_cols + " FROM " + mapping.source_table;
  if (!mapping.columns.empty()) {
    query += " ORDER BY " + mapping.columns[0].source_name;
  }
  return query;
}

std::string DataMovementEngine::generate_teradata_extract(
    const std::string& table_name, const TableMapping& mapping) {
  return "SELECT * FROM " + mapping.source_table;
}

std::string DataMovementEngine::generate_sqlserver_extract(
    const std::string& table_name, const TableMapping& mapping) {
  return "SELECT * FROM " + mapping.source_table + " WITH (NOLOCK)";
}

std::string DataMovementEngine::generate_load_command(
    const std::string& table_name, const std::string& staging_path) {
  std::string target = agent_->schema_translation_config.value("target_platform", "snowflake");
  if (target == "snowflake") return generate_snowflake_copy_into(table_name, staging_path);
  if (target == "bigquery") return generate_bigquery_load(table_name, staging_path);
  if (target == "databricks") return generate_databricks_copy_into(table_name, staging_path);
  return "-- Unknown target platform";
}

std::string DataMovementEngine::generate_snowflake_copy_into(
    const std::string& table, const std::string& path) {
  std::string sql = "COPY INTO " + table + "\n";
  sql += "FROM '" + path + "'\n";
  sql += "FILE_FORMAT = (TYPE = PARQUET";
  std::string compression = agent_->movement_config.value("staging_format", "parquet") == "parquet"
                            ? "ZSTD" : "GZIP";
  sql += ", COMPRESSION = " + compression + ")\n";
  sql += "MATCH_BY_COLUMN_NAME = CASE_INSENSITIVE\n";
  sql += "ON_ERROR = ABORT_STATEMENT\n";
  sql += "PURGE = FALSE;";
  return sql;
}

std::string DataMovementEngine::generate_bigquery_load(
    const std::string& table, const std::string& path) {
  return "LOAD DATA INTO `" + table + "`\nFROM FILES (\n"
         "  format = 'PARQUET',\n  uris = ['" + path + "/*']\n);";
}

std::string DataMovementEngine::generate_databricks_copy_into(
    const std::string& table, const std::string& path) {
  return "COPY INTO " + table + "\nFROM '" + path + "'\n"
         "FILEFORMAT = PARQUET\nFORMAT_OPTIONS ('mergeSchema' = 'true');";
}

}  // namespace neamc::vm::migration
