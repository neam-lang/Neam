//
// Neam v0.6.9 — Tool Permission Model (D2)
//
// Policy-based access control for agent tool invocations.
// Supports allow/deny/confirm actions with per-tool constraints.
//

#pragma once

#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace neamc::security {

enum class PolicyAction { Allow, Deny, Confirm };

struct ToolConstraint {
  std::vector<std::string> allow_args;   // glob patterns for allowed arg values
  std::vector<std::string> deny_args;    // glob patterns for denied arg values
  std::vector<std::string> allow_paths;  // filesystem glob patterns
  std::vector<std::string> deny_paths;   // filesystem glob patterns
  size_t max_output = 0;                 // 0 = unlimited
  long timeout_ms = 0;                   // 0 = default
};

struct PolicyDef {
  std::string name;
  std::unordered_set<std::string> allow_tools;
  std::unordered_set<std::string> deny_tools;
  std::unordered_set<std::string> confirm_tools;
  std::unordered_map<std::string, ToolConstraint> constraints;
  bool default_deny = true;  // deny tools not in allow list
};

struct PolicyResult {
  PolicyAction action;
  std::string reason;
};

// Evaluate a policy for a given tool invocation
PolicyResult evaluate_policy(const PolicyDef& policy,
                             const std::string& tool_name,
                             const std::string& args_text);

// Simple glob matching (supports * and ? wildcards)
bool match_glob(const std::string& pattern, const std::string& value);

}  // namespace neamc::security
