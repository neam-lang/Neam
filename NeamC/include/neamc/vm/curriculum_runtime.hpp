// v1.5 NeamEvolve P1 — Curriculum runtime (auto-progression of task difficulty).
//
// curriculum_next(name)             -> task description string | error
// curriculum_advance(name, success) -> "ok" | "advanced" | "regressed" | error
// curriculum_difficulty(name)       -> current difficulty as Number

#pragma once

#include <string>

namespace neamc::vm::curriculum {

struct CurriculumResult
{
  bool        ok{false};
  std::string output;
  std::string error_code;   // "" | "CR-UNKNOWN" | "CR-EMPTY-POOL"
};

CurriculumResult curriculum_next   (const std::string& name);
CurriculumResult curriculum_advance(const std::string& name, bool success);
double           curriculum_difficulty(const std::string& name);

}  // namespace neamc::vm::curriculum
