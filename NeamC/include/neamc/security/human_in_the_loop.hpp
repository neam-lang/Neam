//
// Neam v0.6.9 — Human-in-the-Loop Controls (D10)
//
// Manages confirmation protocol for sensitive tool invocations.
// Tools marked `sensitive: true` or listed in policy `confirm:` require
// explicit human approval before execution proceeds.
//

#pragma once

#include <chrono>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace neamc::security {

enum class ConfirmationStatus
{
  Pending,
  Approved,
  Denied,
  Expired
};

struct PendingConfirmation
{
  std::string trace_id;
  std::string agent_name;
  std::string tool_name;
  std::string args_json;
  std::string reason;
  std::chrono::steady_clock::time_point created_at;
  int timeout_seconds{300};
  ConfirmationStatus status{ConfirmationStatus::Pending};
};

class ConfirmationManager
{
 public:
  static ConfirmationManager& instance();

  // Submit a pending confirmation request. Returns the trace_id.
  std::string submit(const std::string& trace_id,
                     const std::string& agent_name,
                     const std::string& tool_name,
                     const std::string& args_json,
                     const std::string& reason);

  // Approve a pending confirmation.
  bool approve(const std::string& trace_id);

  // Deny a pending confirmation.
  bool deny(const std::string& trace_id);

  // Check if a tool call has been pre-approved for this trace.
  // Returns true if approved, false if denied/expired/not found.
  std::optional<ConfirmationStatus> check(const std::string& trace_id);

  // Get a pending confirmation by trace_id.
  std::optional<PendingConfirmation> get(const std::string& trace_id);

  // Clean up expired confirmations. Returns count removed.
  int cleanup_expired();

  // Get all pending confirmations (for admin status endpoint).
  std::vector<PendingConfirmation> pending_list() const;

 private:
  ConfirmationManager() = default;
  mutable std::mutex mutex_;
  std::unordered_map<std::string, PendingConfirmation> confirmations_;
};

}  // namespace neamc::security
