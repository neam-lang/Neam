// SPDX-License-Identifier: Apache-2.0
//
// Neam LLM - Provider factory
//

#pragma once

#include <memory>
#include <string>

#include "neamc/llm/provider.hpp"

namespace neamc::llm
{
std::unique_ptr<LLMProvider> create_provider(const std::string& provider,
                                             const ProviderConfig& config);
}  // namespace neamc::llm
