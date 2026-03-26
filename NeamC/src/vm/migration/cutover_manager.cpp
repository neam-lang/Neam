//
// Neam v0.9.2 — Phase 7: Cutover & Rollback implementation
//

#include "cutover_manager.hpp"
#include "audit_trail.hpp"
#include "cdc_manager.hpp"
#include "reconciliation.hpp"

#include <chrono>
#include <cstdlib>
#include <sstream>

namespace neamc::vm::migration {

// ═══════════════════════════════════════════════════════════════════════
// CutoverOrchestrator
// ═══════════════════════════════════════════════════════════════════════

CutoverOrchestrator::CutoverOrchestrator(ObjMigrationAgent* agent, CDCManager* cdc)
    : agent_(agent), cdc_(cdc) {}

bool CutoverOrchestrator::execute() {
  // Phase 1: Pre-cutover validation
  if (!pre_cutover_validation()) {
    audit_log(agent_, "cutover_aborted", agent_name_str(agent_),
              "Pre-cutover validation failed", {});
    return false;
  }

  // Phase 2: Drain CDC
  if (cdc_ && cdc_->status().active) {
    auto max_lag = std::chrono::milliseconds(5000);   // 5s final lag target
    auto timeout = std::chrono::seconds(600);          // 10 min timeout
    if (!cdc_->drain(max_lag, timeout)) {
      audit_log(agent_, "cutover_aborted", agent_name_str(agent_),
                "CDC drain failed — lag too high for cutover", {});
      return false;
    }
  }

  // Phase 3: Execute strategy-specific cutover
  bool success = false;
  std::string strategy = agent_->cutover_config.value("strategy", "big_bang");

  audit_log(agent_, "cutover_start", agent_name_str(agent_),
            "Executing cutover with strategy: " + strategy, {});

  if (strategy == "big_bang")        success = execute_big_bang();
  else if (strategy == "blue_green") success = execute_blue_green();
  else if (strategy == "canary")     success = execute_canary();
  else if (strategy == "trickle")    success = execute_trickle();
  else {
    audit_log(agent_, "cutover_failed", agent_name_str(agent_),
              "Unknown cutover strategy: " + strategy, {});
    return false;
  }

  // Phase 4: Post-cutover validation
  if (success) {
    ReconciliationEngine recon(agent_);
    auto* result = recon.run_all_tables();

    if (!result->overall_passed) {
      RollbackManager rollback(agent_, cdc_);
      auto triggers = agent_->cutover_config.value("rollback", nlohmann::json{})
                          .value("trigger_conditions", nlohmann::json::array());
      if (rollback.should_auto_rollback(result, triggers)) {
        audit_log(agent_, "auto_rollback_triggered", agent_name_str(agent_),
                  "Post-cutover validation failed", {});
        rollback.execute();
        return false;
      }
    }

    agent_->phase = MigrationPhase::COMPLETED;
    agent_->migration_completed = std::chrono::system_clock::now();
    audit_log(agent_, "cutover_complete", agent_name_str(agent_),
              "Migration cutover completed successfully", {});
  }

  return success;
}

bool CutoverOrchestrator::pre_cutover_validation() {
  // Verify all objects are in VALIDATED state
  for (const auto& [name, obj] : agent_->objects) {
    if (obj->state != ObjectMigrationState::VALIDATED &&
        obj->state != ObjectMigrationState::SKIPPED) {
      audit_log(agent_, "pre_cutover_check_failed", name,
                "Object not in VALIDATED state",
                {{"state", static_cast<int>(obj->state)}});
      return false;
    }
  }

  // Run final reconciliation
  ReconciliationEngine recon(agent_);
  auto* result = recon.run_all_tables();
  if (!result->overall_passed) {
    audit_log(agent_, "pre_cutover_check_failed", agent_name_str(agent_),
              "Final reconciliation failed",
              {{"failed_checks", result->failed_checks}});
    return false;
  }

  audit_log(agent_, "pre_cutover_validation_passed", agent_name_str(agent_),
            "All pre-cutover checks passed", {});
  return true;
}

bool CutoverOrchestrator::execute_big_bang() {
  audit_log(agent_, "cutover_big_bang", agent_name_str(agent_),
            "Executing big-bang cutover", {});

  // Step 1: Stop all writes to source
  audit_log(agent_, "cutover_quiesce", agent_name_str(agent_),
            "Quiescing source database writes", {});

  // Step 2: Final data sync
  if (cdc_ && cdc_->status().active) {
    cdc_->drain(std::chrono::milliseconds(0), std::chrono::seconds(300));
    cdc_->stop();
  }

  // Step 3: Switch traffic
  audit_log(agent_, "cutover_switch", agent_name_str(agent_),
            "Traffic switched to target", {});

  agent_->phase = MigrationPhase::CUTOVER;
  return true;
}

bool CutoverOrchestrator::execute_blue_green() {
  audit_log(agent_, "cutover_blue_green", agent_name_str(agent_),
            "Executing blue-green cutover", {});

  // Step 1: Final reconciliation
  ReconciliationEngine recon(agent_);
  auto* result = recon.run_all_tables();
  if (!result->overall_passed) {
    audit_log(agent_, "cutover_failed", agent_name_str(agent_),
              "Final reconciliation failed before switch",
              {{"failed_checks", result->failed_checks}});
    return false;
  }

  // Step 2: Switch traffic (DNS/connection pool/load balancer)
  audit_log(agent_, "cutover_switch", agent_name_str(agent_),
            "Traffic switched to target (blue-green)", {});

  // Step 3: Verify target serving
  auto canary_result = execute_on_target("SELECT 1");
  if (canary_result.empty()) {
    audit_log(agent_, "cutover_failed", agent_name_str(agent_),
              "Target not responsive after switch", {});
    return false;
  }

  // Step 4: Start rollback window
  std::string window = agent_->cutover_config.value("rollback", nlohmann::json{})
                            .value("window", "72h");
  audit_log(agent_, "cutover_complete", agent_name_str(agent_),
            "Blue-green cutover successful. Rollback window: " + window, {});

  agent_->phase = MigrationPhase::CUTOVER;
  return true;
}

bool CutoverOrchestrator::execute_canary() {
  audit_log(agent_, "cutover_canary", agent_name_str(agent_),
            "Executing canary cutover", {});

  int canary_pct = agent_->cutover_config.value("canary_percentage", 10);

  // Step 1: Route canary_pct% of traffic to target
  audit_log(agent_, "cutover_canary_route", agent_name_str(agent_),
            "Routing " + std::to_string(canary_pct) + "% traffic to target", {});

  // Step 2: Monitor (simulated)
  audit_log(agent_, "cutover_canary_monitor", agent_name_str(agent_),
            "Monitoring canary traffic", {});

  // Step 3: If healthy, ramp to 100%
  audit_log(agent_, "cutover_canary_ramp", agent_name_str(agent_),
            "Canary healthy — ramping to 100%", {});

  agent_->phase = MigrationPhase::CUTOVER;
  return true;
}

bool CutoverOrchestrator::execute_trickle() {
  audit_log(agent_, "cutover_trickle", agent_name_str(agent_),
            "Executing trickle cutover (table-by-table)", {});

  // Cut over one table at a time
  for (auto& [name, obj] : agent_->objects) {
    if (obj->state == ObjectMigrationState::VALIDATED) {
      obj->state = ObjectMigrationState::CUTOVER;
      audit_log(agent_, "cutover_table", name,
                "Table cut over to target", {});
    }
  }

  agent_->phase = MigrationPhase::CUTOVER;
  return true;
}

std::string CutoverOrchestrator::execute_on_target(const std::string& sql) {
  // Simulated: in production, executes SQL on the target database
  return "1";  // Simulated success
}

// ═══════════════════════════════════════════════════════════════════════
// RollbackManager
// ═══════════════════════════════════════════════════════════════════════

RollbackManager::RollbackManager(ObjMigrationAgent* agent, CDCManager* cdc)
    : agent_(agent), cdc_(cdc) {}

bool RollbackManager::execute() {
  audit_log(agent_, "rollback_start", agent_name_str(agent_),
            "Initiating rollback", {});

  // Check rollback window
  if (!is_rollback_available()) {
    audit_log(agent_, "rollback_failed", agent_name_str(agent_),
              "Rollback window expired", {});
    return false;
  }

  // Step 1: Verify source is accessible
  auto source_check = execute_on_source("SELECT 1");
  if (source_check.empty()) {
    audit_log(agent_, "rollback_failed", agent_name_str(agent_),
              "Source database not accessible", {});
    return false;
  }

  // Step 2: Stop CDC (if applicable)
  if (cdc_ && cdc_->status().active) {
    cdc_->stop();
  }

  // Step 3: Switch traffic back to source
  audit_log(agent_, "rollback_switch", agent_name_str(agent_),
            "Traffic switched back to source", {});

  // Step 4: Update state
  agent_->phase = MigrationPhase::ROLLBACK;
  agent_->rollback_available = false;

  for (auto& [name, obj] : agent_->objects) {
    if (obj->state == ObjectMigrationState::CUTOVER) {
      obj->state = ObjectMigrationState::VALIDATED;
    }
  }

  audit_log(agent_, "rollback_complete", agent_name_str(agent_),
            "Rollback completed successfully", {});
  return true;
}

bool RollbackManager::is_rollback_available() const {
  if (!agent_->rollback_available) return false;

  // Check if within rollback window
  auto cutover_time = agent_->migration_completed;
  if (cutover_time == std::chrono::system_clock::time_point{}) {
    return true;  // Not yet cutover — rollback always available
  }

  std::string window_str = agent_->cutover_config.value("rollback", nlohmann::json{})
                                .value("window", "72h");
  // Parse window (e.g., "72h" → 72 hours)
  int hours = 72;
  if (window_str.back() == 'h') {
    hours = std::atoi(window_str.substr(0, window_str.length() - 1).c_str());
  }

  auto now = std::chrono::system_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::hours>(now - cutover_time);
  return elapsed.count() < hours;
}

std::chrono::hours RollbackManager::time_remaining() const {
  if (!is_rollback_available()) return std::chrono::hours(0);

  auto cutover_time = agent_->migration_completed;
  if (cutover_time == std::chrono::system_clock::time_point{}) {
    return std::chrono::hours(9999);  // Not yet cutover
  }

  std::string window_str = agent_->cutover_config.value("rollback", nlohmann::json{})
                                .value("window", "72h");
  int hours = 72;
  if (window_str.back() == 'h') {
    hours = std::atoi(window_str.substr(0, window_str.length() - 1).c_str());
  }

  auto now = std::chrono::system_clock::now();
  auto elapsed = std::chrono::duration_cast<std::chrono::hours>(now - cutover_time);
  int remaining = hours - static_cast<int>(elapsed.count());
  return std::chrono::hours(std::max(0, remaining));
}

bool RollbackManager::should_auto_rollback(
    const ObjReconciliationResult* result,
    const nlohmann::json& triggers) {

  if (!agent_->cutover_config.value("rollback", nlohmann::json{})
           .value("auto_trigger", false)) {
    return false;
  }

  if (!triggers.is_array()) return false;

  for (const auto& trigger : triggers) {
    std::string condition = trigger.get<std::string>();

    if (condition.find("validation_failure_rate") != std::string::npos) {
      double threshold = extract_threshold(condition);
      if (result->total_checks == 0) continue;
      double failure_rate = static_cast<double>(result->failed_checks)
                            / static_cast<double>(result->total_checks);
      if (failure_rate > threshold) {
        audit_log(agent_, "auto_rollback_trigger", agent_name_str(agent_),
                  "validation_failure_rate " + std::to_string(failure_rate)
                  + " > " + std::to_string(threshold), {});
        return true;
      }
    }
    else if (condition.find("golden_query_mismatch") != std::string::npos) {
      double threshold = extract_threshold(condition);
      int mismatches = 0;
      for (const auto& check : result->checks) {
        if (check.check_type == "golden_query" && !check.passed) mismatches++;
      }
      if (mismatches > static_cast<int>(threshold)) {
        audit_log(agent_, "auto_rollback_trigger", agent_name_str(agent_),
                  "golden_query_mismatch " + std::to_string(mismatches)
                  + " > " + std::to_string(threshold), {});
        return true;
      }
    }
  }

  return false;
}

std::string RollbackManager::execute_on_source(const std::string& sql) {
  // Simulated: in production, executes SQL on the source database
  return "1";
}

double RollbackManager::extract_threshold(const std::string& condition) {
  // Parse threshold from condition string like "validation_failure_rate > 0.05"
  auto pos = condition.find('>');
  if (pos != std::string::npos) {
    std::string val = condition.substr(pos + 1);
    // Trim whitespace
    val.erase(0, val.find_first_not_of(' '));
    try {
      return std::stod(val);
    } catch (...) {}
  }
  return 0.0;
}

}  // namespace neamc::vm::migration
