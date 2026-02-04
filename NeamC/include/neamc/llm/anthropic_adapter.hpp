// SPDX-License-Identifier: Apache-2.0
//
// Neam LLM - Anthropic Claude adapter
//

#pragma once

#include <memory>

#include "neamc/llm/provider.hpp"

namespace neamc::llm
{
std::unique_ptr<LLMProvider> create_anthropic_provider(const ProviderConfig& config);
}  // namespace neamc::llm
