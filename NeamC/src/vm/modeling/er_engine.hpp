//
// Neam v0.9.5 — ER Modeling Engine
// Generates and maintains conceptual, logical, and physical ER models
//

#pragma once

#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

#include "neamc/ast.hpp"

namespace neamc::vm
{

class ERModelingEngine
{
public:
  // Generate model at specified level from reverse-engineered schema
  nlohmann::json generate_model(
      const nlohmann::json& schema,
      ModelLevel level,
      ERNotation notation);

  // Abstract physical model to logical
  nlohmann::json physical_to_logical(const nlohmann::json& physical_model);

  // Abstract logical model to conceptual
  nlohmann::json logical_to_conceptual(
      const nlohmann::json& logical_model,
      const nlohmann::json& domains);

  // Implement logical model as physical (platform-specific)
  nlohmann::json logical_to_physical(
      const nlohmann::json& logical_model,
      const std::string& target_platform);

  // Validate model at specified level
  nlohmann::json validate_model(
      const nlohmann::json& model,
      ModelLevel level);

  // Export model to external format
  std::string export_to_erwin_xml(const nlohmann::json& model);
  std::string export_to_ddl(const nlohmann::json& physical_model, const std::string& dialect);
  std::string export_to_dbt(const nlohmann::json& physical_model);

private:
  std::mutex engine_mutex_;
};

}  // namespace neamc::vm
