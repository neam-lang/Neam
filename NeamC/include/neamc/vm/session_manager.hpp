//
// Neam v0.8 Phase 3 — Session Manager
// JSONL-based session persistence for claw agents
//

#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "neamc/vm/claw_agent_type.hpp"

namespace neamc::vm
{

class SessionManager
{
public:
  // Get or create a session for the given agent + key.
  // If workspace is set, persists to disk; otherwise in-memory only.
  SessionState& get_or_create(ObjClawAgent* agent, const std::string& session_key);

  // Append a message to the session (role + content).
  // Writes JSONL to disk if workspace is set.
  void append_message(ObjClawAgent* agent, const std::string& session_key,
                      const std::string& role, const std::string& content);

  // Compact: keep last keep_recent messages, archive the rest as a summary placeholder.
  // The actual summarization is done externally; this just trims the history.
  void compact(ObjClawAgent* agent, const std::string& session_key,
               const std::string& summary, int keep_recent = 20);

  // Reset: clear the session history, optionally archive current session file.
  void reset(ObjClawAgent* agent, const std::string& session_key);

  // Load history from disk (if workspace set) or return in-memory history.
  std::vector<std::pair<std::string, std::string>> load_history(
      ObjClawAgent* agent, const std::string& session_key, int limit = -1);

private:
  std::string generate_session_id();
  std::string sessions_dir(const ObjClawAgent* agent);
  std::string session_file_path(const ObjClawAgent* agent, const std::string& session_id);
  void ensure_directory(const std::string& path);
  void write_sessions_index(const ObjClawAgent* agent);
  void load_sessions_index(ObjClawAgent* agent);
};

}  // namespace neamc::vm
