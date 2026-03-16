//
// Neam v0.9.4 — Governance Agent runtime types
//

#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <unordered_map>
#include <vector>

#include <nlohmann/json.hpp>

#include "neamc/vm/data_agent_types.hpp"
#include "neamc/vm/object.hpp"

namespace neamc::vm
{

// Governance lifecycle phase
enum class GovernancePhase
{
  IDLE,
  SCANNING,
  CLASSIFYING,
  AUDITING,
  REMEDIATING,
  REPORTING
};

// --- Runtime objects for each governance declaration ---

struct ObjGovCatalogSource : public Obj
{
  ObjGovCatalogSource() { type = ObjType::OBJ_GOV_CATALOG_SOURCE; }
  ObjString* name = nullptr;
  std::string source_type;
  std::string connection;
  std::string credentials;
  std::vector<std::string> databases;
  std::string scan_interval;
  std::string sync_mode;
  std::string conflict_resolution;
  bool include_views = true;
  bool connected = false;
};

struct ObjGovCatalog : public Obj
{
  ObjGovCatalog() { type = ObjType::OBJ_GOV_CATALOG; }
  ObjString* name = nullptr;
  std::vector<std::string> source_refs;
  std::string auto_document_json;
  std::string staleness_threshold;
  bool shadow_dataset_detection = false;
  std::string ownership_json;
  // Runtime state
  int total_assets = 0;
  int documented_assets = 0;
};

struct ObjGlossary : public Obj
{
  ObjGlossary() { type = ObjType::OBJ_GLOSSARY; }
  ObjString* name = nullptr;
  std::vector<std::string> domains;
  std::string auto_suggest_json;
  std::string synonym_detection_json;
  std::string terms_json;
  int total_terms = 0;
};

struct ObjClassificationPolicy : public Obj
{
  ObjClassificationPolicy() { type = ObjType::OBJ_CLASSIFICATION_POLICY; }
  ObjString* name = nullptr;
  std::string levels_json;
  std::string auto_classify_json;
  std::string propagation_json;
  // Runtime state
  int classified_assets = 0;
  int unclassified_assets = 0;
};

struct ObjAccessPolicy : public Obj
{
  ObjAccessPolicy() { type = ObjType::OBJ_ACCESS_POLICY; }
  ObjString* name = nullptr;
  std::string access_model;
  std::string roles_json;
  std::string attributes_json;
  std::string access_review_json;
  int total_roles = 0;
  int active_reviews = 0;
};

struct ObjQualityPolicy : public Obj
{
  ObjQualityPolicy() { type = ObjType::OBJ_QUALITY_POLICY; }
  ObjString* name = nullptr;
  std::string profiling_json;
  std::string rules_json;
  std::string scoring_json;
  double overall_score = 0.0;
  int violations = 0;
};

struct ObjLineagePolicy : public Obj
{
  ObjLineagePolicy() { type = ObjType::OBJ_LINEAGE_POLICY; }
  ObjString* name = nullptr;
  std::string auto_discover_json;
  std::string impact_analysis_json;
  std::string tag_propagation_json;
  int tracked_assets = 0;
  int lineage_edges = 0;
};

struct ObjCompliancePolicy : public Obj
{
  ObjCompliancePolicy() { type = ObjType::OBJ_COMPLIANCE_POLICY; }
  ObjString* name = nullptr;
  std::vector<std::string> regulations;
  std::string gdpr_json;
  std::string ccpa_json;
  std::string hipaa_json;
  std::string monitoring_json;
  double compliance_score = 0.0;
  int violations = 0;
};

struct ObjLifecyclePolicy : public Obj
{
  ObjLifecyclePolicy() { type = ObjType::OBJ_LIFECYCLE_POLICY; }
  ObjString* name = nullptr;
  std::string retention_json;
  std::string tiering_json;
  std::string legal_hold_json;
  int expired_datasets = 0;
  int held_datasets = 0;
};

struct ObjDataProduct : public Obj
{
  ObjDataProduct() { type = ObjType::OBJ_DATA_PRODUCT; }
  ObjString* name = nullptr;
  std::string domain;
  std::string owner;
  std::string description;
  std::string contract_json;
  std::string quality_json;
  std::string access_json;
  double quality_score = 0.0;
  bool sla_met = true;
};

struct ObjContractPolicy : public Obj
{
  ObjContractPolicy() { type = ObjType::OBJ_CONTRACT_POLICY; }
  ObjString* name = nullptr;
  bool schema_validation_on_deploy = true;
  bool breaking_change_detection = true;
  bool notify_consumers = true;
  std::string versioning_strategy;
  int active_contracts = 0;
  int broken_contracts = 0;
};

struct ObjMasterData : public Obj
{
  ObjMasterData() { type = ObjType::OBJ_MASTER_DATA; }
  ObjString* name = nullptr;
  std::string entity;
  std::string golden_source;
  std::vector<std::string> contributing_sources;
  std::string matching_json;
  std::string survivorship_json;
  int golden_records = 0;
  int duplicates_found = 0;
};

struct ObjGovExternalTool : public Obj
{
  ObjGovExternalTool() { type = ObjType::OBJ_GOV_EXTERNAL_TOOL; }
  ObjString* name = nullptr;
  std::string tool_type;
  std::string connection;
  std::string credentials;
  std::string capabilities_json;
  std::string sync_interval;
  std::string conflict_resolution;
  bool connected = false;
  bool syncing = false;
};

struct ObjGovernanceAgent : public ObjDataAgent
{
  ObjGovernanceAgent() { type = ObjType::OBJ_GOVERNANCE_AGENT; }

  // Pillar references
  std::string catalog_ref;
  std::string glossary_ref;
  std::string classification_ref;
  std::string access_control_ref;
  std::string quality_ref;
  std::string lineage_ref;
  std::string compliance_ref;
  std::string lifecycle_ref;

  // External tools & coordination
  std::vector<std::string> external_tool_refs;
  std::vector<std::string> coordinates_with_refs;
  std::vector<std::string> skill_refs;
  std::vector<std::string> guardchain_refs;

  // Reports
  std::string reports_json;

  // Common agent fields
  std::string budget_ref;
  std::string policy_ref;
  std::string agent_md_path;

  // Runtime state
  GovernancePhase phase = GovernancePhase::IDLE;
  double governance_score = 0.0;
  int total_violations = 0;
  mutable std::mutex state_mutex;
};

}  // namespace neamc::vm
