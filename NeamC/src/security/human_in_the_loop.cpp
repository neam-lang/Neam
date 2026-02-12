//
// Neam v0.6.9 — Human-in-the-Loop Implementation (D10)
//

#include "neamc/security/human_in_the_loop.hpp"

namespace neamc::security {

ConfirmationManager& ConfirmationManager::instance()
{
  static ConfirmationManager inst;
  return inst;
}

std::string ConfirmationManager::submit(const std::string& trace_id,
                                        const std::string& agent_name,
                                        const std::string& tool_name,
                                        const std::string& args_json,
                                        const std::string& reason)
{
  std::lock_guard<std::mutex> lock(mutex_);

  PendingConfirmation pc;
  pc.trace_id = trace_id;
  pc.agent_name = agent_name;
  pc.tool_name = tool_name;
  pc.args_json = args_json;
  pc.reason = reason;
  pc.created_at = std::chrono::steady_clock::now();
  pc.status = ConfirmationStatus::Pending;

  confirmations_[trace_id] = std::move(pc);
  return trace_id;
}

bool ConfirmationManager::approve(const std::string& trace_id)
{
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = confirmations_.find(trace_id);
  if (it == confirmations_.end()) return false;

  auto& pc = it->second;
  // Check if expired
  auto elapsed = std::chrono::steady_clock::now() - pc.created_at;
  if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() >= pc.timeout_seconds)
  {
    pc.status = ConfirmationStatus::Expired;
    return false;
  }

  if (pc.status != ConfirmationStatus::Pending) return false;
  pc.status = ConfirmationStatus::Approved;
  return true;
}

bool ConfirmationManager::deny(const std::string& trace_id)
{
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = confirmations_.find(trace_id);
  if (it == confirmations_.end()) return false;
  if (it->second.status != ConfirmationStatus::Pending) return false;
  it->second.status = ConfirmationStatus::Denied;
  return true;
}

std::optional<ConfirmationStatus> ConfirmationManager::check(const std::string& trace_id)
{
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = confirmations_.find(trace_id);
  if (it == confirmations_.end()) return std::nullopt;

  auto& pc = it->second;
  // Check expiry on access
  if (pc.status == ConfirmationStatus::Pending)
  {
    auto elapsed = std::chrono::steady_clock::now() - pc.created_at;
    if (std::chrono::duration_cast<std::chrono::seconds>(elapsed).count() >= pc.timeout_seconds)
    {
      pc.status = ConfirmationStatus::Expired;
    }
  }
  return pc.status;
}

std::optional<PendingConfirmation> ConfirmationManager::get(const std::string& trace_id)
{
  std::lock_guard<std::mutex> lock(mutex_);
  auto it = confirmations_.find(trace_id);
  if (it == confirmations_.end()) return std::nullopt;
  return it->second;
}

int ConfirmationManager::cleanup_expired()
{
  std::lock_guard<std::mutex> lock(mutex_);
  int count = 0;
  auto now = std::chrono::steady_clock::now();
  for (auto it = confirmations_.begin(); it != confirmations_.end();)
  {
    auto& pc = it->second;
    auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - pc.created_at).count();
    if (elapsed >= pc.timeout_seconds)
    {
      it = confirmations_.erase(it);
      ++count;
    }
    else
    {
      ++it;
    }
  }
  return count;
}

std::vector<PendingConfirmation> ConfirmationManager::pending_list() const
{
  std::lock_guard<std::mutex> lock(mutex_);
  std::vector<PendingConfirmation> result;
  for (const auto& [id, pc] : confirmations_)
  {
    if (pc.status == ConfirmationStatus::Pending)
    {
      result.push_back(pc);
    }
  }
  return result;
}

}  // namespace neamc::security
