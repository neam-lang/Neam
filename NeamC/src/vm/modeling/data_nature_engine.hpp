//
// Neam v0.9.5 — Data Nature Engine
// Profiles data across structured/semi-structured/unstructured spectrum
//

#pragma once

#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

namespace neamc::vm
{

class DataNatureEngine
{
public:
  // Profile structured data source
  nlohmann::json profile_structured(const nlohmann::json& schema);

  // Infer schema from semi-structured data
  nlohmann::json infer_json_schema(const std::string& path, int sample_size);
  nlohmann::json infer_xml_schema(const std::string& path, int sample_size);
  nlohmann::json infer_avro_schema(const std::string& path);
  nlohmann::json infer_csv_schema(const std::string& path, int sample_size);

  // Profile unstructured data
  nlohmann::json profile_unstructured(const std::string& path, const std::string& type);

  // Read file-based schemas
  nlohmann::json read_parquet_schema(const std::string& path);
  nlohmann::json read_orc_schema(const std::string& path);
  nlohmann::json read_delta_schema(const std::string& path);
  nlohmann::json read_iceberg_schema(const std::string& path);
  nlohmann::json read_hudi_schema(const std::string& path);

  // Suggest entity model from inferred schemas
  nlohmann::json suggest_entity_model(const nlohmann::json& inferred_schema);

  // Detect PII in semi-structured/unstructured data (GovernanceAgent integration)
  nlohmann::json detect_pii(const nlohmann::json& data_sample);

  // Full profile across all data natures
  nlohmann::json full_profile(const nlohmann::json& config);

private:
  std::mutex engine_mutex_;
};

}  // namespace neamc::vm
