#include "neamc/vm/causal_types.hpp"

namespace neamc::vm {

ObjCausalDiscovery* new_causal_discovery() {
  auto* obj = new ObjCausalDiscovery();
  obj->type = ObjType::OBJ_CAUSAL_DISCOVERY;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjSCM* new_scm() {
  auto* obj = new ObjSCM();
  obj->type = ObjType::OBJ_SCM;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjIntervention* new_intervention() {
  auto* obj = new ObjIntervention();
  obj->type = ObjType::OBJ_INTERVENTION;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjCounterfactual* new_counterfactual() {
  auto* obj = new ObjCounterfactual();
  obj->type = ObjType::OBJ_COUNTERFACTUAL;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjBayesianModel* new_bayesian_model() {
  auto* obj = new ObjBayesianModel();
  obj->type = ObjType::OBJ_BAYESIAN_MODEL;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjCausalEstimator* new_causal_estimator() {
  auto* obj = new ObjCausalEstimator();
  obj->type = ObjType::OBJ_CAUSAL_ESTIMATOR;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjQuasiExperiment* new_quasi_experiment() {
  auto* obj = new ObjQuasiExperiment();
  obj->type = ObjType::OBJ_QUASI_EXPERIMENT;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjCausalSensitivity* new_causal_sensitivity() {
  auto* obj = new ObjCausalSensitivity();
  obj->type = ObjType::OBJ_CAUSAL_SENSITIVITY;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjCausalDataRequirements* new_causal_data_requirements() {
  auto* obj = new ObjCausalDataRequirements();
  obj->type = ObjType::OBJ_CAUSAL_DATA_REQUIREMENTS;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjCausalAgent* new_causal_agent() {
  auto* obj = new ObjCausalAgent();
  obj->type = ObjType::OBJ_CAUSAL_AGENT;
  obj->marked = false;
  obj->next = nullptr;
  obj->status = "initialized";
  return obj;
}

}  // namespace neamc::vm
