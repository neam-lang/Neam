// SPDX-License-Identifier: Apache-2.0
//
// Neam Eval - Grading strategies for evaluation framework
//

#pragma once

#include <string>

#include "neamc/llm/provider.hpp"

namespace neamc::eval
{

struct GraderConfig
{
  std::string judge_provider{"openai"};
  std::string judge_model{"gpt-4o-mini"};
  std::string judge_api_key;
  double semantic_threshold{0.8};
  std::string embedding_provider{"openai"};
  std::string embedding_model{"text-embedding-3-small"};
  std::string embedding_api_key;
};

struct GradeResult
{
  bool passed{false};
  std::string explanation;
  llm::UsageInfo usage;  // For LLM-judge token tracking
};

/// Grade an actual output against expected using the specified grader.
/// Supported graders: exact_match, contains, regex, llm_judge, semantic_match
GradeResult grade(const std::string& expected, const std::string& actual,
                  const std::string& grader, const GraderConfig& config = {});

}  // namespace neamc::eval
