#include "neamc/vm/deploy_types.hpp"

namespace neamc::vm {

ObjDeployTarget* new_deploy_target() {
  auto* obj = new ObjDeployTarget();
  obj->type = ObjType::OBJ_DEPLOY_TARGET;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjPromotionRule* new_promotion_rule() {
  auto* obj = new ObjPromotionRule();
  obj->type = ObjType::OBJ_PROMOTION_RULE;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjRollbackPolicy* new_rollback_policy() {
  auto* obj = new ObjRollbackPolicy();
  obj->type = ObjType::OBJ_ROLLBACK_POLICY;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjArtifactRegistry* new_artifact_registry() {
  auto* obj = new ObjArtifactRegistry();
  obj->type = ObjType::OBJ_ARTIFACT_REGISTRY;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjDeployConfig* new_deploy_config() {
  auto* obj = new ObjDeployConfig();
  obj->type = ObjType::OBJ_DEPLOY_CONFIG;
  obj->marked = false;
  obj->next = nullptr;
  obj->deploy_status = "idle";
  return obj;
}

}  // namespace neamc::vm
