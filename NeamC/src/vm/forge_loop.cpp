//
// Neam v0.8 Phase 4 — Forge Loop helper implementations
//

#include "neamc/vm/forge_loop.hpp"

#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <sstream>

namespace neamc::vm
{
namespace
{

// Strip leading whitespace, then markdown bullet/number prefixes
std::string strip_prefix(const std::string& line)
{
  std::size_t start = 0;
  while (start < line.size() && (line[start] == ' ' || line[start] == '\t'))
    ++start;

  std::string trimmed = line.substr(start);

  // Strip "- ", "* ", "+ " prefixes
  if (trimmed.size() >= 2 &&
      (trimmed[0] == '-' || trimmed[0] == '*' || trimmed[0] == '+') &&
      trimmed[1] == ' ')
  {
    trimmed = trimmed.substr(2);
  }
  // Strip "1. ", "2. " etc numbered prefixes
  else if (!trimmed.empty() && trimmed[0] >= '0' && trimmed[0] <= '9')
  {
    auto dot_pos = trimmed.find(". ");
    if (dot_pos != std::string::npos && dot_pos < 5)
    {
      trimmed = trimmed.substr(dot_pos + 2);
    }
  }

  // Trim trailing whitespace
  while (!trimmed.empty() &&
         (trimmed.back() == ' ' || trimmed.back() == '\t' ||
          trimmed.back() == '\r' || trimmed.back() == '\n'))
  {
    trimmed.pop_back();
  }

  return trimmed;
}

}  // anonymous namespace

std::vector<std::string> load_plan_tasks(const std::string& path)
{
  std::vector<std::string> tasks;
  std::ifstream in(path);
  if (!in)
    return tasks;

  std::string line;
  while (std::getline(in, line))
  {
    std::string task = strip_prefix(line);
    if (!task.empty())
    {
      tasks.push_back(std::move(task));
    }
  }
  return tasks;
}

std::vector<std::string> load_completed_tasks(const std::string& path)
{
  std::vector<std::string> completed;
  std::ifstream in(path);
  if (!in)
    return completed;

  std::string line;
  while (std::getline(in, line))
  {
    // Minimal JSONL parsing: look for "task":"..." and "status":"completed"
    auto task_pos = line.find("\"task\":\"");
    auto status_pos = line.find("\"status\":\"completed\"");
    if (task_pos != std::string::npos && status_pos != std::string::npos)
    {
      task_pos += 8;  // skip "task":"
      auto end_pos = line.find('"', task_pos);
      if (end_pos != std::string::npos)
      {
        completed.push_back(line.substr(task_pos, end_pos - task_pos));
      }
    }
  }
  return completed;
}

void mark_task_done(const std::string& path, int iteration, const std::string& task)
{
  std::ofstream out(path, std::ios::app);
  if (!out)
    return;
  // Simple JSONL — no special chars expected in task names
  out << "{\"iteration\":" << iteration
      << ",\"task\":\"" << task
      << "\",\"status\":\"completed\"}\n";
}

void append_learning(const std::string& path, int iteration,
                     const std::string& task, const std::string& status,
                     const std::string& detail)
{
  std::ofstream out(path, std::ios::app);
  if (!out)
    return;
  out << "{\"iteration\":" << iteration
      << ",\"task\":\"" << task
      << "\",\"status\":\"" << status
      << "\",\"detail\":\"" << detail << "\"}\n";
}

void forge_checkpoint(const std::string& strategy, const std::string& workspace,
                      int iteration, const std::string& task)
{
  if (strategy.empty() || strategy == "none")
    return;

  if (strategy == "git")
  {
    std::string msg = "forge checkpoint iter " + std::to_string(iteration);
    if (!task.empty())
      msg += ": " + task;
    std::string cmd = "cd \"" + workspace + "\" && git add -A && git commit -m '"
                      + msg + "' --allow-empty 2>/dev/null";
    FILE* proc = popen(cmd.c_str(), "r");
    if (proc)
      pclose(proc);
  }
  else if (strategy == "snapshot")
  {
    std::string snap_dir = workspace + "/.snapshots/" + std::to_string(iteration);
    std::error_code ec;
    std::filesystem::create_directories(snap_dir, ec);
    if (ec)
      return;

    for (const auto& entry : std::filesystem::directory_iterator(workspace, ec))
    {
      if (ec)
        break;
      const auto& p = entry.path();
      if (p.filename() == ".snapshots")
        continue;
      std::filesystem::copy(p, snap_dir / p.filename(),
                            std::filesystem::copy_options::recursive |
                            std::filesystem::copy_options::overwrite_existing,
                            ec);
    }
  }
}

}  // namespace neamc::vm
