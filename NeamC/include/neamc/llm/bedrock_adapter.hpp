//
// Neam LLM - AWS Bedrock adapter
//

#pragma once

#include <memory>

#include "neamc/llm/provider.hpp"

namespace neamc::llm
{
std::unique_ptr<LLMProvider> create_bedrock_provider(const ProviderConfig& config);
}  // namespace neamc::llm
