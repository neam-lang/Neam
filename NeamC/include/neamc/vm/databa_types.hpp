#pragma once
#include "neamc/vm/object.hpp"
#include <string>
#include <mutex>
#include <nlohmann/json.hpp>

namespace neamc::vm {

struct ObjRequirementsElicitation : Obj { std::string name; std::string stakeholders_json; std::string methods_json; std::string output_json; };
struct ObjBRDGenerator : Obj { std::string name; std::string project_json; std::string objectives_json; std::string scope_json; std::string benefits_json; std::string constraints_json; std::string assumptions_json; std::string risks_json; std::string output_json; };
struct ObjFunctionalSpec : Obj { std::string name; std::string data_requirements_json; std::string etl_requirements_json; std::string ml_requirements_json; std::string analytics_requirements_json; };
struct ObjNonfunctionalSpec : Obj { std::string name; std::string performance_json; std::string reliability_json; std::string security_json; std::string scalability_json; std::string data_quality_json; std::string maintainability_json; };
struct ObjAcceptanceCriteriaGen : Obj { std::string name; std::string patterns_json; bool auto_generate = true; bool review_required = true; std::string quality_json; };
struct ObjDataRequirementsBA : Obj { std::string name; std::string source_to_target_json; std::string target_schema_json; std::string quality_rules_json; };
struct ObjImpactAnalysisBA : Obj { std::string name; std::string upstream_json; std::string downstream_json; std::string change_scenarios_json; std::string lineage_json; };
struct ObjTraceabilityMatrix : Obj { std::string name; std::string entries_json; std::string auto_trace_json; std::string reports_json; };
struct ObjETLRequirementSpec : Obj { std::string name; std::string pipeline_json; std::string feature_groups_json; std::string quality_gates_json; std::string upstream_dependencies; std::string downstream_consumers; };
struct ObjMLRequirementSpec : Obj { std::string name; std::string problem_json; std::string success_criteria_json; std::string feature_requirements_json; std::string serving_json; std::string explainability_json; std::string monitoring_json; };
struct ObjGovernanceRequirementSpec : Obj { std::string name; std::string data_classification_json; std::string access_requirements_json; std::string compliance_json; std::string quality_sla_json; };
struct ObjAnalyticsRequirementSpec : Obj { std::string name; std::string reports_json; std::string kpi_definitions_json; };
struct ObjStakeholderAnalysis : Obj { std::string name; std::string stakeholders_json; std::string raci_matrix_json; };
struct ObjUserStoryGenerator : Obj { std::string name; std::string source_ref; std::string epics_json; std::string stories_json; std::string generation_json; };
struct ObjScopeManagement : Obj { std::string name; std::string bcar_json; std::string change_management_json; };
struct ObjChangeImpactAnalyzer : Obj { std::string name; std::string analysis_json; std::string output_json; };

struct ObjDataBAAgent : Obj {
  std::string name;
  std::string provider;
  std::string llm_model;
  std::string system_prompt;
  double temperature = 0.3;
  std::string agent_md_path;
  std::string downstream_agents_json;
  std::string elicitation_ref;
  std::string brd_ref;
  std::string functional_spec_ref;
  std::string nfr_spec_ref;
  std::string data_requirements_ref;
  std::string impact_analysis_ref;
  std::string traceability_ref;
  std::string etl_spec_ref;
  std::string ml_spec_ref;
  std::string governance_spec_ref;
  std::string analytics_spec_ref;
  std::string stakeholders_ref;
  std::string user_stories_ref;
  std::string scope_ref;
  std::string coordinates_with;
  std::string handoffs;
  std::string role;
  std::string purpose;
  std::string autonomy;
  std::string budget_ref;
  std::string status = "initialized";
  int requirements_generated = 0;
  int specs_produced = 0;
  mutable std::mutex state_mutex;
};

ObjRequirementsElicitation* new_requirements_elicitation();
ObjBRDGenerator* new_brd_generator();
ObjFunctionalSpec* new_functional_spec_ba();
ObjNonfunctionalSpec* new_nonfunctional_spec();
ObjAcceptanceCriteriaGen* new_acceptance_criteria_gen();
ObjDataRequirementsBA* new_data_requirements_ba();
ObjImpactAnalysisBA* new_impact_analysis_ba();
ObjTraceabilityMatrix* new_traceability_matrix();
ObjETLRequirementSpec* new_etl_requirement_spec();
ObjMLRequirementSpec* new_ml_requirement_spec();
ObjGovernanceRequirementSpec* new_governance_requirement_spec();
ObjAnalyticsRequirementSpec* new_analytics_requirement_spec();
ObjStakeholderAnalysis* new_stakeholder_analysis();
ObjUserStoryGenerator* new_user_story_generator();
ObjScopeManagement* new_scope_management();
ObjChangeImpactAnalyzer* new_change_impact_analyzer();
ObjDataBAAgent* new_databa_agent();

}  // namespace neamc::vm
