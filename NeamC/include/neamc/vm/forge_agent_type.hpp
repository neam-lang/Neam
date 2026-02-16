//
// Neam v0.8 — Forge Agent runtime type
//

#pragma once

#include <string>

#include "neamc/vm/object.hpp"

namespace neamc::vm
{

struct ForgeLoopConfig
{
  int max_iterations{25};
  double max_cost{10.0};
  int max_tokens{500000};
  std::string prompt_file;
  std::string plan_file;
  std::string progress_file;
  std::string learnings_file;
};

struct ObjForgeAgent : Obj
{
  // Base agent fields
  ObjString* name{nullptr};
  ObjString* provider{nullptr};
  ObjString* model{nullptr};
  ObjString* endpoint{nullptr};
  ObjString* api_key_env{nullptr};
  ObjString* system{nullptr};
  double temperature{0.0};
  ObjList* skills{nullptr};
  ObjContext* context{nullptr};
  // Forge-specific
  ForgeLoopConfig loop_config;
  ObjFunction* verify_fn{nullptr};
  std::string checkpoint;
  std::string workspace;
};

struct ObjLoopContext : Obj
{
  ObjForgeAgent* forge_agent{nullptr};
  int iteration{0};
  std::string current_task;
  std::string feedback;
  double total_cost{0.0};
  int total_tokens{0};
};

}  // namespace neamc::vm
