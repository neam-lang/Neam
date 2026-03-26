//
// Neam v0.9.2 — Phase 9: Stored Procedure Translator implementation
//

#include "sp_translator.hpp"
#include "audit_trail.hpp"

#include <algorithm>
#include <regex>
#include <sstream>

namespace neamc::vm::migration {

SPTranslator::SPTranslator(ObjMigrationAgent* agent) : agent_(agent) {}

SPComplexity SPTranslator::classify(const std::string& source_code) {
  int lines = static_cast<int>(
      std::count(source_code.begin(), source_code.end(), '\n') + 1);

  int complexity_score = 0;
  if (source_code.find("CURSOR") != std::string::npos)                complexity_score += 1;
  if (source_code.find("EXECUTE IMMEDIATE") != std::string::npos)     complexity_score += 2;
  if (source_code.find("REF CURSOR") != std::string::npos)            complexity_score += 2;
  if (source_code.find("DBMS_") != std::string::npos)                 complexity_score += 3;
  if (source_code.find("AUTONOMOUS_TRANSACTION") != std::string::npos) complexity_score += 3;
  if (source_code.find("PIPELINED") != std::string::npos)             complexity_score += 3;
  if (source_code.find("BULK COLLECT") != std::string::npos)          complexity_score += 1;

  if (lines <= 50 && complexity_score == 0)  return SPComplexity::SIMPLE;
  if (lines <= 200 && complexity_score <= 3) return SPComplexity::MEDIUM;
  return SPComplexity::COMPLEX;
}

SPTranslationResult SPTranslator::translate(
    const std::string& sp_name, const std::string& source_code) {
  SPTranslationResult result;
  result.sp_name = sp_name;
  result.source_code = source_code;
  result.complexity = classify(source_code);

  audit_log(agent_, "sp_translate_start", sp_name,
            "Translating stored procedure",
            {{"complexity", result.complexity == SPComplexity::SIMPLE ? "SIMPLE"
                          : result.complexity == SPComplexity::MEDIUM ? "MEDIUM"
                          : "COMPLEX"}});

  std::string source_platform = agent_->schema_translation_config.value("source_platform", "oracle");
  std::string target_platform = agent_->schema_translation_config.value("target_platform", "snowflake");

  switch (result.complexity) {
    case SPComplexity::SIMPLE:
      // Tier 1: Rule-based (deterministic, no LLM)
      result.target_code = rule_based_translate(source_code);
      result.requires_human_review = false;
      break;

    case SPComplexity::MEDIUM:
      // Tier 2: LLM-assisted with compile-and-test loop
      result.target_code = llm_translate(source_code, source_platform, target_platform, 3);
      result.requires_human_review = false;
      break;

    case SPComplexity::COMPLEX:
      // Tier 3: LLM draft + human review required
      result.target_code = llm_translate(source_code, source_platform, target_platform, 5);
      result.requires_human_review = true;
      break;
  }

  // Compile check
  auto compile_result = compile_check(result.target_code);
  result.compile_validated = compile_result.success;
  if (!compile_result.success) {
    result.warnings.push_back("Compilation failed: " + compile_result.error_message);
    result.requires_human_review = true;
  }

  audit_log(agent_, "sp_translate_complete", sp_name,
            result.compile_validated ? "Translation compiled successfully"
                                     : "Translation requires review",
            {{"requires_review", result.requires_human_review},
             {"iterations", result.llm_iterations}});

  return result;
}

std::vector<SPTranslationResult> SPTranslator::translate_all() {
  std::vector<SPTranslationResult> results;

  if (!agent_->schema_map_obj) return results;

  for (const auto& [sp_name, source_code] : agent_->schema_map_obj->sp_translations) {
    if (source_code.empty()) continue;
    results.push_back(translate(sp_name, source_code));
  }

  // Summary
  int simple = 0, medium = 0, complex = 0, review_needed = 0;
  for (const auto& r : results) {
    if (r.complexity == SPComplexity::SIMPLE) simple++;
    else if (r.complexity == SPComplexity::MEDIUM) medium++;
    else complex++;
    if (r.requires_human_review) review_needed++;
  }

  audit_log(agent_, "sp_translate_all_complete", agent_name_str(agent_),
            "All stored procedures translated",
            {{"total", static_cast<int>(results.size())},
             {"simple", simple}, {"medium", medium}, {"complex", complex},
             {"requires_review", review_needed}});

  return results;
}

bool SPTranslator::compile_on_target(const std::string& target_code) {
  return compile_check(target_code).success;
}

// ─── Tier 1: Rule-based translation ─────────────────────────────────

std::string SPTranslator::rule_based_translate(const std::string& source_code) {
  std::string source = agent_->schema_translation_config.value("source_platform", "oracle");
  std::string target = agent_->schema_translation_config.value("target_platform", "snowflake");

  if (source == "oracle" && target == "snowflake") {
    return translate_plsql_to_snowflake_scripting(source_code);
  }

  // Default: return source with a comment
  return "-- Auto-translated from " + source + " to " + target + "\n" + source_code;
}

std::string SPTranslator::translate_plsql_to_snowflake_scripting(const std::string& code) {
  std::string result = code;

  // PL/SQL → Snowflake Scripting rule-based transformations
  // Type mappings
  auto replace_all = [](std::string& str, const std::string& from, const std::string& to) {
    size_t pos = 0;
    while ((pos = str.find(from, pos)) != std::string::npos) {
      str.replace(pos, from.length(), to);
      pos += to.length();
    }
  };

  replace_all(result, "VARCHAR2",          "VARCHAR");
  replace_all(result, "PLS_INTEGER",       "INTEGER");
  replace_all(result, "ELSIF",             "ELSEIF");
  replace_all(result, "SYSDATE",           "CURRENT_TIMESTAMP()");
  replace_all(result, "FROM DUAL",         "");
  replace_all(result, "WHEN OTHERS THEN",  "WHEN OTHER THEN");

  // Function mappings (using regex for precision)
  result = std::regex_replace(result, std::regex(R"(NVL\(([^,]+),\s*([^)]+)\))"),
                              "COALESCE($1, $2)");
  result = std::regex_replace(result, std::regex(R"(NVL2\(([^,]+),\s*([^,]+),\s*([^)]+)\))"),
                              "IFF($1 IS NOT NULL, $2, $3)");
  result = std::regex_replace(result, std::regex(R"(TO_CHAR\(([^)]+)\))"),
                              "TO_VARCHAR($1)");
  result = std::regex_replace(result, std::regex(R"(ADD_MONTHS\(([^,]+),\s*([^)]+)\))"),
                              "DATEADD('MONTH', $2, $1)");
  result = std::regex_replace(result, std::regex(R"(MONTHS_BETWEEN\(([^,]+),\s*([^)]+)\))"),
                              "DATEDIFF('MONTH', $2, $1)");
  result = std::regex_replace(result, std::regex(R"(TRUNC\(([^)]+)\))"),
                              "DATE_TRUNC('DAY', $1)");
  result = std::regex_replace(result, std::regex(R"(RAISE_APPLICATION_ERROR\([^)]+\))"),
                              "RAISE");
  result = std::regex_replace(result, std::regex(R"(ROWNUM)"),
                              "ROW_NUMBER() OVER ()");

  return result;
}

// ─── Tier 2/3: LLM-assisted translation ────────────────────────────

std::string SPTranslator::llm_translate(
    const std::string& source_code,
    const std::string& source_dialect,
    const std::string& target_dialect,
    int max_iterations) {

  std::string prompt =
      "Translate the following " + source_dialect + " stored procedure to "
      + target_dialect + ".\n\nRequirements:\n"
      "- Preserve exact business logic\n"
      "- Handle NULL semantics correctly\n"
      "- Replace platform-specific functions with equivalents\n"
      "- Return ONLY the translated code\n\n"
      "Source:\n```\n" + source_code + "\n```";

  std::string translated = invoke_llm(prompt);

  for (int iter = 0; iter < max_iterations; iter++) {
    auto result = compile_check(translated);
    if (result.success) return translated;

    // Fix compilation error via LLM
    translated = llm_fix_compilation_error(translated, result);
    agent_->total_cost += estimate_llm_cost(translated.length());
  }

  return translated;  // Max iterations reached — marked for human review
}

CompileResult SPTranslator::compile_check(const std::string& target_code) {
  // Simulated: in production, submits to target database for compilation
  CompileResult result;
  result.success = true;  // Simulated success
  return result;
}

std::string SPTranslator::llm_fix_compilation_error(
    const std::string& code, const CompileResult& error) {
  std::string fix_prompt =
      "The following code has a compilation error.\n"
      "Error: " + error.error_message + " (line " +
      std::to_string(error.error_line) + ")\n\n"
      "Code:\n```\n" + code + "\n```\n\n"
      "Fix the error. Return corrected code only.";

  return invoke_llm(fix_prompt);
}

std::string SPTranslator::invoke_llm(const std::string& prompt) {
  // Simulated: in production, calls the agent's LLM provider
  audit_log(agent_, "sp_llm_invoke", agent_name_str(agent_),
            "LLM invocation for SP translation",
            {{"prompt_length", static_cast<int>(prompt.length())}});
  return "-- LLM-translated (simulated)\n" + prompt.substr(prompt.rfind("```") + 3);
}

double SPTranslator::estimate_llm_cost(size_t token_count) {
  // Rough cost estimate: $0.01 per 1K tokens
  return static_cast<double>(token_count) / 1000.0 * 0.01;
}

}  // namespace neamc::vm::migration
