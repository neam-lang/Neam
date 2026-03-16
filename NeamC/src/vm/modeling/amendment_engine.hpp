//
// Neam v0.9.5 — Amendment Proposal Engine
// Detects schema changes, performs impact analysis, generates amendment proposals
//

#pragma once

#include <mutex>
#include <string>

#include <nlohmann/json.hpp>

#include "neamc/ast.hpp"

namespace neamc::vm
{

class AmendmentEngine
{
public:
  // Detect schema changes between two snapshots
  nlohmann::json detect_changes(
      const nlohmann::json& old_schema,
      const nlohmann::json& new_schema);

  // Classify a change as non_breaking, breaking, or requires_review
  AmendmentType classify_change(const nlohmann::json& change);

  // Perform impact analysis for a set of changes
  nlohmann::json impact_analysis(
      const nlohmann::json& changes,
      const nlohmann::json& lineage_graph);

  // Generate amendment proposal document
  std::string generate_proposal(
      const nlohmann::json& changes,
      const nlohmann::json& impact,
      const std::string& format);

  // Generate migration scripts
  std::string generate_migration(
      const nlohmann::json& changes,
      const std::string& platform);

  // Generate rollback scripts
  std::string generate_rollback(
      const nlohmann::json& changes,
      const std::string& platform);

private:
  std::mutex engine_mutex_;
};

}  // namespace neamc::vm
