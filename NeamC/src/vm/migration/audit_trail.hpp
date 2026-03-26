//
// Neam v0.9.2 — Migration Agent audit trail
//

#pragma once

#include <mutex>
#include <string>
#include <vector>

#include <nlohmann/json.hpp>

#include "neamc/vm/migration_types.hpp"

namespace neamc::vm::migration {

struct AuditEntry {
  std::string timestamp;
  std::string event_type;
  std::string object_name;
  std::string detail;
  nlohmann::json metadata;
  std::string user;
};

class AuditTrail {
public:
  explicit AuditTrail(ObjMigrationAgent* agent);

  void log(const std::string& event_type,
           const std::string& object_name,
           const std::string& detail,
           const nlohmann::json& metadata = {});

  std::vector<AuditEntry> query(const std::string& event_type = "",
                                const std::string& object_name = "",
                                int limit = 100) const;

  std::string export_jsonl() const;
  int total_entries() const;

private:
  ObjMigrationAgent* agent_;
  std::vector<AuditEntry> entries_;
  mutable std::mutex entries_mutex_;
};

// Helper to extract agent name as std::string
inline std::string agent_name_str(const ObjMigrationAgent* agent) {
  if (agent && agent->name) return std::string(agent->name->chars, agent->name->length);
  return "unknown";
}

// Free function for use across migration engines
void audit_log(ObjMigrationAgent* agent,
               const std::string& event_type,
               const std::string& object_name,
               const std::string& detail,
               const nlohmann::json& metadata = {});

}  // namespace neamc::vm::migration
