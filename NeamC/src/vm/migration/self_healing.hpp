//
// Neam v0.9.2 — Phase 6: Self-Healing & Remediation
//

#pragma once

#include <optional>
#include <string>
#include <vector>

#include "neamc/vm/migration_types.hpp"

namespace neamc::vm::migration {

enum class FailureCategory {
  MISSING_ROWS,
  DUPLICATE_ROWS,
  TYPE_CONVERSION_ERROR,
  NULL_CONSTRAINT_VIOLATION,
  UNIQUE_CONSTRAINT_VIOLATION,
  FK_CONSTRAINT_VIOLATION,
  NETWORK_FAILURE,
  TIMEOUT,
  DISK_SPACE,
  PERMISSION_DENIED,
  RATE_LIMITED,
  COLUMN_OVERFLOW,
  PRECISION_LOSS,
  ENCODING_ERROR,
  UNKNOWN
};

struct FailureDiagnosis {
  FailureCategory category;
  std::string object_name;
  std::string column_name;
  std::string error_message;
  std::string root_cause;
  bool auto_remediable;
  std::string proposed_fix;
  int estimated_affected_rows;
  double estimated_fix_time_seconds;
};

class SelfHealingEngine {
public:
  explicit SelfHealingEngine(ObjMigrationAgent* agent);

  FailureDiagnosis diagnose(const std::string& object_name,
                            const std::string& error_message);
  bool remediate(const FailureDiagnosis& diagnosis);
  bool within_guardrails(const FailureDiagnosis& diagnosis);
  void escalate(const FailureDiagnosis& diagnosis);

  int total_auto_fixes() const { return total_auto_fixes_; }
  int total_auto_fix_rows() const { return total_auto_fix_rows_; }

private:
  ObjMigrationAgent* agent_;
  int total_auto_fixes_ = 0;
  int total_auto_fix_rows_ = 0;

  bool remediate_missing_rows(const FailureDiagnosis& diagnosis);
  bool remediate_duplicate_rows(const FailureDiagnosis& diagnosis);
  bool remediate_type_conversion(const FailureDiagnosis& diagnosis);
  bool remediate_column_overflow(const FailureDiagnosis& diagnosis);
  bool remediate_network_failure(const FailureDiagnosis& diagnosis);
  bool remediate_checkpoint_resume(const FailureDiagnosis& diagnosis);

  bool check_max_auto_fix_rows(int affected_rows);
  bool check_max_auto_fix_percentage(int affected_rows, int total_rows);
  bool check_max_retries(const std::string& object_name);

  std::string extract_column_from_error(const std::string& error);
  std::string generate_alter_column_fix(const std::string& table, const std::string& col);
  std::string failure_category_to_string(FailureCategory cat);
  std::string to_lower(const std::string& s);
};

}  // namespace neamc::vm::migration
