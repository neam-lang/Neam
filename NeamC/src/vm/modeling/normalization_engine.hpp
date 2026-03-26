//
// Neam v0.9.5 — Normalization Analysis Engine
// Evaluates tables against normal forms (1NF-5NF/BCNF), discovers FDs, suggests decompositions
//

#pragma once

#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "neamc/ast.hpp"

namespace neamc::vm
{

class NormalizationEngine
{
public:
  // Analyze a table for normalization violations
  nlohmann::json analyze_table(
      const nlohmann::json& table_schema,
      NormalForm target_nf);

  // Analyze entire schema/database
  nlohmann::json analyze_schema(
      const nlohmann::json& schema,
      NormalForm target_nf);

  // Discover functional dependencies
  nlohmann::json discover_fds(
      const nlohmann::json& table_schema,
      const nlohmann::json& data_sample,
      int max_columns_per_fd = 3);

  // Determine current NF level of a table
  NormalForm determine_nf_level(
      const nlohmann::json& table_schema,
      const std::vector<nlohmann::json>& fds);

  // Suggest decomposition for a violation
  nlohmann::json suggest_decomposition(
      const nlohmann::json& table_schema,
      const nlohmann::json& violation,
      NormalForm target_nf);

  // Calculate normalization health score (0-100)
  int calculate_score(const nlohmann::json& analysis_result);

private:
  nlohmann::json check_1nf(const nlohmann::json& table_schema);
  nlohmann::json check_2nf(const nlohmann::json& table_schema, const std::vector<nlohmann::json>& fds);
  nlohmann::json check_3nf(const nlohmann::json& table_schema, const std::vector<nlohmann::json>& fds);
  nlohmann::json check_bcnf(const nlohmann::json& table_schema, const std::vector<nlohmann::json>& fds);
  nlohmann::json check_4nf(const nlohmann::json& table_schema);
  nlohmann::json check_5nf(const nlohmann::json& table_schema);

  std::mutex engine_mutex_;
};

}  // namespace neamc::vm
