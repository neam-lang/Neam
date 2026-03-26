//
// Neam v0.9.5 — Modeling Tool Connector Interface
// Base class for Erwin, ER/Studio, PowerDesigner, dbt connectors
//

#pragma once

#include <string>

#include <nlohmann/json.hpp>

#include "neamc/ast.hpp"

namespace neamc::vm
{

class ModelingToolConnector
{
public:
  virtual ~ModelingToolConnector() = default;

  // Read model from tool
  virtual nlohmann::json read_model() = 0;

  // Write model to tool
  virtual bool write_model(const nlohmann::json& model) = 0;

  // Detect changes since last sync
  virtual nlohmann::json detect_changes() = 0;

  // Sync model with tool
  virtual nlohmann::json sync(SyncDirection direction) = 0;

  // Get tool metadata
  virtual nlohmann::json get_metadata() = 0;
};

}  // namespace neamc::vm
