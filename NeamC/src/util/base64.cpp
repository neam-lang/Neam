// SPDX-License-Identifier: Apache-2.0
//
// Neam Utilities - Base64 encoding/decoding implementation
//

#include "neamc/util/base64.hpp"

#include <cctype>
#include <stdexcept>

namespace neamc::util
{
namespace
{
const std::string& base64_alphabet()
{
  static const std::string alphabet =
      "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
  return alphabet;
}
}  // namespace

std::string base64_encode(const std::vector<uint8_t>& data)
{
  const auto& alphabet = base64_alphabet();
  std::string out;
  out.reserve(((data.size() + 2) / 3) * 4);
  for (std::size_t i = 0; i < data.size(); i += 3)
  {
    const std::size_t remaining = data.size() - i;
    const uint32_t octet_a = data[i];
    const uint32_t octet_b = remaining > 1 ? data[i + 1] : 0;
    const uint32_t octet_c = remaining > 2 ? data[i + 2] : 0;
    const uint32_t triple = (octet_a << 16) | (octet_b << 8) | octet_c;

    out.push_back(alphabet[(triple >> 18) & 0x3F]);
    out.push_back(alphabet[(triple >> 12) & 0x3F]);
    out.push_back(remaining > 1 ? alphabet[(triple >> 6) & 0x3F] : '=');
    out.push_back(remaining > 2 ? alphabet[triple & 0x3F] : '=');
  }
  return out;
}

std::string base64_encode(const std::string& data)
{
  std::vector<uint8_t> bytes(data.begin(), data.end());
  return base64_encode(bytes);
}

std::vector<uint8_t> base64_decode(const std::string& input)
{
  const auto& alphabet = base64_alphabet();
  std::vector<int> table(256, -1);
  for (std::size_t i = 0; i < alphabet.size(); ++i)
  {
    table[static_cast<unsigned char>(alphabet[i])] = static_cast<int>(i);
  }
  std::vector<uint8_t> out;
  uint32_t buffer = 0;
  int bits_collected = 0;
  for (unsigned char c : input)
  {
    if (std::isspace(c))
    {
      continue;
    }
    if (c == '=')
    {
      break;
    }
    int val = table[c];
    if (val == -1)
    {
      throw std::runtime_error("Invalid base64 character");
    }
    buffer = (buffer << 6) | static_cast<uint32_t>(val);
    bits_collected += 6;
    if (bits_collected >= 8)
    {
      bits_collected -= 8;
      out.push_back(static_cast<uint8_t>((buffer >> bits_collected) & 0xFF));
    }
  }
  return out;
}
}  // namespace neamc::util
