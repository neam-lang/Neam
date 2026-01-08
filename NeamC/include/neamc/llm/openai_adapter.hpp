//
// Neam LLM - OpenAI adapter
//

#pragma once

#include <memory>

#include "neamc/llm/provider.hpp"

namespace neamc::llm
{
std::unique_ptr<LLMProvider> create_openai_provider(const ProviderConfig& config);
}  // namespace neamc::llm
