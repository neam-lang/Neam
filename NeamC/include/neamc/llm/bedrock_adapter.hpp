// SPDX-License-Identifier: Apache-2.0
//
// Neam LLM - AWS Bedrock adapter (v0.6.0)
//

#pragma once
#ifdef NEAM_BACKEND_AWS

#include <memory>

#include "neamc/llm/provider.hpp"

namespace neamc::llm
{
std::unique_ptr<LLMProvider> create_bedrock_provider(const ProviderConfig& config);
}  // namespace neamc::llm

#endif  // NEAM_BACKEND_AWS
