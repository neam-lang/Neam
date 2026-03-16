//
// Neam v0.9.3 — DataOps Agent runtime type factory functions
//

#include "neamc/vm/dataops_types.hpp"

namespace neamc::vm
{

ObjDataOpsAgent* new_dataops_agent()
{
  auto* obj = new ObjDataOpsAgent();
  obj->type = ObjType::OBJ_DATAOPS_AGENT;
  return obj;
}

ObjScheduler* new_scheduler_obj()
{
  auto* obj = new ObjScheduler();
  obj->type = ObjType::OBJ_SCHEDULER;
  return obj;
}

ObjAuditTable* new_audit_table_obj()
{
  auto* obj = new ObjAuditTable();
  obj->type = ObjType::OBJ_AUDIT_TABLE;
  return obj;
}

ObjLogSource* new_log_source_obj()
{
  auto* obj = new ObjLogSource();
  obj->type = ObjType::OBJ_LOG_SOURCE;
  return obj;
}

ObjPlatformMonitor* new_platform_monitor()
{
  auto* obj = new ObjPlatformMonitor();
  obj->type = ObjType::OBJ_PLATFORM_MONITOR;
  return obj;
}

ObjIncidentPolicy* new_incident_policy()
{
  auto* obj = new ObjIncidentPolicy();
  obj->type = ObjType::OBJ_INCIDENT_POLICY;
  return obj;
}

ObjCorrelation* new_correlation_obj()
{
  auto* obj = new ObjCorrelation();
  obj->type = ObjType::OBJ_CORRELATION;
  return obj;
}

ObjIncident* new_incident_obj()
{
  auto* obj = new ObjIncident();
  obj->type = ObjType::OBJ_INCIDENT;
  return obj;
}

}  // namespace neamc::vm
