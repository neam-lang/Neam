//
// Neam v0.8 — Claw Agent runtime type
//

#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "neamc/vm/object.hpp"

namespace neamc::vm
{

struct SessionState
{
  std::string session_key;
  std::string session_id;
  std::vector<std::pair<std::string, std::string>> history;  // role, content
  std::string workspace_path;
  bool is_compacting{false};
};

struct LaneConfig
{
  std::string name;
  int concurrency{1};
  std::string priority{"normal"};
};

struct ObjClawAgent : Obj
{
  // Base agent fields (mirrors ObjAgent)
  ObjString* name{nullptr};
  ObjString* provider{nullptr};
  ObjString* model{nullptr};
  ObjString* endpoint{nullptr};
  ObjString* api_key_env{nullptr};
  ObjString* system{nullptr};
  double temperature{0.0};
  ObjList* skills{nullptr};
  ObjList* connected_knowledge{nullptr};
  ObjContext* context{nullptr};
  // Claw-specific config
  int idle_reset_minutes{60};
  int daily_reset_hour{4};
  int max_history_turns{100};
  std::string compaction{"auto"};
  std::vector<std::string> channel_names;
  std::vector<LaneConfig> lanes;
  std::string memory_backend;
  std::string memory_search;
  bool flush_on_compact{false};
  std::string workspace;
  // Runtime state
  std::unordered_map<std::string, SessionState> sessions;
};

struct ObjChannel : Obj
{
  ObjString* name{nullptr};
  ObjMap* config{nullptr};
};

struct ObjWorkspace : Obj
{
  ObjString* path{nullptr};
};

}  // namespace neamc::vm
