// SPDX-License-Identifier: Apache-2.0
//
// Neam Voice - Speech-to-Text provider interface
//

#pragma once

#include <memory>
#include <string>

namespace neamc::voice
{
struct STTConfig
{
  std::string provider;
  std::string model;
  std::string api_key;
  std::string endpoint;
  std::string language;
  std::string format;
};

class STTProvider
{
public:
  virtual ~STTProvider() = default;
  virtual std::string transcribe(const std::string& audio_path) = 0;
};

std::unique_ptr<STTProvider> create_stt_provider(const STTConfig& config);
}  // namespace neamc::voice
