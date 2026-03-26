//
// Neam v0.9.6 — Analyst Agent runtime type factories
//

#include "neamc/vm/analyst_types.hpp"

namespace neamc::vm
{

ObjSQLConnection* new_sql_connection()
{
  auto* obj = new ObjSQLConnection();
  obj->type = ObjType::OBJ_SQL_CONNECTION;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjDomainContext* new_domain_context()
{
  auto* obj = new ObjDomainContext();
  obj->type = ObjType::OBJ_DOMAIN_CONTEXT;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjQueryTemplate* new_query_template()
{
  auto* obj = new ObjQueryTemplate();
  obj->type = ObjType::OBJ_QUERY_TEMPLATE;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjQueryOptimizer* new_query_optimizer()
{
  auto* obj = new ObjQueryOptimizer();
  obj->type = ObjType::OBJ_QUERY_OPTIMIZER;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjExecutionPolicy* new_execution_policy()
{
  auto* obj = new ObjExecutionPolicy();
  obj->type = ObjType::OBJ_EXECUTION_POLICY;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjOutputFormat* new_output_format()
{
  auto* obj = new ObjOutputFormat();
  obj->type = ObjType::OBJ_OUTPUT_FORMAT;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjQueryLibrary* new_query_library()
{
  auto* obj = new ObjQueryLibrary();
  obj->type = ObjType::OBJ_QUERY_LIBRARY;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjAnalysisSchedule* new_analysis_schedule()
{
  auto* obj = new ObjAnalysisSchedule();
  obj->type = ObjType::OBJ_ANALYSIS_SCHEDULE;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjAnalystAgent* new_analyst_agent()
{
  auto* obj = new ObjAnalystAgent();
  obj->type = ObjType::OBJ_ANALYST_AGENT;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

}  // namespace neamc::vm
