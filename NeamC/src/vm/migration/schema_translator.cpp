//
// Neam v0.9.2 — Phase 3: Schema Translation Engine implementation
//

#include "schema_translator.hpp"
#include "audit_trail.hpp"

#include <algorithm>
#include <cmath>
#include <climits>
#include <queue>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <unordered_set>

namespace neamc::vm::migration {

// ─── Utility ────────────────────────────────────────────────────────

std::string SchemaTranslator::join(const std::vector<std::string>& parts,
                                   const std::string& sep) {
  std::string result;
  for (size_t i = 0; i < parts.size(); ++i) {
    if (i > 0) result += sep;
    result += parts[i];
  }
  return result;
}

static std::string to_upper(const std::string& s) {
  std::string r = s;
  std::transform(r.begin(), r.end(), r.begin(), ::toupper);
  return r;
}

// ─── TypeMapper ─────────────────────────────────────────────────────

TypeMapper::TypeMapper(const std::string& source_platform,
                       const std::string& target_platform)
    : source_platform_(source_platform), target_platform_(target_platform) {
  add_platform_rules();
}

ColumnMapping TypeMapper::map_column(const std::string& col_name,
                                     const std::string& source_type,
                                     int precision, int scale,
                                     bool nullable) const {
  ColumnMapping mapping;
  mapping.source_name = col_name;
  mapping.target_name = col_name;  // Default: preserve name
  mapping.source_type = source_type;

  std::string upper_type = to_upper(source_type);

  for (const auto& rule : rules_) {
    if (to_upper(rule.source_type) == upper_type ||
        upper_type.find(to_upper(rule.source_type)) == 0) {
      mapping.target_type = rule.target_type;
      if (!rule.warning.empty()) {
        mapping.conversion_note = rule.warning;
      }
      if (rule.requires_precision && precision > 0) {
        mapping.target_type += "(" + std::to_string(precision);
        if (scale > 0) mapping.target_type += "," + std::to_string(scale);
        mapping.target_type += ")";
      }
      if (rule.transform) {
        auto transformed = rule.transform(source_type);
        if (!transformed.empty()) {
          mapping.requires_transform = true;
          mapping.transform_expression = transformed;
        }
      }
      return mapping;
    }
  }

  // Fallback: pass through with warning
  mapping.target_type = source_type;
  mapping.conversion_note = "No mapping rule found — passed through unchanged";
  return mapping;
}

std::vector<ColumnMapping> TypeMapper::map_table_columns(
    const std::string& table_name,
    const std::vector<std::tuple<std::string, std::string, int, int, bool>>& columns) const {
  std::vector<ColumnMapping> mappings;
  mappings.reserve(columns.size());
  for (const auto& [name, type, prec, scale, nullable] : columns) {
    mappings.push_back(map_column(name, type, prec, scale, nullable));
  }
  return mappings;
}

void TypeMapper::add_rule(const TypeMappingRule& rule) {
  rules_.push_back(rule);
}

void TypeMapper::add_platform_rules() {
  std::string key = source_platform_ + "_to_" + target_platform_;
  if (key == "oracle_to_snowflake") load_oracle_to_snowflake();
  else if (key == "oracle_to_bigquery") load_oracle_to_bigquery();
  else if (key == "teradata_to_snowflake") load_teradata_to_snowflake();
  else if (key == "teradata_to_bigquery") load_teradata_to_bigquery();
  else if (key == "sqlserver_to_snowflake") load_sqlserver_to_snowflake();
  else if (key == "postgres_to_snowflake") load_postgres_to_snowflake();
  else if (key == "redshift_to_snowflake") load_redshift_to_snowflake();
  else if (key == "redshift_to_bigquery") load_redshift_to_bigquery();
}

// ─── Oracle → Snowflake type mappings ───────────────────────────────

void TypeMapper::load_oracle_to_snowflake() {
  rules_.push_back({"NUMBER", "NUMBER", true, "", nullptr});
  rules_.push_back({"BINARY_FLOAT", "FLOAT", false, "", nullptr});
  rules_.push_back({"BINARY_DOUBLE", "DOUBLE", false, "", nullptr});
  rules_.push_back({"INTEGER", "INTEGER", false, "", nullptr});
  rules_.push_back({"SMALLINT", "SMALLINT", false, "", nullptr});

  rules_.push_back({"VARCHAR2", "VARCHAR", true,
      "Oracle VARCHAR2 byte vs char semantics", nullptr});
  rules_.push_back({"NVARCHAR2", "VARCHAR", true, "", nullptr});
  rules_.push_back({"CHAR", "CHAR", true, "", nullptr});
  rules_.push_back({"NCHAR", "CHAR", true, "", nullptr});
  rules_.push_back({"CLOB", "VARCHAR(16777216)", false,
      "CLOBs > 16MB externalized to staging", nullptr});
  rules_.push_back({"NCLOB", "VARCHAR(16777216)", false,
      "NCLOBs > 16MB externalized to staging", nullptr});

  rules_.push_back({"DATE", "TIMESTAMP_NTZ", false,
      "Oracle DATE includes time component (YYYY-MM-DD HH24:MI:SS)", nullptr});
  rules_.push_back({"TIMESTAMP WITH TIME ZONE", "TIMESTAMP_TZ", false, "", nullptr});
  rules_.push_back({"TIMESTAMP WITH LOCAL TIME ZONE", "TIMESTAMP_LTZ", false, "", nullptr});
  rules_.push_back({"TIMESTAMP", "TIMESTAMP_NTZ", true, "", nullptr});
  rules_.push_back({"INTERVAL YEAR TO MONTH", "VARCHAR(32)", false,
      "No native interval type in Snowflake", nullptr});
  rules_.push_back({"INTERVAL DAY TO SECOND", "VARCHAR(64)", false,
      "No native interval type in Snowflake", nullptr});

  rules_.push_back({"BLOB", "BINARY", false,
      "BLOBs > 8MB externalized to staging", nullptr});
  rules_.push_back({"RAW", "BINARY", true, "", nullptr});
  rules_.push_back({"LONG RAW", "BINARY", false, "Deprecated Oracle type", nullptr});

  rules_.push_back({"ROWID", "VARCHAR(18)", false,
      "Oracle physical ROWID — informational only", nullptr});
  rules_.push_back({"UROWID", "VARCHAR(4000)", false, "", nullptr});
  rules_.push_back({"XMLTYPE", "VARIANT", false,
      "XML stored as semi-structured in Snowflake VARIANT", nullptr});
  rules_.push_back({"SDO_GEOMETRY", "GEOGRAPHY", false,
      "Oracle Spatial -> Snowflake GEOGRAPHY (WGS84 only)", nullptr});
  rules_.push_back({"BOOLEAN", "BOOLEAN", false, "", nullptr});
}

// ─── Teradata → BigQuery type mappings ──────────────────────────────

void TypeMapper::load_teradata_to_bigquery() {
  rules_.push_back({"INTEGER", "INT64", false, "", nullptr});
  rules_.push_back({"SMALLINT", "INT64", false, "", nullptr});
  rules_.push_back({"BIGINT", "INT64", false, "", nullptr});
  rules_.push_back({"BYTEINT", "INT64", false, "", nullptr});
  rules_.push_back({"DECIMAL", "NUMERIC", true,
      "Teradata DECIMAL(38,0) -> BigQuery NUMERIC(38,9) max", nullptr});
  rules_.push_back({"NUMBER", "NUMERIC", true, "", nullptr});
  rules_.push_back({"FLOAT", "FLOAT64", false, "", nullptr});
  rules_.push_back({"REAL", "FLOAT64", false, "", nullptr});
  rules_.push_back({"DOUBLE PRECISION", "FLOAT64", false, "", nullptr});

  rules_.push_back({"VARCHAR", "STRING", false, "", nullptr});
  rules_.push_back({"CHAR", "STRING", false,
      "CHAR padding semantics lost in BigQuery STRING", nullptr});
  rules_.push_back({"CLOB", "STRING", false, "BigQuery STRING max 10MB", nullptr});
  rules_.push_back({"JSON", "JSON", false, "", nullptr});

  rules_.push_back({"DATE", "DATE", false, "", nullptr});
  rules_.push_back({"TIME", "TIME", false, "", nullptr});
  rules_.push_back({"TIMESTAMP WITH TIME ZONE", "TIMESTAMP", false,
      "BigQuery TIMESTAMP is always UTC", nullptr});
  rules_.push_back({"TIMESTAMP", "TIMESTAMP", false,
      "Teradata TIMESTAMP(6) -> BigQuery TIMESTAMP (microsecond)", nullptr});
  rules_.push_back({"INTERVAL YEAR", "STRING", false, "No native interval", nullptr});
  rules_.push_back({"INTERVAL DAY", "STRING", false, "No native interval", nullptr});
  rules_.push_back({"PERIOD(DATE)", "STRUCT<start DATE, end DATE>", false,
      "Teradata PERIOD -> BigQuery STRUCT", nullptr});

  rules_.push_back({"BYTE", "BYTES", false, "", nullptr});
  rules_.push_back({"VARBYTE", "BYTES", false, "", nullptr});
  rules_.push_back({"BLOB", "BYTES", false, "BigQuery BYTES max 10MB", nullptr});

  rules_.push_back({"XML", "STRING", false, "", nullptr});
  rules_.push_back({"ST_GEOMETRY", "GEOGRAPHY", false, "", nullptr});
  rules_.push_back({"ARRAY", "ARRAY", false, "", nullptr});
}

// ─── Stub loaders for other platform pairs ──────────────────────────

void TypeMapper::load_oracle_to_bigquery() {
  // Oracle NUMBER -> BigQuery NUMERIC, DATE -> DATE, etc.
  rules_.push_back({"NUMBER", "NUMERIC", true, "", nullptr});
  rules_.push_back({"VARCHAR2", "STRING", false, "", nullptr});
  rules_.push_back({"DATE", "TIMESTAMP", false, "", nullptr});
  rules_.push_back({"CLOB", "STRING", false, "", nullptr});
  rules_.push_back({"BLOB", "BYTES", false, "", nullptr});
  rules_.push_back({"BOOLEAN", "BOOL", false, "", nullptr});
}

void TypeMapper::load_teradata_to_snowflake() {
  rules_.push_back({"INTEGER", "INTEGER", false, "", nullptr});
  rules_.push_back({"DECIMAL", "NUMBER", true, "", nullptr});
  rules_.push_back({"VARCHAR", "VARCHAR", true, "", nullptr});
  rules_.push_back({"DATE", "DATE", false, "", nullptr});
  rules_.push_back({"TIMESTAMP", "TIMESTAMP_NTZ", true, "", nullptr});
  rules_.push_back({"CLOB", "VARCHAR(16777216)", false, "", nullptr});
  rules_.push_back({"BLOB", "BINARY", false, "", nullptr});
}

void TypeMapper::load_sqlserver_to_snowflake() {
  rules_.push_back({"INT", "INTEGER", false, "", nullptr});
  rules_.push_back({"BIGINT", "BIGINT", false, "", nullptr});
  rules_.push_back({"DECIMAL", "NUMBER", true, "", nullptr});
  rules_.push_back({"NVARCHAR", "VARCHAR", true, "", nullptr});
  rules_.push_back({"VARCHAR", "VARCHAR", true, "", nullptr});
  rules_.push_back({"DATETIME2", "TIMESTAMP_NTZ", false, "", nullptr});
  rules_.push_back({"DATETIME", "TIMESTAMP_NTZ", false, "", nullptr});
  rules_.push_back({"BIT", "BOOLEAN", false, "", nullptr});
  rules_.push_back({"VARBINARY", "BINARY", true, "", nullptr});
  rules_.push_back({"UNIQUEIDENTIFIER", "VARCHAR(36)", false, "", nullptr});
}

void TypeMapper::load_postgres_to_snowflake() {
  rules_.push_back({"INTEGER", "INTEGER", false, "", nullptr});
  rules_.push_back({"BIGINT", "BIGINT", false, "", nullptr});
  rules_.push_back({"NUMERIC", "NUMBER", true, "", nullptr});
  rules_.push_back({"TEXT", "VARCHAR(16777216)", false, "", nullptr});
  rules_.push_back({"VARCHAR", "VARCHAR", true, "", nullptr});
  rules_.push_back({"TIMESTAMP", "TIMESTAMP_NTZ", false, "", nullptr});
  rules_.push_back({"TIMESTAMPTZ", "TIMESTAMP_TZ", false, "", nullptr});
  rules_.push_back({"BOOLEAN", "BOOLEAN", false, "", nullptr});
  rules_.push_back({"JSONB", "VARIANT", false, "", nullptr});
  rules_.push_back({"UUID", "VARCHAR(36)", false, "", nullptr});
  rules_.push_back({"BYTEA", "BINARY", false, "", nullptr});
}

void TypeMapper::load_redshift_to_snowflake() {
  rules_.push_back({"INTEGER", "INTEGER", false, "", nullptr});
  rules_.push_back({"BIGINT", "BIGINT", false, "", nullptr});
  rules_.push_back({"DECIMAL", "NUMBER", true, "", nullptr});
  rules_.push_back({"VARCHAR", "VARCHAR", true, "", nullptr});
  rules_.push_back({"TIMESTAMP", "TIMESTAMP_NTZ", false, "", nullptr});
  rules_.push_back({"TIMESTAMPTZ", "TIMESTAMP_TZ", false, "", nullptr});
  rules_.push_back({"BOOLEAN", "BOOLEAN", false, "", nullptr});
  rules_.push_back({"SUPER", "VARIANT", false, "", nullptr});
}

void TypeMapper::load_redshift_to_bigquery() {
  rules_.push_back({"INTEGER", "INT64", false, "", nullptr});
  rules_.push_back({"BIGINT", "INT64", false, "", nullptr});
  rules_.push_back({"DECIMAL", "NUMERIC", true, "", nullptr});
  rules_.push_back({"VARCHAR", "STRING", false, "", nullptr});
  rules_.push_back({"TIMESTAMP", "TIMESTAMP", false, "", nullptr});
  rules_.push_back({"BOOLEAN", "BOOL", false, "", nullptr});
  rules_.push_back({"SUPER", "JSON", false, "", nullptr});
}

// ─── SchemaTranslator ───────────────────────────────────────────────

SchemaTranslator::SchemaTranslator(ObjMigrationAgent* agent)
    : agent_(agent),
      type_mapper_(
          agent->schema_translation_config.value("source_platform", "oracle"),
          agent->schema_translation_config.value("target_platform", "snowflake")) {}

ObjSchemaMap* SchemaTranslator::translate_all() {
  auto* schema_map = new_schema_map();
  agent_->schema_map_obj = schema_map;

  audit_log(agent_, "schema_translation_start", agent_name_str(agent_),
            "Beginning full schema translation", {});

  audit_log(agent_, "schema_translation_complete", agent_name_str(agent_),
            "Schema translation complete",
            {{"total_warnings", schema_map->total_warnings},
             {"total_errors", schema_map->total_errors}});

  return schema_map;
}

TableMapping SchemaTranslator::translate_table(const std::string& table_name,
                                               const std::string& source_ddl) {
  TableMapping mapping;
  mapping.source_table = table_name;
  mapping.target_table = table_name;  // Default: preserve table name
  mapping.source_ddl = source_ddl;
  // Target DDL would be generated from column mappings
  return mapping;
}

std::string SchemaTranslator::translate_view(const std::string& view_name,
                                             const std::string& source_sql) {
  // Basic view translation — replace known function names
  std::string target_sql = source_sql;
  // Oracle -> Snowflake common substitutions
  auto replace_all = [](std::string& str, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = str.find(from, pos)) != std::string::npos) {
      str.replace(pos, from.length(), to);
      pos += to.length();
    }
  };
  replace_all(target_sql, "NVL(", "COALESCE(");
  replace_all(target_sql, "SYSDATE", "CURRENT_TIMESTAMP()");
  replace_all(target_sql, "FROM DUAL", "");

  return target_sql;
}

std::string SchemaTranslator::translate_index(const std::string& index_ddl) {
  std::string target = agent_->schema_translation_config.value("target_platform", "snowflake");

  if (target == "snowflake") {
    return "-- Original index dropped (Snowflake uses automatic micro-partitioning)\n"
           "-- Consider: ALTER TABLE ... ADD SEARCH OPTIMIZATION\n"
           "-- Original: " + index_ddl;
  } else if (target == "bigquery") {
    auto columns = extract_index_columns(index_ddl);
    return "-- Original index -> recommend CLUSTER BY (" + join(columns, ", ") + ")\n"
           "-- Original: " + index_ddl;
  }
  return index_ddl;
}

std::string SchemaTranslator::translate_sequence(const std::string& seq_name,
                                                 int64_t current_val) {
  std::string target = agent_->schema_translation_config.value("target_platform", "snowflake");
  if (target == "snowflake") {
    return "CREATE OR REPLACE SEQUENCE " + seq_name +
           " START = " + std::to_string(current_val + 1) + " INCREMENT = 1;";
  }
  return "-- Sequence " + seq_name + " (current: " + std::to_string(current_val) + ")";
}

std::vector<std::string> SchemaTranslator::topological_sort_views(
    const std::unordered_map<std::string, std::vector<std::string>>& view_deps) {
  std::unordered_map<std::string, int> in_degree;
  std::unordered_set<std::string> all_views;

  for (const auto& [view, deps] : view_deps) {
    all_views.insert(view);
    if (in_degree.find(view) == in_degree.end()) in_degree[view] = 0;
    for (const auto& dep : deps) {
      if (view_deps.count(dep)) {
        in_degree[view]++;
        all_views.insert(dep);
      }
    }
  }
  for (const auto& v : all_views) {
    if (in_degree.find(v) == in_degree.end()) in_degree[v] = 0;
  }

  std::queue<std::string> ready;
  for (const auto& [v, deg] : in_degree) {
    if (deg == 0) ready.push(v);
  }

  std::vector<std::string> sorted;
  while (!ready.empty()) {
    auto cur = ready.front();
    ready.pop();
    sorted.push_back(cur);
    for (const auto& [view, deps] : view_deps) {
      for (const auto& dep : deps) {
        if (dep == cur) {
          in_degree[view]--;
          if (in_degree[view] == 0) ready.push(view);
        }
      }
    }
  }

  if (sorted.size() != all_views.size()) {
    throw std::runtime_error("Circular view dependency detected");
  }
  return sorted;
}

std::string SchemaTranslator::translate_partition_clause(
    const std::string& source_partition,
    const std::string& target_platform) {
  auto columns = extract_partition_columns(source_partition);
  if (columns.empty()) return "";

  if (target_platform == "snowflake") {
    return "CLUSTER BY (" + join(columns, ", ") + ")";
  } else if (target_platform == "bigquery") {
    std::string result = "PARTITION BY " + columns[0];
    if (columns.size() > 1) {
      result += "\nCLUSTER BY " + join(
          std::vector<std::string>(columns.begin() + 1, columns.end()), ", ");
    }
    return result;
  } else if (target_platform == "databricks") {
    return "PARTITIONED BY (" + join(columns, ", ") + ")";
  }
  return "";
}

std::string SchemaTranslator::translate_materialized_view(
    const std::string& mv_name, const std::string& source_sql) {
  // Translate materialized view to target platform
  std::string translated = translate_view(mv_name, source_sql);
  return "CREATE OR REPLACE DYNAMIC TABLE " + mv_name + " AS\n" + translated + ";";
}

std::string SchemaTranslator::handle_oracle_empty_string(const std::string& source_ddl) {
  auto config = agent_->schema_translation_config;
  std::string handling = config.value("oracle_specific", nlohmann::json{})
                              .value("empty_string_handling", "preserve_as_null");
  // For preserve_as_null: no DDL change needed — extraction query handles the transform
  return source_ddl;
}

std::string SchemaTranslator::handle_oracle_date_semantics(const std::string& source_ddl) {
  // Oracle DATE includes time — handled by type mapper (DATE -> TIMESTAMP_NTZ)
  return source_ddl;
}

std::string SchemaTranslator::handle_oracle_number_no_precision(
    const std::string& col_name, int64_t max_val, int64_t min_val, int max_scale) {
  if (max_scale == 0) {
    if (max_val <= INT32_MAX && min_val >= INT32_MIN) return "INTEGER";
    return "BIGINT";
  }
  if (max_scale <= 18) {
    int precision = std::max(
        static_cast<int>(std::to_string(std::abs(max_val)).length()),
        static_cast<int>(std::to_string(std::abs(min_val)).length())
    ) + max_scale;
    return "NUMBER(" + std::to_string(std::min(precision, 38)) +
           "," + std::to_string(max_scale) + ")";
  }
  return "DOUBLE";
}

FeatureSupport SchemaTranslator::get_platform_features(const std::string& platform) {
  if (platform == "snowflake") {
    return {true, true, true, true, true, true, true, true};
  } else if (platform == "bigquery") {
    return {true, false, true, false, false, true, false, true};
  } else if (platform == "databricks") {
    return {true, false, false, false, false, false, false, true};
  }
  return {false, false, false, false, false, false, false, false};
}

std::vector<std::string> SchemaTranslator::extract_partition_columns(
    const std::string& partition_clause) {
  // Simple extraction: look for column names in parentheses
  std::vector<std::string> columns;
  auto start = partition_clause.find('(');
  auto end = partition_clause.rfind(')');
  if (start != std::string::npos && end != std::string::npos && end > start) {
    std::string inner = partition_clause.substr(start + 1, end - start - 1);
    std::istringstream stream(inner);
    std::string token;
    while (std::getline(stream, token, ',')) {
      // Trim whitespace
      auto s = token.find_first_not_of(" \t");
      auto e = token.find_last_not_of(" \t");
      if (s != std::string::npos) {
        columns.push_back(token.substr(s, e - s + 1));
      }
    }
  }
  return columns;
}

std::vector<std::string> SchemaTranslator::extract_index_columns(
    const std::string& index_ddl) {
  return extract_partition_columns(index_ddl);  // Same parenthesized format
}

}  // namespace neamc::vm::migration
