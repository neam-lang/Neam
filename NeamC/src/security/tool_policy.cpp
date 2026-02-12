//
// Neam v0.6.9 — Tool Permission Model Implementation (D2)
//

#include "neamc/security/tool_policy.hpp"

namespace neamc::security {

bool match_glob(const std::string& pattern, const std::string& value)
{
  size_t pi = 0, vi = 0;
  size_t star_pi = std::string::npos, star_vi = 0;

  while (vi < value.size())
  {
    if (pi < pattern.size() && (pattern[pi] == '?' || pattern[pi] == value[vi]))
    {
      ++pi;
      ++vi;
    }
    else if (pi < pattern.size() && pattern[pi] == '*')
    {
      star_pi = pi;
      star_vi = vi;
      ++pi;
    }
    else if (star_pi != std::string::npos)
    {
      pi = star_pi + 1;
      ++star_vi;
      vi = star_vi;
    }
    else
    {
      return false;
    }
  }

  while (pi < pattern.size() && pattern[pi] == '*')
  {
    ++pi;
  }
  return pi == pattern.size();
}

PolicyResult evaluate_policy(const PolicyDef& policy,
                             const std::string& tool_name,
                             const std::string& args_text)
{
  // 1. Explicit deny list takes highest priority
  if (policy.deny_tools.count(tool_name))
  {
    return {PolicyAction::Deny, "Tool '" + tool_name + "' is explicitly denied by policy"};
  }

  // Check deny with glob patterns
  for (const auto& pattern : policy.deny_tools)
  {
    if (pattern.find('*') != std::string::npos || pattern.find('?') != std::string::npos)
    {
      if (match_glob(pattern, tool_name))
      {
        return {PolicyAction::Deny,
                "Tool '" + tool_name + "' matches deny pattern '" + pattern + "'"};
      }
    }
  }

  // 2. Confirm list
  if (policy.confirm_tools.count(tool_name))
  {
    return {PolicyAction::Confirm,
            "Tool '" + tool_name + "' requires human confirmation"};
  }

  for (const auto& pattern : policy.confirm_tools)
  {
    if (pattern.find('*') != std::string::npos || pattern.find('?') != std::string::npos)
    {
      if (match_glob(pattern, tool_name))
      {
        return {PolicyAction::Confirm,
                "Tool '" + tool_name + "' matches confirm pattern '" + pattern + "'"};
      }
    }
  }

  // 3. Allow list
  if (policy.allow_tools.count(tool_name))
  {
    // Check per-tool constraints if any
    auto constraint_it = policy.constraints.find(tool_name);
    if (constraint_it != policy.constraints.end())
    {
      const auto& constraint = constraint_it->second;

      // Check deny_args patterns
      for (const auto& pattern : constraint.deny_args)
      {
        if (match_glob(pattern, args_text))
        {
          return {PolicyAction::Deny,
                  "Tool '" + tool_name + "' args match deny pattern '" + pattern + "'"};
        }
      }

      // Check deny_paths patterns
      for (const auto& pattern : constraint.deny_paths)
      {
        if (match_glob(pattern, args_text))
        {
          return {PolicyAction::Deny,
                  "Tool '" + tool_name + "' path matches deny pattern '" + pattern + "'"};
        }
      }

      // If allow_args specified, args must match at least one
      if (!constraint.allow_args.empty())
      {
        bool matched = false;
        for (const auto& pattern : constraint.allow_args)
        {
          if (match_glob(pattern, args_text))
          {
            matched = true;
            break;
          }
        }
        if (!matched)
        {
          return {PolicyAction::Deny,
                  "Tool '" + tool_name + "' args don't match any allow pattern"};
        }
      }
    }

    return {PolicyAction::Allow, "Tool '" + tool_name + "' is allowed by policy"};
  }

  // Check allow with glob patterns
  for (const auto& pattern : policy.allow_tools)
  {
    if (pattern.find('*') != std::string::npos || pattern.find('?') != std::string::npos)
    {
      if (match_glob(pattern, tool_name))
      {
        return {PolicyAction::Allow,
                "Tool '" + tool_name + "' matches allow pattern '" + pattern + "'"};
      }
    }
  }

  // 4. Default action
  if (policy.default_deny)
  {
    return {PolicyAction::Deny,
            "Tool '" + tool_name + "' not in allow list (default-deny policy)"};
  }
  return {PolicyAction::Allow, "Tool '" + tool_name + "' allowed by default-allow policy"};
}

}  // namespace neamc::security
