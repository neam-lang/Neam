// SPDX-License-Identifier: Apache-2.0
//
// Neam Eval - Grading strategies implementation
//

#include "neamc/eval/graders.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <regex>
#include <stdexcept>

#include "neamc/llm/http_client.hpp"

namespace neamc::eval
{
namespace
{

std::string normalize_case(std::string value)
{
  for (char& c : value)
  {
    c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
  }
  return value;
}

GradeResult grade_exact_match(const std::string& expected, const std::string& actual)
{
  GradeResult result;
  result.passed = normalize_case(expected) == normalize_case(actual);
  result.explanation = result.passed ? "Exact match (case-insensitive)" : "No exact match";
  return result;
}

GradeResult grade_contains(const std::string& expected, const std::string& actual)
{
  GradeResult result;
  result.passed = actual.find(expected) != std::string::npos;
  result.explanation = result.passed ? "Expected substring found" : "Expected substring not found";
  return result;
}

GradeResult grade_regex(const std::string& expected, const std::string& actual)
{
  GradeResult result;
  try
  {
    const std::regex pattern(expected);
    result.passed = std::regex_search(actual, pattern);
    result.explanation = result.passed ? "Regex pattern matched" : "Regex pattern did not match";
  }
  catch (const std::regex_error& e)
  {
    result.passed = false;
    result.explanation = std::string("Invalid regex: ") + e.what();
  }
  return result;
}

GradeResult grade_llm_judge(const std::string& expected, const std::string& actual,
                             const GraderConfig& config)
{
  GradeResult result;

  // Build judge prompt
  std::string prompt =
      "You are an evaluation judge. Compare the expected output with the actual output.\n\n"
      "Expected output:\n" + expected + "\n\n"
      "Actual output:\n" + actual + "\n\n"
      "Determine if the actual output is semantically equivalent to or correctly answers "
      "what the expected output describes. Reply with exactly PASS or FAIL on the first line, "
      "followed by a brief explanation on the next line.";

  try
  {
    // Resolve API key
    std::string api_key = config.judge_api_key;
    if (api_key.empty())
    {
      if (const char* env_val = std::getenv("OPENAI_API_KEY"))
      {
        api_key = env_val;
      }
    }

    // Build request
    nlohmann::json payload;
    payload["model"] = config.judge_model;
    payload["messages"] = nlohmann::json::array({
        {{"role", "user"}, {"content", prompt}}
    });
    payload["temperature"] = 0.0;

    std::string endpoint;
    std::vector<std::string> headers = {"Content-Type: application/json"};

    if (config.judge_provider == "anthropic")
    {
      endpoint = "https://api.anthropic.com/v1/messages";
      payload["max_tokens"] = 256;
      headers.push_back("x-api-key: " + api_key);
      headers.push_back("anthropic-version: 2023-06-01");
    }
    else
    {
      // Default to OpenAI-compatible
      endpoint = "https://api.openai.com/v1/chat/completions";
      headers.push_back("Authorization: Bearer " + api_key);
    }

    const std::string response_str = llm::http_post_json(endpoint, payload.dump(), headers);
    const auto response_json = nlohmann::json::parse(response_str);

    // Extract response text
    std::string judge_response;
    if (response_json.contains("choices") && !response_json["choices"].empty())
    {
      judge_response = response_json["choices"][0]["message"].value("content", "");
    }
    else if (response_json.contains("content") && response_json["content"].is_array())
    {
      for (const auto& block : response_json["content"])
      {
        if (block.value("type", "") == "text")
        {
          judge_response = block.value("text", "");
          break;
        }
      }
    }

    // Parse PASS/FAIL from first line
    std::string first_line = judge_response.substr(0, judge_response.find('\n'));
    // Trim
    while (!first_line.empty() && std::isspace(static_cast<unsigned char>(first_line.back())))
    {
      first_line.pop_back();
    }

    result.passed = (normalize_case(first_line) == "pass");
    result.explanation = judge_response;

    // Extract usage
    if (response_json.contains("usage"))
    {
      result.usage.prompt_tokens = response_json["usage"].value("prompt_tokens", std::size_t{0});
      result.usage.completion_tokens = response_json["usage"].value("completion_tokens", std::size_t{0});
      result.usage.total_tokens = response_json["usage"].value("total_tokens", std::size_t{0});
    }
  }
  catch (const std::exception& e)
  {
    result.passed = false;
    result.explanation = std::string("LLM judge error: ") + e.what();
  }

  return result;
}

GradeResult grade_semantic_match(const std::string& expected, const std::string& actual,
                                  const GraderConfig& config)
{
  GradeResult result;

  try
  {
    // Resolve API key
    std::string api_key = config.embedding_api_key;
    if (api_key.empty())
    {
      if (const char* env_val = std::getenv("OPENAI_API_KEY"))
      {
        api_key = env_val;
      }
    }

    // Get embeddings for both texts using OpenAI embedding API
    nlohmann::json payload;
    payload["model"] = config.embedding_model;
    payload["input"] = nlohmann::json::array({expected, actual});

    const std::string response_str = llm::http_post_json(
        "https://api.openai.com/v1/embeddings",
        payload.dump(),
        {"Content-Type: application/json", "Authorization: Bearer " + api_key});

    const auto response_json = nlohmann::json::parse(response_str);

    if (!response_json.contains("data") || response_json["data"].size() < 2)
    {
      result.passed = false;
      result.explanation = "Failed to get embeddings";
      return result;
    }

    // Compute cosine similarity
    const auto& emb_expected = response_json["data"][0]["embedding"];
    const auto& emb_actual = response_json["data"][1]["embedding"];

    double dot = 0.0, norm_a = 0.0, norm_b = 0.0;
    for (std::size_t i = 0; i < emb_expected.size(); ++i)
    {
      double a = emb_expected[i].get<double>();
      double b = emb_actual[i].get<double>();
      dot += a * b;
      norm_a += a * a;
      norm_b += b * b;
    }

    double similarity = (norm_a > 0.0 && norm_b > 0.0)
                             ? dot / (std::sqrt(norm_a) * std::sqrt(norm_b))
                             : 0.0;

    result.passed = similarity >= config.semantic_threshold;
    result.explanation = "Cosine similarity: " + std::to_string(similarity) +
                         " (threshold: " + std::to_string(config.semantic_threshold) + ")";

    // Extract usage
    if (response_json.contains("usage"))
    {
      result.usage.prompt_tokens = response_json["usage"].value("prompt_tokens", std::size_t{0});
      result.usage.total_tokens = response_json["usage"].value("total_tokens", std::size_t{0});
    }
  }
  catch (const std::exception& e)
  {
    result.passed = false;
    result.explanation = std::string("Semantic match error: ") + e.what();
  }

  return result;
}

}  // namespace

GradeResult grade(const std::string& expected, const std::string& actual,
                  const std::string& grader, const GraderConfig& config)
{
  if (grader == "exact_match")
  {
    return grade_exact_match(expected, actual);
  }
  if (grader == "contains")
  {
    return grade_contains(expected, actual);
  }
  if (grader == "regex")
  {
    return grade_regex(expected, actual);
  }
  if (grader == "llm_judge")
  {
    return grade_llm_judge(expected, actual, config);
  }
  if (grader == "semantic_match")
  {
    return grade_semantic_match(expected, actual, config);
  }

  GradeResult result;
  result.passed = false;
  result.explanation = "Unknown grader: " + grader;
  return result;
}

}  // namespace neamc::eval
