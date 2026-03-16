//
// Neam v0.9.5 — Erwin Data Modeler Connector
// Parses .erwin XML files for entity/attribute/relationship metadata
//

#pragma once

#include "neamc/src/vm/modeling/tool_connector.hpp"

#include <string>

namespace neamc::vm
{

class ErwinConnector : public ModelingToolConnector
{
public:
  explicit ErwinConnector(const std::string& file_path, SyncDirection direction)
      : file_path_(file_path), direction_(direction)
  {
  }

  nlohmann::json read_model() override;
  bool write_model(const nlohmann::json& model) override;
  nlohmann::json detect_changes() override;
  nlohmann::json sync(SyncDirection direction) override;
  nlohmann::json get_metadata() override;

private:
  std::string file_path_;
  SyncDirection direction_;
};

}  // namespace neamc::vm
