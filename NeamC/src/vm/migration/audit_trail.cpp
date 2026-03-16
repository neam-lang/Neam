//
// Neam v0.9.2 — Migration Agent audit trail implementation
//

#include "audit_trail.hpp"

#include <chrono>
#include <iomanip>
#include <sstream>

namespace neamc::vm::migration {

namespace {
std::string iso_timestamp() {
  auto now = std::chrono::system_clock::now();
  auto t = std::chrono::system_clock::to_time_t(now);
  std::ostringstream oss;
  oss << std::put_time(std::gmtime(&t), "%FT%TZ");
  return oss.str();
}
}  // namespace

AuditTrail::AuditTrail(ObjMigrationAgent* agent) : agent_(agent) {}

void AuditTrail::log(const std::string& event_type,
                     const std::string& object_name,
                     const std::string& detail,
                     const nlohmann::json& metadata) {
  AuditEntry entry;
  entry.timestamp = iso_timestamp();
  entry.event_type = event_type;
  entry.object_name = object_name;
  entry.detail = detail;
  entry.metadata = metadata;
  entry.user = "system";

  std::lock_guard<std::mutex> lock(entries_mutex_);
  entries_.push_back(std::move(entry));
}

std::vector<AuditEntry> AuditTrail::query(const std::string& event_type,
                                          const std::string& object_name,
                                          int limit) const {
  std::lock_guard<std::mutex> lock(entries_mutex_);
  std::vector<AuditEntry> result;
  for (auto it = entries_.rbegin(); it != entries_.rend() && static_cast<int>(result.size()) < limit; ++it) {
    if (!event_type.empty() && it->event_type != event_type) continue;
    if (!object_name.empty() && it->object_name != object_name) continue;
    result.push_back(*it);
  }
  return result;
}

std::string AuditTrail::export_jsonl() const {
  std::lock_guard<std::mutex> lock(entries_mutex_);
  std::ostringstream oss;
  for (const auto& e : entries_) {
    nlohmann::json j;
    j["timestamp"] = e.timestamp;
    j["event_type"] = e.event_type;
    j["object_name"] = e.object_name;
    j["detail"] = e.detail;
    j["metadata"] = e.metadata;
    j["user"] = e.user;
    oss << j.dump() << "\n";
  }
  return oss.str();
}

int AuditTrail::total_entries() const {
  std::lock_guard<std::mutex> lock(entries_mutex_);
  return static_cast<int>(entries_.size());
}

// Global audit log helper — each migration agent gets a lazily-created AuditTrail
static std::unordered_map<std::string, std::unique_ptr<AuditTrail>> g_audit_trails;
static std::mutex g_audit_mutex;

void audit_log(ObjMigrationAgent* agent,
               const std::string& event_type,
               const std::string& object_name,
               const std::string& detail,
               const nlohmann::json& metadata) {
  std::lock_guard<std::mutex> lock(g_audit_mutex);
  std::string agent_key = agent->name ? std::string(agent->name->chars, agent->name->length) : "unknown";
  auto& trail = g_audit_trails[agent_key];
  if (!trail) {
    trail = std::make_unique<AuditTrail>(agent);
  }
  trail->log(event_type, object_name, detail, metadata);
}

}  // namespace neamc::vm::migration
