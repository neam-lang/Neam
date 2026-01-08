//
// Neam LLM - Ollama adapter
//

#pragma once

#include <memory>

#include "neamc/llm/provider.hpp"

namespace neamc::llm
{
std::unique_ptr<LLMProvider> create_ollama_provider(const ProviderConfig& config);
}  // namespace neamc::llm
