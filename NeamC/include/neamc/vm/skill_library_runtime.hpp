// v1.5 NeamEvolve — Skill library runtime.
//
// skill_acquire(library, name, code)  -> "ok" | "[E-...]" — gates: scope +
//                                        static analysis + capability monotonicity
//                                        + sandbox self-test
// skill_get(library, name)            -> code | error
// skill_list(library)                 -> JSON array
// skill_test(library, name)           -> "passed" | "failed" | error
// skill_deprecate(library, name)      -> "ok"
// skill_invoke(library, name, args)   -> output | "[E-SBX-VIOLATION] ..."
//
// Layered on v1.4.5 substrate + v1.0 code_sandbox concept.  Per NFR-SBX-1
// production execution requires a child process (seccomp/sandbox-exec); for
// P0 testability the sandbox is dry-run-stubbed when NEAM_HARNESS_DRY_RUN=1
// — the static analysis + capability monotonicity layers (the load-bearing
// safety checks) are always enforced.

#pragma once

#include <string>

namespace neamc::vm::skill {

struct SkillResult
{
  bool        ok{false};
  std::string output;       // stdout from invocation; "ok" on simple success
  std::string error_code;   // "" | "SK-UNKNOWN" | "SK-NOSKILL" | "SK-CAP" | "SK-SBX"
                            //    | "SK-UNSIGNED" | "SK-NOACQ" | "SK-SCOPE"
};

SkillResult skill_acquire(const std::string& library_name,
                          const std::string& skill_name,
                          const std::string& code);

SkillResult skill_get(const std::string& library_name,
                      const std::string& skill_name);

std::string skill_list_json(const std::string& library_name);

SkillResult skill_test(const std::string& library_name,
                       const std::string& skill_name);

SkillResult skill_deprecate(const std::string& library_name,
                            const std::string& skill_name);

SkillResult skill_invoke(const std::string& library_name,
                         const std::string& skill_name,
                         const std::string& args_json);

}  // namespace neamc::vm::skill
