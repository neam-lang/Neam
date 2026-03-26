//
// Neam v0.9.2 — Phase 7: CDC Manager implementation
//

#include "cdc_manager.hpp"
#include "audit_trail.hpp"

#include <chrono>
#include <thread>

namespace neamc::vm::migration {

CDCManager::CDCManager(ObjMigrationAgent* agent) : agent_(agent) {
  // Initialize thresholds from config
  auto cdc_cfg = agent_->movement_config.value("cdc", nlohmann::json{});
  int lag_ms = cdc_cfg.value("lag_threshold", 300000);
  int crit_ms = cdc_cfg.value("lag_critical", 1800000);
  status_.threshold = std::chrono::milliseconds(lag_ms);
  status_.critical = std::chrono::milliseconds(crit_ms);
}

CDCManager::~CDCManager() {
  stop();
}

bool CDCManager::start() {
  if (running_.load()) return true;

  auto cdc_cfg = agent_->movement_config.value("cdc", nlohmann::json{});
  std::string tool = cdc_cfg.value("tool", "timestamp");

  audit_log(agent_, "cdc_start", agent_name_str(agent_),
            "Starting CDC with tool: " + tool, {});

  bool ok = false;
  if (tool == "debezium")         ok = start_debezium_cdc();
  else if (tool == "aws_dms")     ok = start_aws_dms_cdc();
  else if (tool == "goldengate")  ok = start_goldengate_cdc();
  else                            ok = start_timestamp_cdc();

  if (ok) {
    running_.store(true);
    std::lock_guard<std::mutex> lock(status_mutex_);
    status_.active = true;
  }

  return ok;
}

bool CDCManager::stop() {
  if (!running_.load()) return true;

  running_.store(false);

  {
    std::lock_guard<std::mutex> lock(status_mutex_);
    status_.active = false;
  }

  audit_log(agent_, "cdc_stop", agent_name_str(agent_),
            "CDC stopped", {});
  return true;
}

bool CDCManager::pause() {
  if (!running_.load()) return false;
  running_.store(false);
  audit_log(agent_, "cdc_pause", agent_name_str(agent_), "CDC paused", {});
  return true;
}

bool CDCManager::resume() {
  if (running_.load()) return true;
  running_.store(true);
  {
    std::lock_guard<std::mutex> lock(status_mutex_);
    status_.active = true;
  }
  audit_log(agent_, "cdc_resume", agent_name_str(agent_), "CDC resumed", {});
  return true;
}

CDCStatus CDCManager::status() const {
  std::lock_guard<std::mutex> lock(status_mutex_);
  return status_;
}

std::chrono::milliseconds CDCManager::current_lag() const {
  std::lock_guard<std::mutex> lock(status_mutex_);
  return status_.current_lag;
}

bool CDCManager::is_caught_up(std::chrono::milliseconds threshold) const {
  return current_lag() <= threshold;
}

bool CDCManager::drain(std::chrono::milliseconds max_lag,
                       std::chrono::seconds timeout) {
  auto start = std::chrono::steady_clock::now();
  auto deadline = start + timeout;

  audit_log(agent_, "cdc_drain_start", agent_name_str(agent_),
            "Starting CDC drain",
            {{"max_lag_ms", max_lag.count()},
             {"timeout_s", timeout.count()}});

  while (std::chrono::steady_clock::now() < deadline) {
    auto lag = query_lag();

    {
      std::lock_guard<std::mutex> lock(status_mutex_);
      status_.current_lag = lag;
      status_.lag_exceeded = (lag > status_.threshold);
      status_.lag_critical = (lag > status_.critical);
    }

    if (lag <= max_lag) {
      audit_log(agent_, "cdc_drain_complete", agent_name_str(agent_),
                "CDC lag drained to " + std::to_string(lag.count()) + "ms",
                {{"threshold_ms", max_lag.count()}});
      return true;
    }

    // Log progress every 30 seconds
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
        std::chrono::steady_clock::now() - start);
    if (elapsed.count() > 0 && elapsed.count() % 30 == 0) {
      audit_log(agent_, "cdc_drain_progress", agent_name_str(agent_),
                "CDC lag: " + std::to_string(lag.count()) + "ms",
                {{"elapsed_seconds", elapsed.count()}});
    }

    std::this_thread::sleep_for(std::chrono::seconds(1));
  }

  audit_log(agent_, "cdc_drain_timeout", agent_name_str(agent_),
            "CDC drain timed out",
            {{"final_lag_ms", status_.current_lag.count()}});
  return false;
}

void CDCManager::set_lag_threshold(std::chrono::milliseconds threshold) {
  std::lock_guard<std::mutex> lock(status_mutex_);
  status_.threshold = threshold;
}

void CDCManager::set_lag_critical(std::chrono::milliseconds critical) {
  std::lock_guard<std::mutex> lock(status_mutex_);
  status_.critical = critical;
}

// ─── Platform-specific CDC starters ─────────────────────────────────

bool CDCManager::start_debezium_cdc() {
  audit_log(agent_, "cdc_init", agent_name_str(agent_),
            "Initializing Debezium CDC connector", {});
  // In production: submit Kafka Connect configuration for Debezium
  return true;
}

bool CDCManager::start_aws_dms_cdc() {
  audit_log(agent_, "cdc_init", agent_name_str(agent_),
            "Initializing AWS DMS CDC task", {});
  // In production: create/start DMS replication task with CDC
  return true;
}

bool CDCManager::start_goldengate_cdc() {
  audit_log(agent_, "cdc_init", agent_name_str(agent_),
            "Initializing Oracle GoldenGate extract/replicat", {});
  // In production: configure GoldenGate extract + replicat processes
  return true;
}

bool CDCManager::start_timestamp_cdc() {
  audit_log(agent_, "cdc_init", agent_name_str(agent_),
            "Initializing timestamp-based CDC polling", {});
  // Fallback: poll source using modified_at/updated_at columns
  return true;
}

std::chrono::milliseconds CDCManager::query_lag() {
  // Simulated: in production, queries the CDC tool for actual lag
  // e.g., Debezium: query Kafka Connect connector status
  // e.g., DMS: describe-replication-tasks
  // e.g., GoldenGate: query lag from GGSCI
  return std::chrono::milliseconds(0);  // Simulated zero lag
}

}  // namespace neamc::vm::migration
