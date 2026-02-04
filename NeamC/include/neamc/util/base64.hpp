// SPDX-License-Identifier: Apache-2.0
//
// Neam Utilities - Base64 encoding/decoding
//

#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace neamc::util
{
std::string base64_encode(const std::vector<uint8_t>& data);
std::string base64_encode(const std::string& data);
std::vector<uint8_t> base64_decode(const std::string& input);
}  // namespace neamc::util
