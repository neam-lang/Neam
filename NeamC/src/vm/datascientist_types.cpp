#include "neamc/vm/datascientist_types.hpp"

namespace neamc::vm {

#define DS_FACTORY(TypeName, ObjEnum) \
  TypeName* new_##TypeName() { \
    auto* obj = new TypeName(); \
    obj->type = ObjType::ObjEnum; \
    obj->marked = false; \
    obj->next = nullptr; \
    return obj; \
  }

// We can't use the macro directly because function names don't match type names.
// Use explicit factory functions following the exact v0.9.7 pattern.

ObjProblemStatement* new_problem_statement() {
  auto* obj = new ObjProblemStatement();
  obj->type = ObjType::OBJ_PROBLEM_STATEMENT;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjHypothesisTest* new_hypothesis_test() {
  auto* obj = new ObjHypothesisTest();
  obj->type = ObjType::OBJ_HYPOTHESIS_TEST;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjFeatureEngineering* new_feature_engineering() {
  auto* obj = new ObjFeatureEngineering();
  obj->type = ObjType::OBJ_FEATURE_ENGINEERING;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjMLExperiment* new_ml_experiment() {
  auto* obj = new ObjMLExperiment();
  obj->type = ObjType::OBJ_ML_EXPERIMENT;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjAutoMLConfig* new_automl_config() {
  auto* obj = new ObjAutoMLConfig();
  obj->type = ObjType::OBJ_AUTOML_CONFIG;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjHyperparameterConfig* new_hyperparameter_config() {
  auto* obj = new ObjHyperparameterConfig();
  obj->type = ObjType::OBJ_HYPERPARAMETER_CONFIG;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjStackedModel* new_stacked_model() {
  auto* obj = new ObjStackedModel();
  obj->type = ObjType::OBJ_STACKED_MODEL;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjEvaluationConfig* new_evaluation_config() {
  auto* obj = new ObjEvaluationConfig();
  obj->type = ObjType::OBJ_EVALUATION_CONFIG;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjDSModelRegistry* new_ds_model_registry() {
  auto* obj = new ObjDSModelRegistry();
  obj->type = ObjType::OBJ_DS_MODEL_REGISTRY;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjExplainabilityConfig* new_explainability_config() {
  auto* obj = new ObjExplainabilityConfig();
  obj->type = ObjType::OBJ_EXPLAINABILITY_CONFIG;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjCodeInterpreter* new_code_interpreter() {
  auto* obj = new ObjCodeInterpreter();
  obj->type = ObjType::OBJ_CODE_INTERPRETER;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjVenvManager* new_venv_manager() {
  auto* obj = new ObjVenvManager();
  obj->type = ObjType::OBJ_VENV_MANAGER;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjNLPPipeline* new_nlp_pipeline() {
  auto* obj = new ObjNLPPipeline();
  obj->type = ObjType::OBJ_NLP_PIPELINE;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjChurnAnalysis* new_churn_analysis() {
  auto* obj = new ObjChurnAnalysis();
  obj->type = ObjType::OBJ_CHURN_ANALYSIS;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjCLVModel* new_clv_model() {
  auto* obj = new ObjCLVModel();
  obj->type = ObjType::OBJ_CLV_MODEL;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjPropensityModel* new_propensity_model() {
  auto* obj = new ObjPropensityModel();
  obj->type = ObjType::OBJ_PROPENSITY_MODEL;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjRecommendationEngine* new_recommendation_engine() {
  auto* obj = new ObjRecommendationEngine();
  obj->type = ObjType::OBJ_RECOMMENDATION_ENGINE;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjExperimentDesign* new_experiment_design() {
  auto* obj = new ObjExperimentDesign();
  obj->type = ObjType::OBJ_EXPERIMENT_DESIGN;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjScenarioAnalysis* new_scenario_analysis() {
  auto* obj = new ObjScenarioAnalysis();
  obj->type = ObjType::OBJ_SCENARIO_ANALYSIS;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjDecisionSupport* new_decision_support() {
  auto* obj = new ObjDecisionSupport();
  obj->type = ObjType::OBJ_DECISION_SUPPORT;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjEDAConfig* new_eda_config() {
  auto* obj = new ObjEDAConfig();
  obj->type = ObjType::OBJ_EDA_CONFIG;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjEDATechniqueSelector* new_eda_technique_selector() {
  auto* obj = new ObjEDATechniqueSelector();
  obj->type = ObjType::OBJ_EDA_TECHNIQUE_SELECTOR;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjSmartConnector* new_smart_connector() {
  auto* obj = new ObjSmartConnector();
  obj->type = ObjType::OBJ_SMART_CONNECTOR;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjVolumeRouter* new_volume_router() {
  auto* obj = new ObjVolumeRouter();
  obj->type = ObjType::OBJ_VOLUME_ROUTER;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjComputeConnector* new_compute_connector() {
  auto* obj = new ObjComputeConnector();
  obj->type = ObjType::OBJ_COMPUTE_CONNECTOR;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjFileConnector* new_file_connector() {
  auto* obj = new ObjFileConnector();
  obj->type = ObjType::OBJ_FILE_CONNECTOR;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjDistributedComputeConfig* new_distributed_compute_config() {
  auto* obj = new ObjDistributedComputeConfig();
  obj->type = ObjType::OBJ_DISTRIBUTED_COMPUTE_CONFIG;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjPerformanceConfig* new_performance_config() {
  auto* obj = new ObjPerformanceConfig();
  obj->type = ObjType::OBJ_PERFORMANCE_CONFIG;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjDataQualityPipeline* new_data_quality_pipeline() {
  auto* obj = new ObjDataQualityPipeline();
  obj->type = ObjType::OBJ_DATA_QUALITY_PIPELINE;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjSelfCorrectionConfig* new_self_correction_config() {
  auto* obj = new ObjSelfCorrectionConfig();
  obj->type = ObjType::OBJ_SELF_CORRECTION_CONFIG;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjSelfAssessment* new_self_assessment() {
  auto* obj = new ObjSelfAssessment();
  obj->type = ObjType::OBJ_SELF_ASSESSMENT;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjAdaptiveKnowledgeConfig* new_adaptive_knowledge_config() {
  auto* obj = new ObjAdaptiveKnowledgeConfig();
  obj->type = ObjType::OBJ_ADAPTIVE_KNOWLEDGE_CONFIG;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjAnalysisHistory* new_analysis_history() {
  auto* obj = new ObjAnalysisHistory();
  obj->type = ObjType::OBJ_ANALYSIS_HISTORY;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjObservabilityConfig* new_observability_config() {
  auto* obj = new ObjObservabilityConfig();
  obj->type = ObjType::OBJ_OBSERVABILITY_CONFIG;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjDataScientistAgent* new_datascientist_agent() {
  auto* obj = new ObjDataScientistAgent();
  obj->type = ObjType::OBJ_DATASCIENTIST_AGENT;
  obj->marked = false;
  obj->next = nullptr;
  obj->status = "initialized";
  return obj;
}

}  // namespace neamc::vm
