#include "neamc/vm/dio_types.hpp"
namespace neamc::vm {
ObjAgentRegistry* new_agent_registry() { auto* o = new ObjAgentRegistry(); o->type = ObjType::OBJ_AGENT_REGISTRY; o->marked = false; o->next = nullptr; return o; }
ObjAgentContracts* new_agent_contracts() { auto* o = new ObjAgentContracts(); o->type = ObjType::OBJ_AGENT_CONTRACTS; o->marked = false; o->next = nullptr; return o; }
ObjRACIMatrix* new_raci_matrix() { auto* o = new ObjRACIMatrix(); o->type = ObjType::OBJ_RACI_MATRIX; o->marked = false; o->next = nullptr; return o; }
ObjTaskUnderstanding* new_task_understanding() { auto* o = new ObjTaskUnderstanding(); o->type = ObjType::OBJ_TASK_UNDERSTANDING; o->marked = false; o->next = nullptr; return o; }
ObjTaskDecomposer* new_task_decomposer_dio() { auto* o = new ObjTaskDecomposer(); o->type = ObjType::OBJ_TASK_DECOMPOSER; o->marked = false; o->next = nullptr; return o; }
ObjCrewFormation* new_crew_formation() { auto* o = new ObjCrewFormation(); o->type = ObjType::OBJ_CREW_FORMATION; o->marked = false; o->next = nullptr; return o; }
ObjPatternSelector* new_pattern_selector() { auto* o = new ObjPatternSelector(); o->type = ObjType::OBJ_PATTERN_SELECTOR; o->marked = false; o->next = nullptr; return o; }
ObjExecutionManagerDIO* new_execution_manager_dio() { auto* o = new ObjExecutionManagerDIO(); o->type = ObjType::OBJ_EXECUTION_MANAGER_DIO; o->marked = false; o->next = nullptr; return o; }
ObjDIOStateMachine* new_dio_state_machine() { auto* o = new ObjDIOStateMachine(); o->type = ObjType::OBJ_DIO_STATE_MACHINE; o->marked = false; o->next = nullptr; return o; }
ObjDIOErrorHandling* new_dio_error_handling() { auto* o = new ObjDIOErrorHandling(); o->type = ObjType::OBJ_DIO_ERROR_HANDLING; o->marked = false; o->next = nullptr; return o; }
ObjResultSynthesizer* new_result_synthesizer() { auto* o = new ObjResultSynthesizer(); o->type = ObjType::OBJ_RESULT_SYNTHESIZER; o->marked = false; o->next = nullptr; return o; }
ObjInfrastructureProfile* new_infrastructure_profile() { auto* o = new ObjInfrastructureProfile(); o->type = ObjType::OBJ_INFRASTRUCTURE_PROFILE; o->marked = false; o->next = nullptr; return o; }
ObjRoleFramework* new_role_framework() { auto* o = new ObjRoleFramework(); o->type = ObjType::OBJ_ROLE_FRAMEWORK; o->marked = false; o->next = nullptr; return o; }
ObjDelegationProtocol* new_delegation_protocol() { auto* o = new ObjDelegationProtocol(); o->type = ObjType::OBJ_DELEGATION_PROTOCOL; o->marked = false; o->next = nullptr; return o; }
ObjDIOAccountability* new_dio_accountability() { auto* o = new ObjDIOAccountability(); o->type = ObjType::OBJ_DIO_ACCOUNTABILITY; o->marked = false; o->next = nullptr; return o; }
ObjDIOAgent* new_dio_agent() { auto* o = new ObjDIOAgent(); o->type = ObjType::OBJ_DIO_AGENT; o->marked = false; o->next = nullptr; o->status = "initialized"; return o; }
}  // namespace neamc::vm
