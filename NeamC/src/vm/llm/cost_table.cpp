// SPDX-License-Identifier: Apache-2.0
//
// Neam LLM - Cost estimation table
//

#include "neamc/llm/cost_table.hpp"

#include <unordered_map>

namespace neamc::llm
{
namespace
{
const std::unordered_map<std::string, ModelPricing>& pricing_table()
{
  static const std::unordered_map<std::string, ModelPricing> table = {
      // OpenAI
      {"gpt-4o", {2.50, 10.00}},
      {"gpt-4o-mini", {0.15, 0.60}},
      {"gpt-4-turbo", {10.00, 30.00}},
      {"gpt-3.5-turbo", {0.50, 1.50}},
      {"o1", {15.00, 60.00}},
      {"o1-mini", {3.00, 12.00}},
      // Anthropic
      {"claude-sonnet-4-20250514", {3.00, 15.00}},
      {"claude-opus-4-20250514", {15.00, 75.00}},
      {"claude-3-5-sonnet-20241022", {3.00, 15.00}},
      {"claude-3-5-haiku-20241022", {0.80, 4.00}},
      {"claude-3-haiku-20240307", {0.25, 1.25}},
      // Google
      {"gemini-2.0-flash", {0.10, 0.40}},
      {"gemini-1.5-pro", {1.25, 5.00}},
      {"gemini-1.5-flash", {0.075, 0.30}},
  };
  return table;
}
}  // namespace

double estimate_cost(const std::string& model, const UsageInfo& usage)
{
  const auto& table = pricing_table();

  // Try exact match first
  auto it = table.find(model);
  if (it != table.end())
  {
    const auto& pricing = it->second;
    return (static_cast<double>(usage.prompt_tokens) * pricing.input_per_1m_tokens +
            static_cast<double>(usage.completion_tokens) * pricing.output_per_1m_tokens) /
           1000000.0;
  }

  // Try prefix match (e.g., "gpt-4o-2024-11-20" matches "gpt-4o")
  for (const auto& [key, pricing] : table)
  {
    if (model.find(key) == 0)
    {
      return (static_cast<double>(usage.prompt_tokens) * pricing.input_per_1m_tokens +
              static_cast<double>(usage.completion_tokens) * pricing.output_per_1m_tokens) /
             1000000.0;
    }
  }

  return 0.0;  // Unknown model (local/free)
}
}  // namespace neamc::llm
