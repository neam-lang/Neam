// SPDX-License-Identifier: Apache-2.0
//
// Neam LLM - Azure OpenAI adapter (v0.6.0)
//

#pragma once

#include <memory>

#include "neamc/llm/provider.hpp"

namespace neamc::llm
{
std::unique_ptr<LLMProvider> create_azure_openai_provider(const ProviderConfig& config);
}  // namespace neamc::llm
