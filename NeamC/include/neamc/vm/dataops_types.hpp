//
// Neam v0.9.3 — DataOps Agent runtime types
//

#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "neamc/vm/data_agent_types.hpp"
#include "neamc/vm/object.hpp"

namespace neamc::vm
{

// DataOps monitoring phase
enum class DataOpsPhase
{
  IDLE,
  MONITORING,
  INCIDENT_DETECTED,
  TRIAGING,
  INVESTIGATING,
  REMEDIATING,
  ESCALATED,
  RESOLVED
};

// Incident severity
enum class IncidentSeverity
{
  P1_CRITICAL,
  P2_HIGH,
  P3_MEDIUM,
  P4_LOW
};

// Scheduler runtime object
struct ObjScheduler : public Obj
{
  ObjScheduler() { type = ObjType::OBJ_SCHEDULER; }

  ObjString* name = nullptr;
  std::string scheduler_type;
  std::string connection;
  std::string credentials;
  std::string poll_interval;
  std::vector<std::string> filters;
  std::string timezone;
  std::string datacenter;
  std::string host;

  // Runtime state
  bool connected = false;
  int consecutive_poll_failures = 0;
  std::chrono::steady_clock::time_point last_poll;
};

// Audit table runtime object
struct ObjAuditTable : public Obj
{
  ObjAuditTable() { type = ObjType::OBJ_AUDIT_TABLE; }

  ObjString* name = nullptr;
  std::string source_ref;
  std::string table_name;
  nlohmann::json column_map_json;
  std::string poll_interval;
  std::string lookback_window;
  std::string retention_analysis;

  // Anomaly config
  double row_count_drop_threshold = 0.20;
  double row_count_spike_threshold = 3.0;
  double duration_spike_threshold = 2.0;
  double failure_rate_threshold = 0.05;
  int zero_rows_consecutive_threshold = 3;

  // Runtime baselines
  double baseline_row_count = 0.0;
  double baseline_duration = 0.0;
  int zero_rows_streak = 0;
  std::chrono::steady_clock::time_point last_poll;
};

// Log source runtime object
struct ObjLogSource : public Obj
{
  ObjLogSource() { type = ObjType::OBJ_LOG_SOURCE; }

  ObjString* name = nullptr;
  std::string source_type;
  std::string connection;
  std::string credentials;
  std::vector<std::string> views;
  std::string log_file;
  std::string log_format;
  std::string poll_interval;
  std::string lookback_window;
  nlohmann::json alerts_json;

  // Runtime state
  std::string last_log_position;
  std::chrono::steady_clock::time_point last_poll;
};

// Platform monitor runtime object
struct ObjPlatformMonitor : public Obj
{
  ObjPlatformMonitor() { type = ObjType::OBJ_PLATFORM_MONITOR; }

  ObjString* name = nullptr;
  std::string platform_type;
  std::string connection;
  std::string credentials;
  std::string database;
  nlohmann::json health_checks_json;
  nlohmann::json finops_json;

  // Runtime FinOps tracking
  double cost_today = 0.0;
  double cost_this_hour = 0.0;
  std::unordered_map<std::string, double> warehouse_costs;
  std::chrono::steady_clock::time_point last_poll;
};

// Incident policy runtime object
struct ObjIncidentPolicy : public Obj
{
  ObjIncidentPolicy() { type = ObjType::OBJ_INCIDENT_POLICY; }

  ObjString* name = nullptr;
  nlohmann::json severity_rules_json;
  nlohmann::json auto_heal_json;
};

// Correlation runtime object
struct ObjCorrelation : public Obj
{
  ObjCorrelation() { type = ObjType::OBJ_CORRELATION; }

  ObjString* name = nullptr;
  nlohmann::json scope_json;
  std::string time_window;
  nlohmann::json dependencies_json;
  nlohmann::json sla_json;
};

// Incident runtime object
struct ObjIncident : public Obj
{
  ObjIncident() { type = ObjType::OBJ_INCIDENT; }

  std::string id;
  IncidentSeverity severity = IncidentSeverity::P4_LOW;
  std::string source_type;
  std::string source_name;
  std::string summary;
  std::string root_cause;
  std::string impact;
  std::vector<std::string> correlated_events;
  std::vector<std::string> actions_taken;
  std::string status;  // "open", "investigating", "remediated", "escalated", "closed"
  std::chrono::system_clock::time_point created_at;
  std::chrono::system_clock::time_point resolved_at;
};

// Top-level DataOps agent runtime object
struct ObjDataOpsAgent : public ObjDataAgent
{
  ObjDataOpsAgent() { type = ObjType::OBJ_DATAOPS_AGENT; }

  // DataOps references (resolved at runtime)
  std::vector<std::string> platform_refs;
  std::vector<std::string> scheduler_refs;
  std::vector<std::string> audit_table_refs;
  std::vector<std::string> log_source_refs;
  std::vector<std::string> correlation_refs;
  std::string incident_policy_ref;

  // Mode
  std::string mode;  // "continuous", "scheduled", "on_demand"

  // Reports config
  nlohmann::json reports_json;

  // Runtime state
  DataOpsPhase phase = DataOpsPhase::IDLE;
  bool monitoring_active = false;
  std::atomic<int> incidents_open{0};
  std::atomic<int> incidents_today{0};
  std::atomic<int> remediations_today{0};
  std::atomic<double> cost_today{0.0};
  std::chrono::steady_clock::time_point started_at;

  // Active incidents
  std::unordered_map<std::string, ObjIncident*> active_incidents;

  // Thread safety
  mutable std::mutex state_mutex;
};

}  // namespace neamc::vm
