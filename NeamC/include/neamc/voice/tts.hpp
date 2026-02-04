// SPDX-License-Identifier: Apache-2.0
//
// Neam Voice - Text-to-Speech provider interface
//

#pragma once

#include <memory>
#include <string>

namespace neamc::voice
{
struct TTSConfig
{
  std::string provider;
  std::string model;
  std::string voice;
  std::string api_key;
  std::string endpoint;
  std::string format;
  std::string speed;
  std::string instructions;
};

class TTSProvider
{
public:
  virtual ~TTSProvider() = default;
  virtual void synthesize(const std::string& text, const std::string& output_path) = 0;
};

std::unique_ptr<TTSProvider> create_tts_provider(const TTSConfig& config);
}  // namespace neamc::voice
