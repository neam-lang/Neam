//
// Neam v0.9.2 — Migration Agent runtime type factory functions
//

#include "neamc/vm/migration_types.hpp"

namespace neamc::vm
{

ObjMigrationAgent* new_migration_agent()
{
  auto* obj = new ObjMigrationAgent();
  obj->type = ObjType::OBJ_MIGRATION_AGENT;
  return obj;
}

ObjGovernanceMig* new_governance_mig()
{
  auto* obj = new ObjGovernanceMig();
  obj->type = ObjType::OBJ_GOVERNANCE;
  return obj;
}

ObjWavePlan* new_wave_plan()
{
  auto* obj = new ObjWavePlan();
  obj->type = ObjType::OBJ_WAVE_PLAN;
  return obj;
}

ObjMigrationObject* new_migration_object()
{
  auto* obj = new ObjMigrationObject();
  obj->type = ObjType::OBJ_MIGRATION_OBJECT;
  return obj;
}

ObjReconciliationResult* new_reconciliation_result()
{
  auto* obj = new ObjReconciliationResult();
  obj->type = ObjType::OBJ_RECONCILIATION_RESULT;
  return obj;
}

ObjSchemaMap* new_schema_map()
{
  auto* obj = new ObjSchemaMap();
  obj->type = ObjType::OBJ_SCHEMA_MAP;
  return obj;
}

ObjMigrationCheckpoint* new_migration_checkpoint()
{
  auto* obj = new ObjMigrationCheckpoint();
  obj->type = ObjType::OBJ_MIGRATION_CHECKPOINT;
  return obj;
}

}  // namespace neamc::vm
