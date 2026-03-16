//
// Neam v0.9.5 — SAP PowerDesigner Connector
// Parses .pdm/.ldm/.cdm XML files
//

#pragma once

#include "neamc/src/vm/modeling/tool_connector.hpp"

#include <string>

namespace neamc::vm
{

class PowerDesignerConnector : public ModelingToolConnector
{
public:
  explicit PowerDesignerConnector(const std::string& file_path, SyncDirection direction)
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
