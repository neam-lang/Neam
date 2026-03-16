//
// Neam v0.9.2 — Phase 7: Cutover & Rollback Manager
//

#pragma once

#include <chrono>
#include <string>
#include <vector>

#include "neamc/vm/migration_types.hpp"

namespace neamc::vm::migration {

// Forward declarations
class CDCManager;
class ReconciliationEngine;

class CutoverOrchestrator {
public:
  CutoverOrchestrator(ObjMigrationAgent* agent, CDCManager* cdc);

  // Execute cutover (dispatches to strategy-specific method)
  bool execute();

  // Strategy-specific cutover
  bool execute_big_bang();
  bool execute_blue_green();
  bool execute_canary();
  bool execute_trickle();

  // Pre-cutover checks
  bool pre_cutover_validation();

private:
  ObjMigrationAgent* agent_;
  CDCManager* cdc_;

  std::string execute_on_target(const std::string& sql);
};

class RollbackManager {
public:
  RollbackManager(ObjMigrationAgent* agent, CDCManager* cdc);

  // Execute rollback
  bool execute();

  // Rollback readiness
  bool is_rollback_available() const;
  std::chrono::hours time_remaining() const;

  // Auto-rollback check
  bool should_auto_rollback(const ObjReconciliationResult* result,
                            const nlohmann::json& triggers);

private:
  ObjMigrationAgent* agent_;
  CDCManager* cdc_;

  std::string execute_on_source(const std::string& sql);
  double extract_threshold(const std::string& condition);
};

}  // namespace neamc::vm::migration
