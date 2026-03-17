#pragma once
#include "neamc/vm/object.hpp"
#include <string>
#include <vector>
#include <mutex>
#include <nlohmann/json.hpp>

namespace neamc::vm {

// ═══════════════════════════════════════════════════════════════
// v0.9.7 Data Pipeline Deployment Runtime Types
// ═══════════════════════════════════════════════════════════════

struct ObjDeployTarget : Obj {
  std::string name;
  std::string environment;
  std::string connection;
  std::string namespace_name;
  std::string region;
  nlohmann::json tags;
  nlohmann::json variables;
  bool frozen = false;
  std::string freeze_reason;

  // Runtime state
  std::string current_version;
  std::string last_deploy_time;
  std::string last_deploy_status;
  mutable std::mutex state_mutex;
};

struct ObjPromotionRule : Obj {
  std::string name;
  std::string from_env;
  std::string to_env;
  bool require_tests = true;
  bool require_approval = false;
  std::vector<std::string> approvers;
  bool auto_promote = false;
  std::string cooldown;
  nlohmann::json gate_checks;

  // Runtime state
  std::string last_promotion_time;
  std::string last_promotion_status;
  mutable std::mutex state_mutex;
};

struct ObjRollbackPolicy : Obj {
  std::string name;
  std::string strategy;
  bool keep_data = true;
  bool notify_dataops = true;
  std::string max_rollback_window;
  bool reconciliation = true;
  nlohmann::json pre_rollback_checks;
  nlohmann::json notifications;

  // Runtime state
  int rollbacks_executed = 0;
  std::string last_rollback_time;
  mutable std::mutex state_mutex;
};

struct ObjArtifactRegistry : Obj {
  std::string name;
  std::string storage;
  std::string path;
  std::string versioning;
  std::string retention;
  bool sign_artifacts = false;
  std::string checksum;
  bool immutable = true;

  // Runtime state
  std::vector<std::string> published_versions;
  mutable std::mutex state_mutex;
};

struct ObjDeployConfig : Obj {
  std::string name;
  std::string target;
  std::string strategy;
  bool approval_gate = false;
  std::string pipeline_ref;
  nlohmann::json pre_deploy_checks;
  nlohmann::json post_deploy_checks;
  nlohmann::json notifications;
  std::string schedule;
  bool auto_rollback = true;
  std::string rollback_policy;
  std::string artifact_registry;

  // Runtime state
  std::string current_version;
  std::string deploy_status = "idle";
  std::vector<nlohmann::json> deploy_history;
  mutable std::mutex state_mutex;
};

// Factory functions
ObjDeployTarget* new_deploy_target();
ObjPromotionRule* new_promotion_rule();
ObjRollbackPolicy* new_rollback_policy();
ObjArtifactRegistry* new_artifact_registry();
ObjDeployConfig* new_deploy_config();

}  // namespace neamc::vm
