#include "neamc/vm/databa_types.hpp"

namespace neamc::vm {

#define BA_FACTORY(Type, Enum) Type* new_##Type() { auto* o = new Type(); o->type = ObjType::Enum; o->marked = false; o->next = nullptr; return o; }

ObjRequirementsElicitation* new_requirements_elicitation() { auto* o = new ObjRequirementsElicitation(); o->type = ObjType::OBJ_REQUIREMENTS_ELICITATION; o->marked = false; o->next = nullptr; return o; }
ObjBRDGenerator* new_brd_generator() { auto* o = new ObjBRDGenerator(); o->type = ObjType::OBJ_BRD_GENERATOR; o->marked = false; o->next = nullptr; return o; }
ObjFunctionalSpec* new_functional_spec_ba() { auto* o = new ObjFunctionalSpec(); o->type = ObjType::OBJ_FUNCTIONAL_SPEC; o->marked = false; o->next = nullptr; return o; }
ObjNonfunctionalSpec* new_nonfunctional_spec() { auto* o = new ObjNonfunctionalSpec(); o->type = ObjType::OBJ_NONFUNCTIONAL_SPEC; o->marked = false; o->next = nullptr; return o; }
ObjAcceptanceCriteriaGen* new_acceptance_criteria_gen() { auto* o = new ObjAcceptanceCriteriaGen(); o->type = ObjType::OBJ_ACCEPTANCE_CRITERIA_GEN; o->marked = false; o->next = nullptr; return o; }
ObjDataRequirementsBA* new_data_requirements_ba() { auto* o = new ObjDataRequirementsBA(); o->type = ObjType::OBJ_DATA_REQUIREMENTS_BA; o->marked = false; o->next = nullptr; return o; }
ObjImpactAnalysisBA* new_impact_analysis_ba() { auto* o = new ObjImpactAnalysisBA(); o->type = ObjType::OBJ_IMPACT_ANALYSIS_BA; o->marked = false; o->next = nullptr; return o; }
ObjTraceabilityMatrix* new_traceability_matrix() { auto* o = new ObjTraceabilityMatrix(); o->type = ObjType::OBJ_TRACEABILITY_MATRIX; o->marked = false; o->next = nullptr; return o; }
ObjETLRequirementSpec* new_etl_requirement_spec() { auto* o = new ObjETLRequirementSpec(); o->type = ObjType::OBJ_ETL_REQUIREMENT_SPEC; o->marked = false; o->next = nullptr; return o; }
ObjMLRequirementSpec* new_ml_requirement_spec() { auto* o = new ObjMLRequirementSpec(); o->type = ObjType::OBJ_ML_REQUIREMENT_SPEC; o->marked = false; o->next = nullptr; return o; }
ObjGovernanceRequirementSpec* new_governance_requirement_spec() { auto* o = new ObjGovernanceRequirementSpec(); o->type = ObjType::OBJ_GOVERNANCE_REQUIREMENT_SPEC; o->marked = false; o->next = nullptr; return o; }
ObjAnalyticsRequirementSpec* new_analytics_requirement_spec() { auto* o = new ObjAnalyticsRequirementSpec(); o->type = ObjType::OBJ_ANALYTICS_REQUIREMENT_SPEC; o->marked = false; o->next = nullptr; return o; }
ObjStakeholderAnalysis* new_stakeholder_analysis() { auto* o = new ObjStakeholderAnalysis(); o->type = ObjType::OBJ_STAKEHOLDER_ANALYSIS; o->marked = false; o->next = nullptr; return o; }
ObjUserStoryGenerator* new_user_story_generator() { auto* o = new ObjUserStoryGenerator(); o->type = ObjType::OBJ_USER_STORY_GENERATOR; o->marked = false; o->next = nullptr; return o; }
ObjScopeManagement* new_scope_management() { auto* o = new ObjScopeManagement(); o->type = ObjType::OBJ_SCOPE_MANAGEMENT; o->marked = false; o->next = nullptr; return o; }
ObjChangeImpactAnalyzer* new_change_impact_analyzer() { auto* o = new ObjChangeImpactAnalyzer(); o->type = ObjType::OBJ_CHANGE_IMPACT_ANALYZER; o->marked = false; o->next = nullptr; return o; }
ObjDataBAAgent* new_databa_agent() { auto* o = new ObjDataBAAgent(); o->type = ObjType::OBJ_DATABA_AGENT; o->marked = false; o->next = nullptr; o->status = "initialized"; return o; }

}  // namespace neamc::vm
