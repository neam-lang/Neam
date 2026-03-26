//
// Neam v0.9.4 — Governance Agent runtime type factory functions
//

#include "neamc/vm/governance_types.hpp"

namespace neamc::vm
{

ObjGovernanceAgent* new_governance_agent()
{
  auto* obj = new ObjGovernanceAgent();
  obj->type = ObjType::OBJ_GOVERNANCE_AGENT;
  return obj;
}

ObjGovCatalogSource* new_gov_catalog_source()
{
  auto* obj = new ObjGovCatalogSource();
  obj->type = ObjType::OBJ_GOV_CATALOG_SOURCE;
  return obj;
}

ObjGovCatalog* new_gov_catalog()
{
  auto* obj = new ObjGovCatalog();
  obj->type = ObjType::OBJ_GOV_CATALOG;
  return obj;
}

ObjGlossary* new_glossary_obj()
{
  auto* obj = new ObjGlossary();
  obj->type = ObjType::OBJ_GLOSSARY;
  return obj;
}

ObjClassificationPolicy* new_classification_policy()
{
  auto* obj = new ObjClassificationPolicy();
  obj->type = ObjType::OBJ_CLASSIFICATION_POLICY;
  return obj;
}

ObjAccessPolicy* new_access_policy()
{
  auto* obj = new ObjAccessPolicy();
  obj->type = ObjType::OBJ_ACCESS_POLICY;
  return obj;
}

ObjQualityPolicy* new_quality_policy()
{
  auto* obj = new ObjQualityPolicy();
  obj->type = ObjType::OBJ_QUALITY_POLICY;
  return obj;
}

ObjLineagePolicy* new_lineage_policy()
{
  auto* obj = new ObjLineagePolicy();
  obj->type = ObjType::OBJ_LINEAGE_POLICY;
  return obj;
}

ObjCompliancePolicy* new_compliance_policy()
{
  auto* obj = new ObjCompliancePolicy();
  obj->type = ObjType::OBJ_COMPLIANCE_POLICY;
  return obj;
}

ObjLifecyclePolicy* new_lifecycle_policy()
{
  auto* obj = new ObjLifecyclePolicy();
  obj->type = ObjType::OBJ_LIFECYCLE_POLICY;
  return obj;
}

ObjDataProduct* new_data_product()
{
  auto* obj = new ObjDataProduct();
  obj->type = ObjType::OBJ_DATA_PRODUCT;
  return obj;
}

ObjContractPolicy* new_contract_policy()
{
  auto* obj = new ObjContractPolicy();
  obj->type = ObjType::OBJ_CONTRACT_POLICY;
  return obj;
}

ObjMasterData* new_master_data_obj()
{
  auto* obj = new ObjMasterData();
  obj->type = ObjType::OBJ_MASTER_DATA;
  return obj;
}

ObjGovExternalTool* new_gov_external_tool()
{
  auto* obj = new ObjGovExternalTool();
  obj->type = ObjType::OBJ_GOV_EXTERNAL_TOOL;
  return obj;
}

}  // namespace neamc::vm
