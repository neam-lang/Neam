//
// Neam v0.9.2 — Phase 6: Self-Healing & Remediation implementation
//

#include "self_healing.hpp"
#include "audit_trail.hpp"

#include <algorithm>
#include <cmath>

namespace neamc::vm::migration {

SelfHealingEngine::SelfHealingEngine(ObjMigrationAgent* agent) : agent_(agent) {}

FailureDiagnosis SelfHealingEngine::diagnose(
    const std::string& object_name, const std::string& error_message) {
  FailureDiagnosis diag;
  diag.object_name = object_name;
  diag.error_message = error_message;
  diag.auto_remediable = false;
  diag.estimated_affected_rows = 0;
  diag.estimated_fix_time_seconds = 0.0;

  std::string lower_err = to_lower(error_message);

  // Data type / overflow errors
  if (lower_err.find("numeric value out of range") != std::string::npos ||
      lower_err.find("value too long") != std::string::npos ||
      lower_err.find("string too long") != std::string::npos ||
      lower_err.find("ora-01438") != std::string::npos) {
    diag.category = FailureCategory::COLUMN_OVERFLOW;
    diag.column_name = extract_column_from_error(error_message);
    diag.root_cause = "Source data exceeds target column size for '" + diag.column_name + "'.";
    diag.auto_remediable = agent_->self_heal_config.value("enabled", false);
    diag.proposed_fix = generate_alter_column_fix(object_name, diag.column_name);
    return diag;
  }

  // NULL constraint violations
  if (lower_err.find("null value") != std::string::npos ||
      lower_err.find("not null constraint") != std::string::npos ||
      lower_err.find("cannot insert null") != std::string::npos) {
    diag.category = FailureCategory::NULL_CONSTRAINT_VIOLATION;
    diag.column_name = extract_column_from_error(error_message);
    diag.root_cause = "Source NULL violates target NOT NULL on '" + diag.column_name + "'.";
    diag.auto_remediable = false;
    diag.proposed_fix = "ALTER TABLE ... ALTER COLUMN " + diag.column_name + " DROP NOT NULL";
    return diag;
  }

  // Duplicate / unique constraint
  if (lower_err.find("duplicate") != std::string::npos ||
      lower_err.find("unique constraint") != std::string::npos) {
    diag.category = FailureCategory::DUPLICATE_ROWS;
    diag.root_cause = "Duplicate rows detected during load.";
    diag.auto_remediable = agent_->self_heal_config.value("enabled", false) &&
                           agent_->self_heal_config.value("duplicate_rows", false);
    diag.proposed_fix = "Remove duplicate rows from target";
    return diag;
  }

  // Network / connection errors
  if (lower_err.find("connection") != std::string::npos ||
      lower_err.find("timeout") != std::string::npos ||
      lower_err.find("network") != std::string::npos ||
      lower_err.find("socket") != std::string::npos) {
    diag.category = FailureCategory::NETWORK_FAILURE;
    diag.root_cause = "Network failure during data transfer.";
    diag.auto_remediable = agent_->self_heal_config.value("enabled", false) &&
                           agent_->self_heal_config.value("checkpoint_resume", false);
    diag.proposed_fix = "Resume from last checkpoint.";
    return diag;
  }

  // Missing rows
  if (lower_err.find("missing rows") != std::string::npos ||
      lower_err.find("row count mismatch") != std::string::npos) {
    diag.category = FailureCategory::MISSING_ROWS;
    diag.root_cause = "Target has fewer rows than source.";
    diag.auto_remediable = agent_->self_heal_config.value("enabled", false) &&
                           agent_->self_heal_config.value("missing_rows", false);
    diag.proposed_fix = "Re-extract missing rows from source.";
    return diag;
  }

  // Default: unknown
  diag.category = FailureCategory::UNKNOWN;
  diag.root_cause = "Unclassified error: " + error_message;
  diag.auto_remediable = false;
  return diag;
}

bool SelfHealingEngine::remediate(const FailureDiagnosis& diagnosis) {
  if (!agent_->self_heal_config.value("enabled", false)) {
    escalate(diagnosis);
    return false;
  }

  if (!within_guardrails(diagnosis)) {
    escalate(diagnosis);
    return false;
  }

  // Audit the attempt
  if (agent_->self_heal_config.value("guardrails", nlohmann::json{})
          .value("audit_all_remediations", true)) {
    audit_log(agent_, "remediation_attempt", diagnosis.object_name,
              diagnosis.proposed_fix,
              {{"category", failure_category_to_string(diagnosis.category)},
               {"root_cause", diagnosis.root_cause},
               {"affected_rows", diagnosis.estimated_affected_rows}});
  }

  bool success = false;
  switch (diagnosis.category) {
    case FailureCategory::MISSING_ROWS:
      success = remediate_missing_rows(diagnosis);
      break;
    case FailureCategory::DUPLICATE_ROWS:
      success = remediate_duplicate_rows(diagnosis);
      break;
    case FailureCategory::TYPE_CONVERSION_ERROR:
    case FailureCategory::COLUMN_OVERFLOW:
      success = remediate_column_overflow(diagnosis);
      break;
    case FailureCategory::NETWORK_FAILURE:
      success = remediate_network_failure(diagnosis);
      break;
    default:
      escalate(diagnosis);
      return false;
  }

  if (success) {
    total_auto_fixes_++;
    total_auto_fix_rows_ += diagnosis.estimated_affected_rows;

    auto it = agent_->objects.find(diagnosis.object_name);
    if (it != agent_->objects.end()) {
      auto* obj = it->second;
      obj->remediation_log.push_back(
          "AUTO-FIX: " + failure_category_to_string(diagnosis.category)
          + " - " + diagnosis.proposed_fix);
      obj->state = ObjectMigrationState::EXTRACTING;  // Re-enter pipeline
      obj->retry_count++;
    }
    agent_->total_remediations++;
  }

  return success;
}

bool SelfHealingEngine::within_guardrails(const FailureDiagnosis& diagnosis) {
  auto guardrails = agent_->self_heal_config.value("guardrails", nlohmann::json{});

  int max_rows = guardrails.value("max_auto_fix_rows", 1000);
  if (diagnosis.estimated_affected_rows > max_rows) {
    audit_log(agent_, "guardrail_blocked", diagnosis.object_name,
              "Exceeds max_auto_fix_rows",
              {{"affected", diagnosis.estimated_affected_rows}, {"limit", max_rows}});
    return false;
  }

  auto it = agent_->objects.find(diagnosis.object_name);
  if (it != agent_->objects.end()) {
    auto* obj = it->second;
    double max_pct = guardrails.value("max_auto_fix_percentage", 0.001);
    if (obj->row_count > 0) {
      double pct = static_cast<double>(diagnosis.estimated_affected_rows)
                   / static_cast<double>(obj->row_count);
      if (pct > max_pct) {
        audit_log(agent_, "guardrail_blocked", diagnosis.object_name,
                  "Exceeds max_auto_fix_percentage",
                  {{"percentage", pct}, {"limit", max_pct}});
        return false;
      }
    }

    int max_retries = guardrails.value("max_retries_per_table", 3);
    if (obj->retry_count >= max_retries) {
      audit_log(agent_, "guardrail_blocked", diagnosis.object_name,
                "Exceeds max_retries_per_table",
                {{"retries", obj->retry_count}, {"limit", max_retries}});
      return false;
    }
  }

  return true;
}

void SelfHealingEngine::escalate(const FailureDiagnosis& diagnosis) {
  audit_log(agent_, "escalation", diagnosis.object_name,
            "Escalated to human operator",
            {{"category", failure_category_to_string(diagnosis.category)},
             {"root_cause", diagnosis.root_cause},
             {"proposed_fix", diagnosis.proposed_fix}});

  auto it = agent_->objects.find(diagnosis.object_name);
  if (it != agent_->objects.end()) {
    it->second->state = ObjectMigrationState::ESCALATED;
  }
}

bool SelfHealingEngine::remediate_missing_rows(const FailureDiagnosis& diagnosis) {
  audit_log(agent_, "remediate_missing_rows", diagnosis.object_name,
            "Re-extracting missing rows", {});
  return true;  // Simulated
}

bool SelfHealingEngine::remediate_duplicate_rows(const FailureDiagnosis& diagnosis) {
  audit_log(agent_, "remediate_duplicate_rows", diagnosis.object_name,
            "Removing duplicates from target", {});
  return true;  // Simulated
}

bool SelfHealingEngine::remediate_type_conversion(const FailureDiagnosis& diagnosis) {
  return remediate_column_overflow(diagnosis);
}

bool SelfHealingEngine::remediate_column_overflow(const FailureDiagnosis& diagnosis) {
  audit_log(agent_, "remediate_column_overflow", diagnosis.object_name,
            "Widening target column " + diagnosis.column_name, {});
  return true;  // Simulated — in production, executes ALTER TABLE
}

bool SelfHealingEngine::remediate_network_failure(const FailureDiagnosis& diagnosis) {
  audit_log(agent_, "remediate_network", diagnosis.object_name,
            "Resuming from checkpoint", {});
  return true;  // Simulated
}

bool SelfHealingEngine::remediate_checkpoint_resume(const FailureDiagnosis& diagnosis) {
  return remediate_network_failure(diagnosis);
}

bool SelfHealingEngine::check_max_auto_fix_rows(int affected_rows) {
  int max = agent_->self_heal_config.value("guardrails", nlohmann::json{})
                .value("max_auto_fix_rows", 1000);
  return affected_rows <= max;
}

bool SelfHealingEngine::check_max_auto_fix_percentage(int affected_rows, int total_rows) {
  if (total_rows <= 0) return true;
  double max_pct = agent_->self_heal_config.value("guardrails", nlohmann::json{})
                       .value("max_auto_fix_percentage", 0.001);
  return (static_cast<double>(affected_rows) / total_rows) <= max_pct;
}

bool SelfHealingEngine::check_max_retries(const std::string& object_name) {
  auto it = agent_->objects.find(object_name);
  if (it == agent_->objects.end()) return true;
  int max = agent_->self_heal_config.value("guardrails", nlohmann::json{})
                .value("max_retries_per_table", 3);
  return it->second->retry_count < max;
}

std::string SelfHealingEngine::extract_column_from_error(const std::string& error) {
  // Simple heuristic: look for quoted column name
  auto pos = error.find("'");
  if (pos != std::string::npos) {
    auto end = error.find("'", pos + 1);
    if (end != std::string::npos) return error.substr(pos + 1, end - pos - 1);
  }
  return "unknown_column";
}

std::string SelfHealingEngine::generate_alter_column_fix(
    const std::string& table, const std::string& col) {
  return "ALTER TABLE " + table + " ALTER COLUMN " + col + " SET DATA TYPE VARCHAR(MAX)";
}

std::string SelfHealingEngine::failure_category_to_string(FailureCategory cat) {
  switch (cat) {
    case FailureCategory::MISSING_ROWS: return "MISSING_ROWS";
    case FailureCategory::DUPLICATE_ROWS: return "DUPLICATE_ROWS";
    case FailureCategory::TYPE_CONVERSION_ERROR: return "TYPE_CONVERSION_ERROR";
    case FailureCategory::NULL_CONSTRAINT_VIOLATION: return "NULL_CONSTRAINT_VIOLATION";
    case FailureCategory::UNIQUE_CONSTRAINT_VIOLATION: return "UNIQUE_CONSTRAINT_VIOLATION";
    case FailureCategory::FK_CONSTRAINT_VIOLATION: return "FK_CONSTRAINT_VIOLATION";
    case FailureCategory::NETWORK_FAILURE: return "NETWORK_FAILURE";
    case FailureCategory::TIMEOUT: return "TIMEOUT";
    case FailureCategory::DISK_SPACE: return "DISK_SPACE";
    case FailureCategory::PERMISSION_DENIED: return "PERMISSION_DENIED";
    case FailureCategory::RATE_LIMITED: return "RATE_LIMITED";
    case FailureCategory::COLUMN_OVERFLOW: return "COLUMN_OVERFLOW";
    case FailureCategory::PRECISION_LOSS: return "PRECISION_LOSS";
    case FailureCategory::ENCODING_ERROR: return "ENCODING_ERROR";
    case FailureCategory::UNKNOWN: return "UNKNOWN";
  }
  return "UNKNOWN";
}

std::string SelfHealingEngine::to_lower(const std::string& s) {
  std::string r = s;
  std::transform(r.begin(), r.end(), r.begin(), ::tolower);
  return r;
}

}  // namespace neamc::vm::migration
