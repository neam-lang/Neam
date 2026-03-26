//
// Neam v0.9.2 — Phase 4: Data Movement Orchestration
//

#pragma once

#include <atomic>
#include <condition_variable>
#include <mutex>
#include <queue>
#include <string>
#include <thread>
#include <vector>

#include "neamc/vm/migration_types.hpp"

namespace neamc::vm::migration {

struct ExtractionTask {
  std::string table_name;
  std::string partition_key;
  std::string partition_value;
  int64_t estimated_rows;
  std::string extract_query;
  std::string staging_path;
  std::string format;
  std::string compression;
};

struct LoadTask {
  std::string table_name;
  std::string staging_path;
  std::string load_command;
  int64_t expected_rows;
};

class DataMovementEngine {
public:
  explicit DataMovementEngine(ObjMigrationAgent* agent);

  bool execute_wave(const MigrationWave& wave);
  bool execute_all_waves();

  bool extract_table(const std::string& table_name);
  bool load_table(const std::string& table_name);
  bool extract_and_load_table(const std::string& table_name);

  bool extract_table_partitioned(const std::string& table_name,
                                 const std::string& partition_column,
                                 const std::vector<std::string>& partition_values);

  void save_checkpoint(const std::string& table_name,
                       const std::string& phase,
                       int64_t rows_completed,
                       int64_t bytes_completed,
                       const std::string& last_partition);
  ObjMigrationCheckpoint* load_checkpoint(const std::string& table_name);
  bool resume_from_checkpoint(const std::string& table_name);

  double progress() const;
  int64_t rows_moved() const;
  int64_t bytes_staged() const;

private:
  ObjMigrationAgent* agent_;
  int extraction_threads_;
  int load_threads_;
  std::atomic<int64_t> total_rows_moved_{0};
  std::atomic<int64_t> total_bytes_staged_{0};
  std::atomic<int> tables_completed_{0};
  int total_tables_ = 0;

  std::string generate_extract_query(const std::string& table_name,
                                     const TableMapping& mapping);
  std::string generate_oracle_extract(const std::string& table_name,
                                      const TableMapping& mapping);
  std::string generate_teradata_extract(const std::string& table_name,
                                        const TableMapping& mapping);
  std::string generate_sqlserver_extract(const std::string& table_name,
                                         const TableMapping& mapping);

  std::string generate_load_command(const std::string& table_name,
                                    const std::string& staging_path);
  std::string generate_snowflake_copy_into(const std::string& table,
                                           const std::string& path);
  std::string generate_bigquery_load(const std::string& table,
                                     const std::string& path);
  std::string generate_databricks_copy_into(const std::string& table,
                                            const std::string& path);
};

}  // namespace neamc::vm::migration
