//
// Neam v0.9.5 — DataMart Generation Engine
// Creates purpose-specific data marts from dimensional models
//

#pragma once

#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

namespace neamc::vm
{

class DataMartEngine
{
public:
  // Generate a DataMart from a dimensional model
  nlohmann::json generate_mart(
      const nlohmann::json& dimensional_model,
      const nlohmann::json& config);

  // Generate aggregate tables
  nlohmann::json generate_aggregates(
      const nlohmann::json& mart,
      const nlohmann::json& aggregate_config);

  // Generate materialization DDL
  std::string generate_materialization_ddl(
      const nlohmann::json& mart,
      const std::string& platform);

  // Generate ETL pipeline stubs (sends to ETLAgent v0.9.1)
  nlohmann::json generate_etl_stubs(const nlohmann::json& mart);

  // Validate mart against dimensional model
  nlohmann::json validate_mart(
      const nlohmann::json& mart,
      const nlohmann::json& dimensional_model);

private:
  std::mutex engine_mutex_;
};

}  // namespace neamc::vm
