#pragma once
#include "neamc/vm/object.hpp"
#include <string>
#include <mutex>
#include <nlohmann/json.hpp>

namespace neamc::vm {

struct ObjAgentRegistry : Obj { std::string name; std::string agents_json; };
struct ObjAgentContracts : Obj { std::string name; std::string contracts_json; };
struct ObjRACIMatrix : Obj { std::string name; std::string tasks_json; };
struct ObjTaskUnderstanding : Obj { std::string name; std::string intent_classifier_json; std::string detail_extraction_json; };
struct ObjTaskDecomposer : Obj { std::string name; std::string strategy_json; std::string output_json; };
struct ObjCrewFormation : Obj { std::string name; std::string strategy_json; };
struct ObjPatternSelector : Obj { std::string name; std::string patterns_json; std::string selection_json; };
struct ObjExecutionManagerDIO : Obj { std::string name; std::string modes_json; std::string resources_json; std::string monitoring_json; };
struct ObjDIOStateMachine : Obj { std::string name; std::string states_json; std::string transitions_json; std::string persistence_json; };
struct ObjDIOErrorHandling : Obj { std::string name; std::string strategies_json; std::string self_healing_json; };
struct ObjResultSynthesizer : Obj { std::string name; std::string synthesis_json; std::string delivery_json; };
struct ObjInfrastructureProfile : Obj { std::string name; std::string data_warehouse_json; std::string data_lake_json; std::string databases_json; std::string streaming_json; std::string data_science_json; std::string governance_json; std::string cicd_json; };
struct ObjRoleFramework : Obj { std::string name; std::string roles_json; std::string dio_role_json; };
struct ObjDelegationProtocol : Obj { std::string name; std::string delegation_json; };
struct ObjDIOAccountability : Obj { std::string name; std::string escalation_json; };

struct ObjDIOAgent : Obj {
  std::string name;
  std::string provider;
  std::string llm_model;
  double temperature = 0.2;
  std::string mode;
  std::string task;
  std::string agent_md_path;
  std::string infrastructure_ref;
  std::string agent_registry_ref;
  std::string raci_matrix_ref;
  std::string pattern_selector_ref;
  std::string crew_formation_ref;
  std::string execution_manager_ref;
  std::string state_machine_ref;
  std::string error_handling_ref;
  std::string result_synthesizer_ref;
  std::string managed_agents_json;
  std::string guardrails_json;
  std::string coordinates_with;
  std::string role;
  std::string purpose;
  std::string autonomy;
  std::string budget_ref;
  std::string status = "initialized";
  int tasks_delegated = 0;
  int tasks_completed = 0;
  int tasks_failed = 0;
  int messages_sent = 0;
  int messages_received = 0;
  double total_cost = 0.0;
  mutable std::mutex state_mutex;
};

ObjAgentRegistry* new_agent_registry();
ObjAgentContracts* new_agent_contracts();
ObjRACIMatrix* new_raci_matrix();
ObjTaskUnderstanding* new_task_understanding();
ObjTaskDecomposer* new_task_decomposer_dio();
ObjCrewFormation* new_crew_formation();
ObjPatternSelector* new_pattern_selector();
ObjExecutionManagerDIO* new_execution_manager_dio();
ObjDIOStateMachine* new_dio_state_machine();
ObjDIOErrorHandling* new_dio_error_handling();
ObjResultSynthesizer* new_result_synthesizer();
ObjInfrastructureProfile* new_infrastructure_profile();
ObjRoleFramework* new_role_framework();
ObjDelegationProtocol* new_delegation_protocol();
ObjDIOAccountability* new_dio_accountability();
ObjDIOAgent* new_dio_agent();

}  // namespace neamc::vm
