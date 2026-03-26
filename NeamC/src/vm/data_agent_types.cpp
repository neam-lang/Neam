//
// Neam v0.9 — Data Agent runtime type factory functions
//

#include "neamc/vm/data_agent_types.hpp"

namespace neamc::vm
{

ObjDataAgent* new_data_agent()
{
  auto* obj = new ObjDataAgent();
  obj->type = ObjType::OBJ_DATA_AGENT;
  return obj;
}

ObjSource* new_source()
{
  auto* obj = new ObjSource();
  obj->type = ObjType::OBJ_SOURCE;
  return obj;
}

ObjSink* new_sink()
{
  auto* obj = new ObjSink();
  obj->type = ObjType::OBJ_SINK;
  return obj;
}

ObjSchema* new_schema_obj()
{
  auto* obj = new ObjSchema();
  obj->type = ObjType::OBJ_SCHEMA;
  return obj;
}

ObjQualityGate* new_quality_gate()
{
  auto* obj = new ObjQualityGate();
  obj->type = ObjType::OBJ_QUALITY_GATE;
  return obj;
}

ObjComputeEngine* new_compute_engine()
{
  auto* obj = new ObjComputeEngine();
  obj->type = ObjType::OBJ_COMPUTE_ENGINE;
  return obj;
}

ObjGovernancePolicy* new_governance_policy()
{
  auto* obj = new ObjGovernancePolicy();
  obj->type = ObjType::OBJ_GOVERNANCE_POLICY;
  return obj;
}

ObjCatalog* new_catalog_obj()
{
  auto* obj = new ObjCatalog();
  obj->type = ObjType::OBJ_CATALOG;
  return obj;
}

}  // namespace neamc::vm
