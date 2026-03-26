#include "neamc/vm/mlops_types.hpp"

namespace neamc::vm {

#define MLOPS_FACTORY(TypeName, EnumVal) \
  TypeName* new_##TypeName() { \
    auto* obj = new TypeName(); \
    obj->type = ObjType::EnumVal; \
    obj->marked = false; \
    obj->next = nullptr; \
    return obj; \
  }

ObjDriftMonitor* new_drift_monitor() {
  auto* obj = new ObjDriftMonitor(); obj->type = ObjType::OBJ_DRIFT_MONITOR; obj->marked = false; obj->next = nullptr; return obj;
}
ObjRetrainingPipeline* new_retraining_pipeline() {
  auto* obj = new ObjRetrainingPipeline(); obj->type = ObjType::OBJ_RETRAINING_PIPELINE; obj->marked = false; obj->next = nullptr; return obj;
}
ObjMLDeployStrategy* new_ml_deploy_strategy() {
  auto* obj = new ObjMLDeployStrategy(); obj->type = ObjType::OBJ_ML_DEPLOY_STRATEGY; obj->marked = false; obj->next = nullptr; return obj;
}
ObjChampionChallenger* new_champion_challenger() {
  auto* obj = new ObjChampionChallenger(); obj->type = ObjType::OBJ_CHAMPION_CHALLENGER; obj->marked = false; obj->next = nullptr; return obj;
}
ObjServingInfra* new_serving_infra() {
  auto* obj = new ObjServingInfra(); obj->type = ObjType::OBJ_SERVING_INFRA; obj->marked = false; obj->next = nullptr; return obj;
}
ObjTrainingInfraMLOps* new_training_infra_mlops() {
  auto* obj = new ObjTrainingInfraMLOps(); obj->type = ObjType::OBJ_TRAINING_INFRA_MLOPS; obj->marked = false; obj->next = nullptr; return obj;
}
ObjMLOpsRollback* new_mlops_rollback() {
  auto* obj = new ObjMLOpsRollback(); obj->type = ObjType::OBJ_MLOPS_ROLLBACK; obj->marked = false; obj->next = nullptr; return obj;
}
ObjMonitoringStack* new_monitoring_stack() {
  auto* obj = new ObjMonitoringStack(); obj->type = ObjType::OBJ_MONITORING_STACK; obj->marked = false; obj->next = nullptr; return obj;
}
ObjMLflowConfig* new_mlflow_config() {
  auto* obj = new ObjMLflowConfig(); obj->type = ObjType::OBJ_MLFLOW_CONFIG; obj->marked = false; obj->next = nullptr; return obj;
}
ObjBusinessKPITracker* new_business_kpi_tracker() {
  auto* obj = new ObjBusinessKPITracker(); obj->type = ObjType::OBJ_BUSINESS_KPI_TRACKER; obj->marked = false; obj->next = nullptr; return obj;
}
ObjDatasetVersion* new_dataset_version() {
  auto* obj = new ObjDatasetVersion(); obj->type = ObjType::OBJ_DATASET_VERSION; obj->marked = false; obj->next = nullptr; return obj;
}
ObjFeedbackLoop* new_feedback_loop() {
  auto* obj = new ObjFeedbackLoop(); obj->type = ObjType::OBJ_FEEDBACK_LOOP; obj->marked = false; obj->next = nullptr; return obj;
}
ObjDecisionEngine* new_decision_engine() {
  auto* obj = new ObjDecisionEngine(); obj->type = ObjType::OBJ_DECISION_ENGINE; obj->marked = false; obj->next = nullptr; return obj;
}
ObjEventBus* new_event_bus() {
  auto* obj = new ObjEventBus(); obj->type = ObjType::OBJ_EVENT_BUS; obj->marked = false; obj->next = nullptr; return obj;
}
ObjDriftRCA* new_drift_rca() {
  auto* obj = new ObjDriftRCA(); obj->type = ObjType::OBJ_DRIFT_RCA; obj->marked = false; obj->next = nullptr; return obj;
}
ObjMLOpsAgent* new_mlops_agent() {
  auto* obj = new ObjMLOpsAgent(); obj->type = ObjType::OBJ_MLOPS_AGENT; obj->marked = false; obj->next = nullptr; obj->status = "initialized"; return obj;
}

}  // namespace neamc::vm
