//
// Neam v0.9.1 — ETL Agent runtime type factory functions
//

#include "neamc/vm/etl_agent_types.hpp"

namespace neamc::vm
{

ObjETLAgent* new_etl_agent()
{
  auto* obj = new ObjETLAgent();
  obj->type = ObjType::OBJ_ETL_AGENT;
  return obj;
}

ObjMart* new_mart()
{
  auto* obj = new ObjMart();
  obj->type = ObjType::OBJ_MART;
  return obj;
}

ObjSemanticLayer* new_semantic_layer()
{
  auto* obj = new ObjSemanticLayer();
  obj->type = ObjType::OBJ_SEMANTIC_LAYER;
  return obj;
}

ObjSQLPlan* new_sql_plan()
{
  auto* obj = new ObjSQLPlan();
  obj->type = ObjType::OBJ_SQL_PLAN;
  return obj;
}

ObjModelProposal* new_model_proposal()
{
  auto* obj = new ObjModelProposal();
  obj->type = ObjType::OBJ_MODEL_PROPOSAL;
  return obj;
}

}  // namespace neamc::vm
