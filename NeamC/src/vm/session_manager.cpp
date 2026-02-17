//
// Neam v0.8 Phase 3 — Session Manager implementation
//

#include "neamc/vm/session_manager.hpp"

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <sstream>

#include <nlohmann/json.hpp>

namespace neamc::vm
{

namespace
{
std::string hex_string(int bytes)
{
  static thread_local std::mt19937 rng{std::random_device{}()};
  std::uniform_int_distribution<int> dist(0, 255);
  std::ostringstream oss;
  oss << std::hex;
  for (int i = 0; i < bytes; ++i)
  {
    int byte = dist(rng);
    if (byte < 16) oss << '0';
    oss << byte;
  }
  return oss.str();
}

int64_t epoch_ms()
{
  return std::chrono::duration_cast<std::chrono::milliseconds>(
             std::chrono::system_clock::now().time_since_epoch())
      .count();
}
}  // namespace

std::string SessionManager::generate_session_id()
{
  return hex_string(8);
}

std::string SessionManager::sessions_dir(const ObjClawAgent* agent)
{
  return agent->workspace + "/sessions";
}

std::string SessionManager::session_file_path(const ObjClawAgent* agent,
                                              const std::string& session_id)
{
  return sessions_dir(agent) + "/" + session_id + ".jsonl";
}

void SessionManager::ensure_directory(const std::string& path)
{
  std::filesystem::create_directories(path);
}

void SessionManager::write_sessions_index(const ObjClawAgent* agent)
{
  if (agent->workspace.empty()) return;
  std::string dir = sessions_dir(agent);
  ensure_directory(dir);
  std::string index_path = dir + "/sessions.json";
  nlohmann::json index = nlohmann::json::object();
  for (const auto& [key, state] : agent->sessions)
  {
    index[key] = state.session_id;
  }
  std::ofstream out(index_path);
  out << index.dump(2) << "\n";
}

void SessionManager::load_sessions_index(ObjClawAgent* agent)
{
  if (agent->workspace.empty()) return;
  std::string index_path = sessions_dir(agent) + "/sessions.json";
  std::ifstream in(index_path);
  if (!in.is_open()) return;
  try
  {
    nlohmann::json index = nlohmann::json::parse(in);
    for (auto it = index.begin(); it != index.end(); ++it)
    {
      const std::string& key = it.key();
      const std::string& sid = it.value().get_ref<const std::string&>();
      if (agent->sessions.find(key) == agent->sessions.end())
      {
        SessionState state;
        state.session_key = key;
        state.session_id = sid;
        state.workspace_path = agent->workspace;
        agent->sessions[key] = std::move(state);
      }
    }
  }
  catch (...)
  {
    // Corrupted index — ignore, will be recreated
  }
}

SessionState& SessionManager::get_or_create(ObjClawAgent* agent,
                                             const std::string& session_key)
{
  auto it = agent->sessions.find(session_key);
  if (it != agent->sessions.end())
  {
    return it->second;
  }

  // Try loading from disk
  if (!agent->workspace.empty())
  {
    load_sessions_index(agent);
    it = agent->sessions.find(session_key);
    if (it != agent->sessions.end())
    {
      // Load history from JSONL file
      auto& state = it->second;
      std::string fpath = session_file_path(agent, state.session_id);
      std::ifstream in(fpath);
      if (in.is_open())
      {
        std::string line;
        while (std::getline(in, line))
        {
          if (line.empty()) continue;
          try
          {
            auto j = nlohmann::json::parse(line);
            std::string role = j.value("role", "");
            std::string content = j.value("content", "");
            if (!role.empty())
            {
              state.history.emplace_back(std::move(role), std::move(content));
            }
          }
          catch (...)
          {
            // Skip malformed lines
          }
        }
      }
      return state;
    }
  }

  // Create new session
  SessionState state;
  state.session_key = session_key;
  state.session_id = generate_session_id();
  state.workspace_path = agent->workspace;
  auto [inserted, _] = agent->sessions.emplace(session_key, std::move(state));

  // Persist index
  if (!agent->workspace.empty())
  {
    write_sessions_index(agent);
  }

  return inserted->second;
}

void SessionManager::append_message(ObjClawAgent* agent, const std::string& session_key,
                                    const std::string& role, const std::string& content)
{
  auto& session = get_or_create(agent, session_key);
  session.history.emplace_back(role, content);

  // Write JSONL to disk
  if (!agent->workspace.empty())
  {
    std::string dir = sessions_dir(agent);
    ensure_directory(dir);
    std::string fpath = session_file_path(agent, session.session_id);
    std::ofstream out(fpath, std::ios::app);
    nlohmann::json j;
    j["ts"] = epoch_ms();
    j["role"] = role;
    j["content"] = content;
    out << j.dump() << "\n";
  }
}

void SessionManager::compact(ObjClawAgent* agent, const std::string& session_key,
                              const std::string& summary, int keep_recent)
{
  auto& session = get_or_create(agent, session_key);
  if (static_cast<int>(session.history.size()) <= keep_recent)
  {
    return;  // Nothing to compact
  }

  // Keep last keep_recent messages
  int to_remove = static_cast<int>(session.history.size()) - keep_recent;
  std::vector<std::pair<std::string, std::string>> kept(
      session.history.begin() + to_remove, session.history.end());

  // Prepend summary as a system message
  session.history.clear();
  session.history.emplace_back("system", "[Session Summary] " + summary);
  session.history.insert(session.history.end(), kept.begin(), kept.end());

  // Rewrite session file on disk
  if (!agent->workspace.empty())
  {
    std::string fpath = session_file_path(agent, session.session_id);
    std::ofstream out(fpath, std::ios::trunc);
    for (const auto& [r, c] : session.history)
    {
      nlohmann::json j;
      j["ts"] = epoch_ms();
      j["role"] = r;
      j["content"] = c;
      out << j.dump() << "\n";
    }
  }
}

void SessionManager::reset(ObjClawAgent* agent, const std::string& session_key)
{
  auto it = agent->sessions.find(session_key);
  if (it != agent->sessions.end())
  {
    // Archive old session file by renaming
    if (!agent->workspace.empty())
    {
      std::string old_path = session_file_path(agent, it->second.session_id);
      std::string archive_path = old_path + ".archived";
      std::filesystem::rename(old_path, archive_path);
    }
    agent->sessions.erase(it);
  }

  // Create fresh session
  get_or_create(agent, session_key);
}

std::vector<std::pair<std::string, std::string>> SessionManager::load_history(
    ObjClawAgent* agent, const std::string& session_key, int limit)
{
  auto& session = get_or_create(agent, session_key);
  if (limit < 0 || limit >= static_cast<int>(session.history.size()))
  {
    return session.history;
  }
  // Return last `limit` entries
  return std::vector<std::pair<std::string, std::string>>(
      session.history.end() - limit, session.history.end());
}

}  // namespace neamc::vm
