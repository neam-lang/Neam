//
// Neam v0.9.5 — ER/Studio Connector
// REST API integration with ER/Studio Team Server
//

#pragma once

#include "neamc/src/vm/modeling/tool_connector.hpp"

#include <string>

namespace neamc::vm
{

class ERStudioConnector : public ModelingToolConnector
{
public:
  ERStudioConnector(const std::string& api_url, const std::string& credentials,
                    const std::string& repository, SyncDirection direction)
      : api_url_(api_url), credentials_(credentials),
        repository_(repository), direction_(direction)
  {
  }

  nlohmann::json read_model() override;
  bool write_model(const nlohmann::json& model) override;
  nlohmann::json detect_changes() override;
  nlohmann::json sync(SyncDirection direction) override;
  nlohmann::json get_metadata() override;

private:
  std::string api_url_;
  std::string credentials_;
  std::string repository_;
  SyncDirection direction_;
};

}  // namespace neamc::vm
