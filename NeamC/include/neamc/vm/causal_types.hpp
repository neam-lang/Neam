#pragma once
#include "neamc/vm/object.hpp"
#include <string>
#include <vector>
#include <mutex>
#include <nlohmann/json.hpp>

namespace neamc::vm {

// ═══════════════════════════════════════════════════════════════
// v0.9.8.1 Causal Agent Runtime Types
// ═══════════════════════════════════════════════════════════════

struct ObjCausalDiscovery : Obj {
  std::string name;
  std::string llm_discovery_json;
  std::string algorithmic_discovery_json;
  std::string merge_strategy_json;
  std::string validation_json;
  // Runtime state
  nlohmann::json discovered_dag;
  std::string discovery_status = "pending";
  mutable std::mutex state_mutex;
};

struct ObjSCM : Obj {
  std::string name;
  std::string variables_json;
  std::string exogenous_json;
  std::string latent_confounders_json;
  std::string dag_ref;
  // Runtime state
  std::string fitted_status = "unfitted";
  nlohmann::json posterior_results;
  mutable std::mutex state_mutex;
};

struct ObjIntervention : Obj {
  std::string name;
  std::string scm_ref;
  std::string do_json;
  std::string outcome;
  std::string identification_json;
  std::string estimation_json;
  bool compare_with_naive = true;
  // Runtime state
  nlohmann::json effect_estimate;
  std::string estimation_status = "pending";
  mutable std::mutex state_mutex;
};

struct ObjCounterfactual : Obj {
  std::string name;
  std::string scm_ref;
  std::string evidence_json;
  std::string question;
  std::string abduction_json;
  std::string action_json;
  std::string prediction_json;
  std::string attribution_json;
  // Runtime state
  nlohmann::json counterfactual_result;
  std::string cf_status = "pending";
  mutable std::mutex state_mutex;
};

struct ObjBayesianModel : Obj {
  std::string name;
  std::string framework;
  std::string version;
  std::string priors_json;
  std::string likelihood_json;
  std::string sampling_json;
  std::string posterior_json;
  std::string comparison_json;
  // Runtime state
  std::string fit_status = "unfitted";
  nlohmann::json posterior_summary;
  mutable std::mutex state_mutex;
};

struct ObjCausalEstimator : Obj {
  std::string name;
  std::string scm_ref;
  std::string treatment;
  std::string outcome;
  std::string primary_json;
  std::string secondary_json;
  std::string heterogeneous_json;
  bool compare_estimators = true;
  // Runtime state
  nlohmann::json estimates;
  mutable std::mutex state_mutex;
};

struct ObjQuasiExperiment : Obj {
  std::string name;
  std::string method;
  std::string treatment_time;
  std::string treatment_group;
  std::string control_group;
  std::string outcome;
  std::string covariates;
  bool parallel_trends_test = true;
  bool bayesian = true;
  std::string mcmc_json;
  std::string running_variable;
  double cutoff = 0.0;
  std::string bandwidth;
  // Runtime state
  nlohmann::json results;
  mutable std::mutex state_mutex;
};

struct ObjCausalSensitivity : Obj {
  std::string name;
  std::string estimator_ref;
  std::string rosenbaum_json;
  bool e_value = true;
  std::string refutations_json;
  std::string assumptions_json;
  std::string output_json;
  // Runtime state
  nlohmann::json sensitivity_results;
  mutable std::mutex state_mutex;
};

struct ObjCausalDataRequirements : Obj {
  std::string name;
  std::string temporal_json;
  std::string required_confounders;
  std::string instruments_json;
  std::string natural_experiments;
  std::string quality_json;
};

struct ObjCausalAgent : Obj {
  std::string name;
  std::string provider;
  std::string llm_model;
  std::string system_prompt;
  double temperature = 0.3;
  std::string agent_md_path;
  std::string sub_agents_json;
  std::string forge_ref;
  std::string peer_agent_ref;
  std::string discovery_ref;
  std::string scm_ref;
  std::string intervention_ref;
  std::string counterfactual_ref;
  std::string bayesian_model_ref;
  std::string estimator_ref;
  std::string sensitivity_ref;
  std::string data_requirements_ref;
  std::string code_interpreter_ref;
  std::string coordinates_with;
  std::string handoffs;
  std::string role;
  std::string purpose;
  std::string autonomy;
  std::string budget_ref;
  // Runtime state
  std::string status = "initialized";
  mutable std::mutex state_mutex;
};

// Factory functions
ObjCausalDiscovery*        new_causal_discovery();
ObjSCM*                    new_scm();
ObjIntervention*           new_intervention();
ObjCounterfactual*         new_counterfactual();
ObjBayesianModel*          new_bayesian_model();
ObjCausalEstimator*        new_causal_estimator();
ObjQuasiExperiment*        new_quasi_experiment();
ObjCausalSensitivity*      new_causal_sensitivity();
ObjCausalDataRequirements* new_causal_data_requirements();
ObjCausalAgent*            new_causal_agent();

}  // namespace neamc::vm
