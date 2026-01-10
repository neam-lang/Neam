//
// Neam LLM - Provider interface
//

#pragma once

#include <memory>
#include <string>
#include <vector>

#include "neamc/vm/async/future.hpp"

namespace neamc::llm
{
struct Message
{
  std::string role;
  std::string content;
};

struct ProviderConfig
{
  std::string model;
  std::string endpoint;
  std::string api_key;
  double temperature{0.0};
  std::string default_host;
};

class LLMProvider
{
public:
  virtual ~LLMProvider() = default;
  virtual std::string complete(const std::string& prompt) = 0;
  virtual vm::async::Future<std::string> complete_async(const std::string& prompt) = 0;
  virtual std::string chat(const std::vector<Message>& messages) = 0;
};
}  // namespace neamc::llm
