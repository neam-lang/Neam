//
// Neam v0.9.5 — Modeling Agent runtime type factories
//

#include "neamc/vm/modeling_types.hpp"

namespace neamc::vm
{

ObjSchemaSource* new_schema_source()
{
  auto* obj = new ObjSchemaSource();
  obj->type = ObjType::OBJ_SCHEMA_SOURCE;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjERModel* new_er_model()
{
  auto* obj = new ObjERModel();
  obj->type = ObjType::OBJ_ER_MODEL;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjEntity* new_entity_obj()
{
  auto* obj = new ObjEntity();
  obj->type = ObjType::OBJ_ENTITY;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjDimensionalModel* new_dimensional_model()
{
  auto* obj = new ObjDimensionalModel();
  obj->type = ObjType::OBJ_DIMENSIONAL_MODEL;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjDataMartV095* new_datamart_v095()
{
  auto* obj = new ObjDataMartV095();
  obj->type = ObjType::OBJ_DATAMART_V095;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjNormAnalysis* new_norm_analysis()
{
  auto* obj = new ObjNormAnalysis();
  obj->type = ObjType::OBJ_NORM_ANALYSIS;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjAmendmentConfig* new_amendment_config()
{
  auto* obj = new ObjAmendmentConfig();
  obj->type = ObjType::OBJ_AMENDMENT_CONFIG;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjAmendment* new_amendment_obj()
{
  auto* obj = new ObjAmendment();
  obj->type = ObjType::OBJ_AMENDMENT;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjDataProfile* new_data_profile()
{
  auto* obj = new ObjDataProfile();
  obj->type = ObjType::OBJ_DATA_PROFILE;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjModelingTool* new_modeling_tool()
{
  auto* obj = new ObjModelingTool();
  obj->type = ObjType::OBJ_MODELING_TOOL;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

ObjModelingAgent* new_modeling_agent()
{
  auto* obj = new ObjModelingAgent();
  obj->type = ObjType::OBJ_MODELING_AGENT;
  obj->marked = false;
  obj->next = nullptr;
  return obj;
}

}  // namespace neamc::vm
