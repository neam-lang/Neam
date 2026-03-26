//
// Neam v0.9.2 — Phase 8: Security & Governance Migrator implementation
//

#include "security_migrator.hpp"
#include "audit_trail.hpp"

#include <algorithm>

namespace neamc::vm::migration {

// ═══════════════════════════════════════════════════════════════════════
// PIIDetector patterns
// ═══════════════════════════════════════════════════════════════════════

const std::vector<std::pair<std::string, std::string>> PIIDetector::pii_patterns_ = {
    {"ssn",             "SSN"},       {"social_security", "SSN"},
    {"email",           "EMAIL"},     {"phone",           "PHONE"},
    {"mobile",          "PHONE"},     {"first_name",      "NAME"},
    {"last_name",       "NAME"},      {"address",         "ADDRESS"},
    {"street",          "ADDRESS"},   {"zip",             "ADDRESS"},
    {"postal",          "ADDRESS"},   {"birth",           "DOB"},
    {"dob",             "DOB"},       {"credit_card",     "PCI"},
    {"card_number",     "PCI"},       {"account_num",     "FINANCIAL"},
    {"routing",         "FINANCIAL"}, {"passport",        "ID_DOCUMENT"},
    {"driver_lic",      "ID_DOCUMENT"},
    {"ip_address",      "NETWORK"},   {"mac_address",     "NETWORK"},
};

// ═══════════════════════════════════════════════════════════════════════
// SecurityMigrator
// ═══════════════════════════════════════════════════════════════════════

SecurityMigrator::SecurityMigrator(ObjMigrationAgent* agent) : agent_(agent) {}

std::vector<SourcePermission> SecurityMigrator::discover_permissions() {
  // In production: query source database's permission catalog
  // Oracle: SELECT * FROM DBA_TAB_PRIVS / DBA_COL_PRIVS
  // SQL Server: SELECT * FROM sys.database_permissions
  audit_log(agent_, "security_discover_permissions", agent_name_str(agent_),
            "Discovering source permissions", {});
  return {};
}

std::vector<RoleMembership> SecurityMigrator::discover_roles() {
  // Oracle: DBA_ROLES, DBA_ROLE_PRIVS
  // SQL Server: sys.database_role_members
  audit_log(agent_, "security_discover_roles", agent_name_str(agent_),
            "Discovering source roles", {});
  return {};
}

std::vector<RLSPolicy> SecurityMigrator::discover_rls_policies() {
  // Oracle: DBA_POLICIES (VPD)
  // SQL Server: sys.security_policies
  audit_log(agent_, "security_discover_rls", agent_name_str(agent_),
            "Discovering RLS policies", {});
  return {};
}

std::vector<MaskingPolicy> SecurityMigrator::discover_masking_policies() {
  // Oracle: DBMS_REDACT policies
  // SQL Server: sys.masked_columns
  audit_log(agent_, "security_discover_masking", agent_name_str(agent_),
            "Discovering masking policies", {});
  return {};
}

std::vector<DataClassification> SecurityMigrator::discover_classifications() {
  // Combine catalog-based and PII-detection based classification
  std::vector<DataClassification> classifications;

  // Auto-detect PII if governance.pii_detection is enabled
  if (agent_->governance_obj && agent_->governance_obj->pii_detection) {
    PIIDetector detector;
    if (agent_->schema_map_obj) {
      classifications = detector.detect(agent_->schema_map_obj);
    }
  }

  audit_log(agent_, "security_discover_classifications", agent_name_str(agent_),
            "Discovered " + std::to_string(classifications.size()) + " classifications", {});
  return classifications;
}

std::string SecurityMigrator::translate_role(const RoleMembership& role) {
  std::string source = agent_->schema_translation_config.value("source_platform", "oracle");
  std::string target = agent_->schema_translation_config.value("target_platform", "snowflake");

  if (source == "oracle" && target == "snowflake")
    return translate_oracle_role_to_snowflake(role);
  if (source == "teradata" && target == "bigquery")
    return translate_teradata_role_to_bigquery(role);

  // Default: CREATE ROLE with GRANT
  std::string sql = "CREATE ROLE IF NOT EXISTS " + role.role_name + ";\n";
  for (const auto& member : role.members) {
    sql += "GRANT ROLE " + role.role_name + " TO USER " + member + ";\n";
  }
  return sql;
}

std::string SecurityMigrator::translate_permission(const SourcePermission& perm) {
  std::string sql = "GRANT " + perm.privilege + " ON " + perm.object_name
                  + " TO " + perm.grantee;
  if (perm.with_grant_option) sql += " WITH GRANT OPTION";
  sql += ";";
  return sql;
}

std::string SecurityMigrator::translate_rls_policy(const RLSPolicy& policy) {
  std::string target = agent_->schema_translation_config.value("target_platform", "snowflake");

  if (target == "snowflake") {
    // Snowflake: Row Access Policy
    return "CREATE OR REPLACE ROW ACCESS POLICY " + policy.policy_name + "\n"
           "  AS (" + policy.table_name + " VARCHAR) RETURNS BOOLEAN ->\n"
           "  " + policy.predicate + ";\n"
           "ALTER TABLE " + policy.table_name
           + " ADD ROW ACCESS POLICY " + policy.policy_name + ";";
  }
  if (target == "bigquery") {
    // BigQuery: Row-level security via authorized views
    return "-- BigQuery RLS: create authorized view with WHERE " + policy.predicate;
  }
  return "-- RLS policy: " + policy.policy_name + " (manual translation required)";
}

std::string SecurityMigrator::translate_masking_policy(const MaskingPolicy& policy) {
  std::string target = agent_->schema_translation_config.value("target_platform", "snowflake");

  if (target == "snowflake") {
    return "CREATE OR REPLACE MASKING POLICY " + policy.policy_name + "\n"
           "  AS (val STRING) RETURNS STRING ->\n"
           "  CASE WHEN CURRENT_ROLE() IN ('" + policy.grantee + "') THEN val\n"
           "       ELSE " + policy.masking_function + "\n"
           "  END;\n"
           "ALTER TABLE " + policy.table_name + " ALTER COLUMN " + policy.column_name
           + " SET MASKING POLICY " + policy.policy_name + ";";
  }
  return "-- Masking policy: " + policy.policy_name + " (manual translation required)";
}

bool SecurityMigrator::apply_roles() {
  auto roles = discover_roles();
  for (const auto& role : roles) {
    std::string sql = translate_role(role);
    audit_log(agent_, "security_apply_role", role.role_name, sql, {});
  }
  return true;
}

bool SecurityMigrator::apply_permissions() {
  auto perms = discover_permissions();
  for (const auto& perm : perms) {
    std::string sql = translate_permission(perm);
    audit_log(agent_, "security_apply_permission", perm.object_name, sql, {});
  }
  return true;
}

bool SecurityMigrator::apply_rls_policies() {
  auto policies = discover_rls_policies();
  for (const auto& policy : policies) {
    std::string sql = translate_rls_policy(policy);
    audit_log(agent_, "security_apply_rls", policy.table_name, sql, {});
  }
  return true;
}

bool SecurityMigrator::apply_masking_policies() {
  auto policies = discover_masking_policies();
  for (const auto& policy : policies) {
    std::string sql = translate_masking_policy(policy);
    audit_log(agent_, "security_apply_masking", policy.column_name, sql, {});
  }
  return true;
}

bool SecurityMigrator::apply_classifications() {
  auto classifications = discover_classifications();
  for (const auto& cls : classifications) {
    audit_log(agent_, "security_apply_classification", cls.table_name + "." + cls.column_name,
              cls.classification + "/" + cls.sub_classification,
              {{"handling_rule", cls.handling_rule}});
  }
  return true;
}

bool SecurityMigrator::validate_permission_matrix() {
  audit_log(agent_, "security_validate", agent_name_str(agent_),
            "Validating permission matrix", {});
  // In production: compare source vs target permission catalogs
  return true;
}

bool SecurityMigrator::migrate_all() {
  audit_log(agent_, "security_migration_start", agent_name_str(agent_),
            "Starting full security migration", {});

  bool ok = true;
  ok = apply_roles() && ok;
  ok = apply_permissions() && ok;
  ok = apply_rls_policies() && ok;
  ok = apply_masking_policies() && ok;
  ok = apply_classifications() && ok;
  ok = validate_permission_matrix() && ok;

  audit_log(agent_, "security_migration_complete", agent_name_str(agent_),
            ok ? "Security migration completed" : "Security migration had failures",
            {{"success", ok}});
  return ok;
}

// ─── Platform-specific role translators ─────────────────────────────

std::string SecurityMigrator::translate_oracle_role_to_snowflake(const RoleMembership& role) {
  std::string sql = "CREATE ROLE IF NOT EXISTS " + role.role_name + ";\n";
  for (const auto& priv : role.privileges) {
    sql += "GRANT " + priv + " TO ROLE " + role.role_name + ";\n";
  }
  for (const auto& member : role.members) {
    sql += "GRANT ROLE " + role.role_name + " TO USER " + member + ";\n";
  }
  return sql;
}

std::string SecurityMigrator::translate_oracle_vpd_to_snowflake_rap(const RLSPolicy& policy) {
  return translate_rls_policy(policy);
}

std::string SecurityMigrator::translate_teradata_role_to_bigquery(const RoleMembership& role) {
  // BigQuery uses IAM roles, not database roles
  std::string sql = "-- BigQuery: map role '" + role.role_name + "' to IAM role\n";
  for (const auto& member : role.members) {
    sql += "-- GRANT roles/bigquery.dataViewer TO user:" + member + "\n";
  }
  return sql;
}

// ═══════════════════════════════════════════════════════════════════════
// PIIDetector
// ═══════════════════════════════════════════════════════════════════════

std::vector<DataClassification> PIIDetector::detect(const ObjSchemaMap* schema_map) {
  std::vector<DataClassification> results;

  for (const auto& [table_name, mapping] : schema_map->table_mappings) {
    for (const auto& col : mapping.columns) {
      if (is_pii_column_name(col.source_name)) {
        DataClassification cls;
        cls.table_name = table_name;
        cls.column_name = col.source_name;
        cls.classification = "PII";
        cls.sub_classification = classify_pii_type(col.source_name);
        cls.handling_rule = "MASK";
        results.push_back(cls);
      }
    }
  }

  return results;
}

bool PIIDetector::is_pii_column_name(const std::string& col_name) {
  std::string lower = col_name;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
  for (const auto& [pattern, _] : pii_patterns_) {
    if (lower.find(pattern) != std::string::npos) return true;
  }
  return false;
}

std::string PIIDetector::classify_pii_type(const std::string& col_name) {
  std::string lower = col_name;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
  for (const auto& [pattern, classification] : pii_patterns_) {
    if (lower.find(pattern) != std::string::npos) return classification;
  }
  return "UNKNOWN";
}

}  // namespace neamc::vm::migration
