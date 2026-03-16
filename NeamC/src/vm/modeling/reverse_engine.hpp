//
// Neam v0.9.5 — Reverse-Engineering Engine
// Reads metadata from connected schema sources and produces unified schema representation
//

#pragma once

#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

namespace neamc::vm
{

struct ObjSchemaSource;

class ReverseEngineeringEngine
{
public:
  // Read metadata from a schema source
  nlohmann::json reverse_engineer(const ObjSchemaSource* source);

  // Infer relationships across all sources
  nlohmann::json infer_relationships(
      const std::vector<ObjSchemaSource*>& sources,
      double confidence_threshold = 0.80);

  // Read metadata from specific platforms
  nlohmann::json read_information_schema(const std::string& connection, const std::string& database);
  nlohmann::json read_oracle_catalog(const std::string& connection, const std::string& database);
  nlohmann::json read_erwin_xml(const std::string& file_path);
  nlohmann::json read_parquet_schema(const std::string& file_path);
  nlohmann::json read_avro_schema(const std::string& file_path);
  nlohmann::json read_json_schema(const std::string& file_path, int sample_size);
  nlohmann::json read_delta_log(const std::string& path);
  nlohmann::json read_dbt_manifest(const std::string& manifest_path);
  nlohmann::json parse_ddl(const std::string& ddl_text, const std::string& dialect);

private:
  std::mutex engine_mutex_;
  nlohmann::json cached_schemas_;
};

}  // namespace neamc::vm
