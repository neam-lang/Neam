//
// Neam v0.9.5 — dbt Connector
// Parses schema.yml and *.sql model files
//

#pragma once

#include "neamc/src/vm/modeling/tool_connector.hpp"

#include <string>

namespace neamc::vm
{

class DbtConnector : public ModelingToolConnector
{
public:
  DbtConnector(const std::string& project_path, const std::string& manifest_path,
               SyncDirection direction)
      : project_path_(project_path), manifest_path_(manifest_path),
        direction_(direction)
  {
  }

  nlohmann::json read_model() override;
  bool write_model(const nlohmann::json& model) override;
  nlohmann::json detect_changes() override;
  nlohmann::json sync(SyncDirection direction) override;
  nlohmann::json get_metadata() override;

private:
  std::string project_path_;
  std::string manifest_path_;
  SyncDirection direction_;
};

}  // namespace neamc::vm
