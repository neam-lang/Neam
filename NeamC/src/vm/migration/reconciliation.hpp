//
// Neam v0.9.2 — Phase 5: Reconciliation Engine
//

#pragma once

#include <string>
#include <thread>
#include <vector>

#include "neamc/vm/migration_types.hpp"

namespace neamc::vm::migration {

enum class ReconciliationLevel {
  INFRASTRUCTURE = 1,
  SCHEMA = 2,
  ROW_COUNTS = 3,
  COLUMN_AGGREGATES = 4,
  STATISTICAL_DISTRIBUTION = 5,
  HASH_COMPARISON = 6,
  BUSINESS_RULES = 7,
  PERFORMANCE = 8
};

class ReconciliationEngine {
public:
  explicit ReconciliationEngine(ObjMigrationAgent* agent);

  ObjReconciliationResult* run_full(const std::string& table_name);
  ObjReconciliationResult* run_all_tables();

  ReconciliationCheck check_infrastructure(const std::string& table_name);
  ReconciliationCheck check_schema(const std::string& table_name);
  ReconciliationCheck check_row_counts(const std::string& table_name);
  std::vector<ReconciliationCheck> check_column_aggregates(const std::string& table_name);
  std::vector<ReconciliationCheck> check_statistical_distribution(const std::string& table_name);
  ReconciliationCheck check_hash_comparison(const std::string& table_name);
  std::vector<ReconciliationCheck> check_golden_queries();
  ReconciliationCheck check_performance(const std::string& table_name);

  void start_continuous(const std::string& interval);
  void stop_continuous();
  bool is_continuous_running() const;

  void set_sampling_strategy(const std::string& strategy);

private:
  ObjMigrationAgent* agent_;
  bool continuous_running_ = false;
  std::string sampling_strategy_ = "full";

  std::string generate_row_count_sql(const std::string& table, bool is_source);
  std::string generate_aggregate_sql(const std::string& table,
                                     const std::string& column,
                                     const std::string& agg_func,
                                     bool is_source);
  std::string generate_hash_sql(const std::string& table,
                                const std::vector<std::string>& columns,
                                bool is_source);

  bool within_tolerance(const std::string& check_type,
                        const std::string& column_name,
                        double source_val,
                        double target_val);
  bool is_financial_column(const std::string& column_name);
  bool is_numeric_type(const std::string& type_name);
  bool is_aggregatable_type(const std::string& type_name);
  std::string to_lower(const std::string& s);
};

}  // namespace neamc::vm::migration
