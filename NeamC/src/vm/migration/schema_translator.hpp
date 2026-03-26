//
// Neam v0.9.2 — Phase 3: Schema Translation Engine
//

#pragma once

#include <functional>
#include <optional>
#include <queue>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "neamc/vm/migration_types.hpp"

namespace neamc::vm::migration {

// A single source->target type mapping with optional precision analysis
struct TypeMappingRule {
  std::string source_type;
  std::string target_type;
  bool requires_precision;
  std::string warning;
  std::function<std::string(const std::string& source_col_def)> transform;
};

// Holds all source->target type mappings for a specific platform pair
class TypeMapper {
public:
  TypeMapper(const std::string& source_platform, const std::string& target_platform);

  ColumnMapping map_column(const std::string& col_name,
                           const std::string& source_type,
                           int precision = -1,
                           int scale = -1,
                           bool nullable = true) const;

  std::vector<ColumnMapping> map_table_columns(
      const std::string& table_name,
      const std::vector<std::tuple<std::string, std::string, int, int, bool>>& columns) const;

  void add_rule(const TypeMappingRule& rule);
  void add_platform_rules();

private:
  std::string source_platform_;
  std::string target_platform_;
  std::vector<TypeMappingRule> rules_;

  void load_oracle_to_snowflake();
  void load_oracle_to_bigquery();
  void load_teradata_to_snowflake();
  void load_teradata_to_bigquery();
  void load_sqlserver_to_snowflake();
  void load_postgres_to_snowflake();
  void load_redshift_to_snowflake();
  void load_redshift_to_bigquery();
};

// Feature support matrix per platform
struct FeatureSupport {
  bool partitioning;
  bool materialized_views;
  bool row_level_security;
  bool column_masking;
  bool sequences;
  bool spatial;
  bool xml;
  bool json_native;
};

// Schema Translation Engine — orchestrates full DDL translation
class SchemaTranslator {
public:
  explicit SchemaTranslator(ObjMigrationAgent* agent);

  ObjSchemaMap* translate_all();

  TableMapping translate_table(const std::string& table_name,
                               const std::string& source_ddl);
  std::string translate_view(const std::string& view_name,
                             const std::string& source_sql);
  std::string translate_index(const std::string& index_ddl);
  std::string translate_sequence(const std::string& seq_name, int64_t current_val);

  std::vector<std::string> topological_sort_views(
      const std::unordered_map<std::string, std::vector<std::string>>& view_deps);

  std::string translate_partition_clause(const std::string& source_partition,
                                        const std::string& target_platform);
  std::string translate_materialized_view(const std::string& mv_name,
                                          const std::string& source_sql);

private:
  ObjMigrationAgent* agent_;
  TypeMapper type_mapper_;

  std::string handle_oracle_empty_string(const std::string& source_ddl);
  std::string handle_oracle_date_semantics(const std::string& source_ddl);
  std::string handle_oracle_number_no_precision(
      const std::string& col_name, int64_t max_val, int64_t min_val, int max_scale);

  FeatureSupport get_platform_features(const std::string& platform);
  std::vector<std::string> extract_partition_columns(const std::string& partition_clause);
  std::vector<std::string> extract_index_columns(const std::string& index_ddl);
  std::string join(const std::vector<std::string>& parts, const std::string& sep);
};

}  // namespace neamc::vm::migration
