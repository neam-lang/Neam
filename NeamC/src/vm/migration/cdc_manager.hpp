//
// Neam v0.9.2 — Phase 7: CDC Manager
//

#pragma once

#include <atomic>
#include <chrono>
#include <mutex>
#include <string>
#include <thread>

#include "neamc/vm/migration_types.hpp"

namespace neamc::vm::migration {

struct CDCStatus {
  bool active = false;
  std::chrono::milliseconds current_lag{0};
  std::chrono::milliseconds threshold{300000};   // 5m default
  std::chrono::milliseconds critical{1800000};   // 30m default
  int64_t transactions_pending = 0;
  int64_t transactions_applied = 0;
  std::string last_applied_timestamp;
  bool lag_exceeded = false;
  bool lag_critical = false;
};

class CDCManager {
public:
  explicit CDCManager(ObjMigrationAgent* agent);
  ~CDCManager();

  // Lifecycle
  bool start();
  bool stop();
  bool pause();
  bool resume();

  // Monitoring
  CDCStatus status() const;
  std::chrono::milliseconds current_lag() const;
  bool is_caught_up(std::chrono::milliseconds threshold) const;

  // Drain (for cutover)
  bool drain(std::chrono::milliseconds max_lag,
             std::chrono::seconds timeout);

  // Configuration
  void set_lag_threshold(std::chrono::milliseconds threshold);
  void set_lag_critical(std::chrono::milliseconds critical);

private:
  ObjMigrationAgent* agent_;
  std::atomic<bool> running_{false};
  CDCStatus status_;
  mutable std::mutex status_mutex_;

  // Platform-specific CDC implementations
  bool start_debezium_cdc();
  bool start_aws_dms_cdc();
  bool start_goldengate_cdc();
  bool start_timestamp_cdc();

  // Query CDC tool for current lag
  std::chrono::milliseconds query_lag();
};

}  // namespace neamc::vm::migration
