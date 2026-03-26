//
// Neam v0.9.2 — Phase 9: Stored Procedure Translator
//

#pragma once

#include <string>
#include <vector>

#include "neamc/vm/migration_types.hpp"

namespace neamc::vm::migration {

enum class SPComplexity { SIMPLE, MEDIUM, COMPLEX };

struct CompileResult {
  bool success = false;
  std::string error_message;
  int error_line = 0;
};

struct SPTranslationResult {
  std::string sp_name;
  std::string source_code;
  std::string target_code;
  SPComplexity complexity;
  bool requires_human_review = false;
  bool compile_validated = false;
  bool test_validated = false;
  std::vector<std::string> warnings;
  std::vector<std::string> unsupported_features;
  int llm_iterations = 0;
  double translation_cost = 0.0;
};

class SPTranslator {
public:
  explicit SPTranslator(ObjMigrationAgent* agent);

  SPComplexity classify(const std::string& source_code);
  SPTranslationResult translate(const std::string& sp_name,
                                const std::string& source_code);
  std::vector<SPTranslationResult> translate_all();
  bool compile_on_target(const std::string& target_code);

private:
  ObjMigrationAgent* agent_;

  // Tier 1: Rule-based
  std::string rule_based_translate(const std::string& source_code);
  std::string translate_plsql_to_snowflake_scripting(const std::string& code);

  // Tier 2/3: LLM-assisted with compile-and-test loop
  std::string llm_translate(const std::string& source_code,
                            const std::string& source_dialect,
                            const std::string& target_dialect,
                            int max_iterations = 3);

  CompileResult compile_check(const std::string& target_code);
  std::string llm_fix_compilation_error(const std::string& code,
                                        const CompileResult& error);

  // Utilities
  std::string invoke_llm(const std::string& prompt);
  double estimate_llm_cost(size_t token_count);
};

}  // namespace neamc::vm::migration
