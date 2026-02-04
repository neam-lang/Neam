// SPDX-License-Identifier: Apache-2.0
//
// Neam LLM - Cost estimation table
//

#pragma once

#include <cstddef>
#include <string>

#include "neamc/llm/provider.hpp"

namespace neamc::llm
{
struct ModelPricing
{
  double input_per_1m_tokens{0.0};
  double output_per_1m_tokens{0.0};
};

/// Returns estimated cost in USD for a given model and usage.
/// Returns 0.0 for unknown or local models.
double estimate_cost(const std::string& model, const UsageInfo& usage);
}  // namespace neamc::llm
