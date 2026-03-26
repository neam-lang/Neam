//
// Neam v0.9.5 — Dimensional Design Engine
// Generates Star, Snowflake, Starflake, and Data Vault 2.0 schemas
//

#pragma once

#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "neamc/ast.hpp"

namespace neamc::vm
{

class DimensionalDesignEngine
{
public:
  // Design a dimensional model from source schema
  nlohmann::json design(
      const nlohmann::json& source_schema,
      DimensionalMethodology methodology);

  // Generate star schema
  nlohmann::json generate_star(const nlohmann::json& source_schema);

  // Generate snowflake schema
  nlohmann::json generate_snowflake(const nlohmann::json& source_schema);

  // Generate starflake schema (hybrid)
  nlohmann::json generate_starflake(const nlohmann::json& source_schema);

  // Generate Data Vault 2.0 (hub/link/satellite)
  nlohmann::json generate_data_vault(const nlohmann::json& source_schema);

  // Identify facts, dimensions, grain, measures
  nlohmann::json identify_facts(const nlohmann::json& schema);
  nlohmann::json identify_dimensions(const nlohmann::json& schema);
  std::string determine_grain(const nlohmann::json& fact_table);
  nlohmann::json identify_measures(const nlohmann::json& fact_table);
  nlohmann::json detect_conformed_dimensions(const std::vector<nlohmann::json>& facts);

  // SCD recommendation
  int recommend_scd_type(const nlohmann::json& dimension, const nlohmann::json& change_patterns);

  // Generate DDL for the dimensional model
  std::string generate_ddl(const nlohmann::json& model, const std::string& platform);

  // Generate dbt models
  nlohmann::json generate_dbt_models(const nlohmann::json& model);

  // Generate date dimension
  nlohmann::json generate_date_dimension(
      const std::string& start, const std::string& end,
      const std::string& fiscal_year_start);

private:
  std::mutex engine_mutex_;
};

}  // namespace neamc::vm
