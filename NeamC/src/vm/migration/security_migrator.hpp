//
// Neam v0.9.2 — Phase 8: Security & Governance Migrator
//

#pragma once

#include <string>
#include <utility>
#include <vector>

#include "neamc/vm/migration_types.hpp"

namespace neamc::vm::migration {

struct SourcePermission {
  std::string grantee;
  std::string object_name;
  std::string privilege;
  bool with_grant_option = false;
  std::string column_restrictions;
};

struct RoleMembership {
  std::string role_name;
  std::vector<std::string> members;
  std::vector<std::string> privileges;
};

struct RLSPolicy {
  std::string policy_name;
  std::string table_name;
  std::string predicate;
  std::string grantee;
  std::string source_platform;
  std::string target_translation;
};

struct MaskingPolicy {
  std::string policy_name;
  std::string table_name;
  std::string column_name;
  std::string masking_function;
  std::string grantee;
  std::string source_platform;
  std::string target_translation;
};

struct DataClassification {
  std::string table_name;
  std::string column_name;
  std::string classification;    // PII, PHI, PCI, CONFIDENTIAL, PUBLIC
  std::string sub_classification; // SSN, EMAIL, PHONE, etc.
  std::string handling_rule;     // MASK, ENCRYPT, TOKENIZE
};

class SecurityMigrator {
public:
  explicit SecurityMigrator(ObjMigrationAgent* agent);

  // Discovery
  std::vector<SourcePermission> discover_permissions();
  std::vector<RoleMembership> discover_roles();
  std::vector<RLSPolicy> discover_rls_policies();
  std::vector<MaskingPolicy> discover_masking_policies();
  std::vector<DataClassification> discover_classifications();

  // Translation
  std::string translate_role(const RoleMembership& role);
  std::string translate_permission(const SourcePermission& perm);
  std::string translate_rls_policy(const RLSPolicy& policy);
  std::string translate_masking_policy(const MaskingPolicy& policy);

  // Application
  bool apply_roles();
  bool apply_permissions();
  bool apply_rls_policies();
  bool apply_masking_policies();
  bool apply_classifications();

  // Validation
  bool validate_permission_matrix();

  // Full security migration
  bool migrate_all();

private:
  ObjMigrationAgent* agent_;

  std::string translate_oracle_role_to_snowflake(const RoleMembership& role);
  std::string translate_oracle_vpd_to_snowflake_rap(const RLSPolicy& policy);
  std::string translate_teradata_role_to_bigquery(const RoleMembership& role);
};

class PIIDetector {
public:
  std::vector<DataClassification> detect(const ObjSchemaMap* schema_map);

private:
  bool is_pii_column_name(const std::string& col_name);
  std::string classify_pii_type(const std::string& col_name);

  static const std::vector<std::pair<std::string, std::string>> pii_patterns_;
};

}  // namespace neamc::vm::migration
