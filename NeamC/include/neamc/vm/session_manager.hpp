//
// v0.8: Session manager — manages claw agent conversation sessions
//

#pragma once

#include <string>
#include <unordered_map>
#include <vector>

namespace neamc::vm
{

struct ObjClawAgent;

struct Session
{
  std::vector<std::pair<std::string, std::string>> history;
};

class SessionManager
{
public:
  Session& get_or_create(ObjClawAgent* /*agent*/, const std::string& key)
  {
    return sessions_[key];
  }

  void append_message(ObjClawAgent* /*agent*/, const std::string& key,
                      const std::string& role, const std::string& content)
  {
    sessions_[key].history.emplace_back(role, content);
  }

  void compact(ObjClawAgent* /*agent*/, const std::string& key, const std::string& summary)
  {
    auto& s = sessions_[key];
    s.history.clear();
    s.history.emplace_back("system", summary);
  }

  void reset(ObjClawAgent* /*agent*/, const std::string& key)
  {
    sessions_[key].history.clear();
  }

  std::vector<std::pair<std::string, std::string>> load_history(
      ObjClawAgent* /*agent*/, const std::string& key, int limit = -1)
  {
    auto it = sessions_.find(key);
    if (it == sessions_.end()) return {};
    const auto& h = it->second.history;
    if (limit < 0 || static_cast<std::size_t>(limit) >= h.size()) return h;
    return {h.end() - limit, h.end()};
  }

private:
  std::unordered_map<std::string, Session> sessions_;
};

}  // namespace neamc::vm
