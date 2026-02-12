//
// Neam v0.6.9 — Prompt Injection Defense (D3)
//
// Detects and blocks prompt injection attempts in tool arguments,
// user inputs, and retrieved knowledge context.
//

#pragma once

#include <string>
#include <vector>

namespace neamc::security {

enum class InjectionStrictness { Permissive, Moderate, Strict };

struct InjectionResult {
  double score;                       // 0.0 - 1.0
  std::vector<std::string> patterns;  // matched pattern names
  bool blocked;                       // true if score > threshold
};

class InjectionScanner {
public:
  explicit InjectionScanner(InjectionStrictness level = InjectionStrictness::Moderate);

  // Scan text for injection patterns
  InjectionResult scan(const std::string& text) const;

  // Wrap system prompt with boundary markers
  std::string wrap_system_prompt(const std::string& prompt, const std::string& nonce) const;

  // Tag retrieved RAG context to mark it as DATA
  std::string tag_retrieved_context(const std::string& context) const;

  InjectionStrictness level() const { return level_; }

private:
  struct Pattern {
    std::string name;
    std::string regex_str;
    double weight;
  };

  InjectionStrictness level_;
  std::vector<Pattern> patterns_;
  double threshold_;
};

// Parse strictness from string (for config)
InjectionStrictness parse_strictness(const std::string& value);

}  // namespace neamc::security
