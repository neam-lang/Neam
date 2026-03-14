//
// Neam v0.6.5 - Integration Tests (Compile + Run)
//
// End-to-end tests that compile Neam source through the Pipeline
// and execute it in the VirtualMachine.
//

#include "neamc/pipeline.hpp"
#include "neamc/vm/vm.hpp"

#include <gtest/gtest.h>
#include <string>

namespace
{
using namespace neamc;
using namespace neamc::vm;

// Helper: compile source and run, returning the result value
static Value compile_and_run(const std::string& source)
{
  Pipeline pipeline;
  auto unit = pipeline.compile(source, {});
  VirtualMachine vm;
  return vm.run(unit.chunk);
}

// ============================================================================
// Arithmetic Tests
// ============================================================================

TEST(CompileRunTest, IntegerAddition)
{
  auto result = compile_and_run("return 1 + 2;");
  ASSERT_TRUE(result.is_number());
  EXPECT_DOUBLE_EQ(result.as_number(), 3.0);
}

TEST(CompileRunTest, IntegerSubtraction)
{
  auto result = compile_and_run("return 10 - 3;");
  ASSERT_TRUE(result.is_number());
  EXPECT_DOUBLE_EQ(result.as_number(), 7.0);
}

TEST(CompileRunTest, IntegerMultiplication)
{
  auto result = compile_and_run("return 4 * 5;");
  ASSERT_TRUE(result.is_number());
  EXPECT_DOUBLE_EQ(result.as_number(), 20.0);
}

TEST(CompileRunTest, IntegerDivision)
{
  auto result = compile_and_run("return 15 / 3;");
  ASSERT_TRUE(result.is_number());
  EXPECT_DOUBLE_EQ(result.as_number(), 5.0);
}

TEST(CompileRunTest, NestedArithmetic)
{
  auto result = compile_and_run("return (2 + 3) * (10 - 4);");
  ASSERT_TRUE(result.is_number());
  EXPECT_DOUBLE_EQ(result.as_number(), 30.0);
}

TEST(CompileRunTest, NegativeNumbers)
{
  auto result = compile_and_run("return -5 + 3;");
  ASSERT_TRUE(result.is_number());
  EXPECT_DOUBLE_EQ(result.as_number(), -2.0);
}

// ============================================================================
// Variable Tests
// ============================================================================

TEST(CompileRunTest, LetBinding)
{
  auto result = compile_and_run("let x = 42; return x;");
  ASSERT_TRUE(result.is_number());
  EXPECT_DOUBLE_EQ(result.as_number(), 42.0);
}

TEST(CompileRunTest, VariableReassignment)
{
  auto result = compile_and_run("let x = 1; x = 2; return x;");
  ASSERT_TRUE(result.is_number());
  EXPECT_DOUBLE_EQ(result.as_number(), 2.0);
}

TEST(CompileRunTest, MultipleVariables)
{
  auto result = compile_and_run("let a = 10; let b = 20; return a + b;");
  ASSERT_TRUE(result.is_number());
  EXPECT_DOUBLE_EQ(result.as_number(), 30.0);
}

// ============================================================================
// String Tests
// ============================================================================

TEST(CompileRunTest, StringLiteral)
{
  Pipeline pipeline;
  auto unit = pipeline.compile("emit \"hello\"; return nil;", {});
  VirtualMachine vm;
  vm.run(unit.chunk);
  const auto& emitted = vm.emitted();
  ASSERT_EQ(emitted.size(), 1u);
  ASSERT_TRUE(is_obj_type(emitted[0], ObjType::OBJ_STRING));
  auto* str = as_string(emitted[0]);
  EXPECT_EQ(std::string(str->chars, str->length), "hello");
}

TEST(CompileRunTest, StringConcatenation)
{
  Pipeline pipeline;
  auto unit = pipeline.compile("emit \"hello\" + \" world\"; return nil;", {});
  VirtualMachine vm;
  vm.run(unit.chunk);
  const auto& emitted = vm.emitted();
  ASSERT_EQ(emitted.size(), 1u);
  ASSERT_TRUE(is_obj_type(emitted[0], ObjType::OBJ_STRING));
  auto* str = as_string(emitted[0]);
  EXPECT_EQ(std::string(str->chars, str->length), "hello world");
}

// ============================================================================
// Boolean and Nil Tests
// ============================================================================

TEST(CompileRunTest, BooleanTrue)
{
  auto result = compile_and_run("return true;");
  ASSERT_TRUE(result.is_bool());
  EXPECT_TRUE(result.as_bool());
}

TEST(CompileRunTest, BooleanFalse)
{
  auto result = compile_and_run("return false;");
  ASSERT_TRUE(result.is_bool());
  EXPECT_FALSE(result.as_bool());
}

TEST(CompileRunTest, NilLiteral)
{
  auto result = compile_and_run("return nil;");
  EXPECT_TRUE(result.is_nil());
}

// ============================================================================
// Comparison Tests
// ============================================================================

TEST(CompileRunTest, EqualityTrue)
{
  auto result = compile_and_run("return 1 == 1;");
  ASSERT_TRUE(result.is_bool());
  EXPECT_TRUE(result.as_bool());
}

TEST(CompileRunTest, EqualityFalse)
{
  auto result = compile_and_run("return 1 == 2;");
  ASSERT_TRUE(result.is_bool());
  EXPECT_FALSE(result.as_bool());
}

TEST(CompileRunTest, LessThan)
{
  auto result = compile_and_run("return 1 < 2;");
  ASSERT_TRUE(result.is_bool());
  EXPECT_TRUE(result.as_bool());
}

TEST(CompileRunTest, GreaterThan)
{
  auto result = compile_and_run("return 5 > 3;");
  ASSERT_TRUE(result.is_bool());
  EXPECT_TRUE(result.as_bool());
}

// ============================================================================
// Function Tests
// ============================================================================

TEST(CompileRunTest, SimpleFunctionCall)
{
  auto result = compile_and_run(
      "fun add(a, b) { return a + b; } return add(2, 3);");
  ASSERT_TRUE(result.is_number());
  EXPECT_DOUBLE_EQ(result.as_number(), 5.0);
}

TEST(CompileRunTest, RecursiveFunction)
{
  auto result = compile_and_run(R"(
    fun factorial(n) {
      if (n <= 1) { return 1; }
      return n * factorial(n - 1);
    }
    return factorial(5);
  )");
  ASSERT_TRUE(result.is_number());
  EXPECT_DOUBLE_EQ(result.as_number(), 120.0);
}

TEST(CompileRunTest, NestedFunctionCalls)
{
  auto result = compile_and_run(R"(
    fun double(x) { return x * 2; }
    fun add_one(x) { return x + 1; }
    return double(add_one(4));
  )");
  ASSERT_TRUE(result.is_number());
  EXPECT_DOUBLE_EQ(result.as_number(), 10.0);
}

// ============================================================================
// Control Flow Tests
// ============================================================================

TEST(CompileRunTest, IfTrue)
{
  auto result = compile_and_run("if (true) { return 1; } return 0;");
  ASSERT_TRUE(result.is_number());
  EXPECT_DOUBLE_EQ(result.as_number(), 1.0);
}

TEST(CompileRunTest, IfFalse)
{
  auto result = compile_and_run("if (false) { return 1; } return 0;");
  ASSERT_TRUE(result.is_number());
  EXPECT_DOUBLE_EQ(result.as_number(), 0.0);
}

TEST(CompileRunTest, IfElse)
{
  auto result = compile_and_run(
      "let x = 10; if (x > 5) { return 1; } else { return 0; }");
  ASSERT_TRUE(result.is_number());
  EXPECT_DOUBLE_EQ(result.as_number(), 1.0);
}

TEST(CompileRunTest, WhileLoop)
{
  auto result = compile_and_run(
      "let x = 0; while (x < 5) { x = x + 1; } return x;");
  ASSERT_TRUE(result.is_number());
  EXPECT_DOUBLE_EQ(result.as_number(), 5.0);
}

TEST(CompileRunTest, WhileLoopAccumulator)
{
  auto result = compile_and_run(R"(
    let sum = 0;
    let i = 1;
    while (i <= 10) {
      sum = sum + i;
      i = i + 1;
    }
    return sum;
  )");
  ASSERT_TRUE(result.is_number());
  EXPECT_DOUBLE_EQ(result.as_number(), 55.0);
}

// ============================================================================
// List Tests
// ============================================================================

TEST(CompileRunTest, ListLiteral)
{
  // Test list via indexing — direct return may not preserve object
  auto r0 = compile_and_run("let arr = [10, 20, 30,]; return arr[0];");
  ASSERT_TRUE(r0.is_number());
  EXPECT_DOUBLE_EQ(r0.as_number(), 10.0);
  auto r2 = compile_and_run("let arr = [10, 20, 30,]; return arr[2];");
  ASSERT_TRUE(r2.is_number());
  EXPECT_DOUBLE_EQ(r2.as_number(), 30.0);
}

TEST(CompileRunTest, ListIndexing)
{
  auto result = compile_and_run("let arr = [10, 20, 30,]; return arr[1];");
  ASSERT_TRUE(result.is_number());
  EXPECT_DOUBLE_EQ(result.as_number(), 20.0);
}

// ============================================================================
// Map Tests
// ============================================================================

TEST(CompileRunTest, MapLiteral)
{
  auto result = compile_and_run(
      "let m = {\"a\": 1, \"b\": 2,}; return m[\"a\"];");
  ASSERT_TRUE(result.is_number());
  EXPECT_DOUBLE_EQ(result.as_number(), 1.0);
}

TEST(CompileRunTest, MapStringKey)
{
  // Test map string value via emit — direct return may not preserve object
  Pipeline pipeline;
  auto unit = pipeline.compile(
      "let m = {\"key\": \"value\",}; emit m[\"key\"]; return nil;", {});
  VirtualMachine vm;
  vm.run(unit.chunk);
  const auto& emitted = vm.emitted();
  ASSERT_EQ(emitted.size(), 1u);
  ASSERT_TRUE(is_obj_type(emitted[0], ObjType::OBJ_STRING));
  auto* str = as_string(emitted[0]);
  EXPECT_EQ(std::string(str->chars, str->length), "value");
}

// ============================================================================
// Emit Tests
// ============================================================================

TEST(CompileRunTest, EmitCapturesValues)
{
  Pipeline pipeline;
  auto unit = pipeline.compile("emit 42; emit \"hello\"; return nil;", {});
  VirtualMachine vm;
  auto result = vm.run(unit.chunk);
  EXPECT_TRUE(result.is_nil());
  const auto& emitted = vm.emitted();
  ASSERT_EQ(emitted.size(), 2u);
  EXPECT_TRUE(emitted[0].is_number());
  EXPECT_DOUBLE_EQ(emitted[0].as_number(), 42.0);
  ASSERT_TRUE(is_obj_type(emitted[1], ObjType::OBJ_STRING));
  auto* str = as_string(emitted[1]);
  EXPECT_EQ(std::string(str->chars, str->length), "hello");
}

// ============================================================================
// Native Function Tests
// ============================================================================

TEST(CompileRunTest, PrintDoesNotThrow)
{
  auto result = compile_and_run("print(\"test\"); return nil;");
  EXPECT_TRUE(result.is_nil());
}

TEST(CompileRunTest, PrintMultipleArgs)
{
  auto result = compile_and_run("print(\"hello\", 42, true); return nil;");
  EXPECT_TRUE(result.is_nil());
}

// ============================================================================
// Compile Error Tests
// ============================================================================

TEST(CompileRunTest, SyntaxErrorThrows)
{
  Pipeline pipeline;
  EXPECT_THROW(pipeline.compile("let x = ;", {}), std::exception);
}

TEST(CompileRunTest, UnclosedStringThrows)
{
  Pipeline pipeline;
  EXPECT_THROW(pipeline.compile("return \"unterminated;", {}), std::exception);
}

// ============================================================================
// Skill / Agent Compile Tests
// ============================================================================

TEST(CompileRunTest, SkillDefinitionCompiles)
{
  Pipeline pipeline;
  auto unit = pipeline.compile(R"(
    skill Echo {
      description: "Echo input."
      params: { text: String }
      impl(text) { return text; }
    }
    return nil;
  )", {});
  VirtualMachine vm;
  auto result = vm.run(unit.chunk);
  EXPECT_TRUE(result.is_nil());
}

TEST(CompileRunTest, AgentWithSkillCompiles)
{
  Pipeline pipeline;
  auto unit = pipeline.compile(R"(
    skill Echo {
      description: "Echo."
      params: { text: String }
      impl(text) { return text; }
    }
    agent Bot {
      provider: "openai"
      model: "gpt-4"
      system: "You are helpful."
      skills: [Echo]
    }
    return nil;
  )", {});
  VirtualMachine vm;
  auto result = vm.run(unit.chunk);
  EXPECT_TRUE(result.is_nil());
}

// ============================================================================
// DataAgent Compile Tests (v0.9)
// ============================================================================

TEST(CompileRunTest, SchemaDeclarationCompiles)
{
  Pipeline pipeline;
  auto unit = pipeline.compile(R"(
    schema UserRecord {
      user_id: int @primary_key
      name: string @not_null
      email: string @unique
      score: float @range(0.0, 100.0)
    }
    return nil;
  )", {});
  VirtualMachine vm;
  auto result = vm.run(unit.chunk);
  EXPECT_TRUE(result.is_nil());
}

TEST(CompileRunTest, SourceDeclarationCompiles)
{
  Pipeline pipeline;
  auto unit = pipeline.compile(R"(
    source UserDB {
      type: "postgres"
      connection: "pg://localhost/users"
    }
    return nil;
  )", {});
  VirtualMachine vm;
  auto result = vm.run(unit.chunk);
  EXPECT_TRUE(result.is_nil());
}

TEST(CompileRunTest, SinkDeclarationCompiles)
{
  Pipeline pipeline;
  auto unit = pipeline.compile(R"(
    sink OutputWarehouse {
      type: "s3"
      connection: "s3://analytics-output/processed/"
      format: "parquet"
      write_mode: "append"
    }
    return nil;
  )", {});
  VirtualMachine vm;
  auto result = vm.run(unit.chunk);
  EXPECT_TRUE(result.is_nil());
}

TEST(CompileRunTest, QualityBlockCompiles)
{
  Pipeline pipeline;
  auto unit = pipeline.compile(R"(
    quality StrictQuality {
      completeness: 0.99
      on_violation: "warn"
    }
    return nil;
  )", {});
  VirtualMachine vm;
  auto result = vm.run(unit.chunk);
  EXPECT_TRUE(result.is_nil());
}

TEST(CompileRunTest, ComputeDeclarationCompiles)
{
  Pipeline pipeline;
  auto unit = pipeline.compile(R"(
    compute LocalEngine {
      engine: "local"
      config: { parallelism: 4 }
    }
    return nil;
  )", {});
  VirtualMachine vm;
  auto result = vm.run(unit.chunk);
  EXPECT_TRUE(result.is_nil());
}

TEST(CompileRunTest, GovernanceDeclarationCompiles)
{
  Pipeline pipeline;
  auto unit = pipeline.compile(R"(
    governance BasicPolicy {
      access: [
        { "role": "analyst", "permissions": ["read"] }
      ]
      retention: [
        { "classification": "pii", "max_days": 90, "action": "delete" }
      ]
    }
    return nil;
  )", {});
  VirtualMachine vm;
  auto result = vm.run(unit.chunk);
  EXPECT_TRUE(result.is_nil());
}

TEST(CompileRunTest, CatalogDeclarationCompiles)
{
  Pipeline pipeline;
  auto unit = pipeline.compile(R"(
    catalog DataCatalog {
      engine: "unity_catalog"
      discovery: true
    }
    return nil;
  )", {});
  VirtualMachine vm;
  auto result = vm.run(unit.chunk);
  EXPECT_TRUE(result.is_nil());
}

TEST(CompileRunTest, MinimalDataAgentCompiles)
{
  Pipeline pipeline;
  auto unit = pipeline.compile(R"(
    source Input {
      type: "s3"
      connection: "./data.csv"
      format: "csv"
    }

    sink Output {
      type: "s3"
      connection: "./output.parquet"
      format: "parquet"
    }

    data agent Processor {
      provider: "openai", model: "gpt-4o-mini",
      sources: [Input],
      sinks: [Output]
    }
    return nil;
  )", {});
  VirtualMachine vm;
  auto result = vm.run(unit.chunk);
  EXPECT_TRUE(result.is_nil());
}

TEST(CompileRunTest, DataAgentWithTransformCompiles)
{
  Pipeline pipeline;
  auto unit = pipeline.compile(R"(
    source SalesDB {
      type: "postgres"
      connection: "pg://localhost/sales"
    }

    sink AnalyticsOut {
      type: "s3"
      connection: "./analytics.parquet"
      format: "parquet"
      write_mode: "replace"
    }

    quality SalesQuality {
      completeness: 0.95
      on_violation: "warn"
    }

    data agent SalesProcessor {
      provider: "openai", model: "gpt-4o-mini",
      sources: [SalesDB],
      sinks: [AnalyticsOut],
      quality: SalesQuality,
      pipeline: {
        extract: [SalesDB],
        transform: [
          filter(column: "amount", op: ">", value: 0)
        ],
        load: [AnalyticsOut]
      }
    }
    return nil;
  )", {});
  VirtualMachine vm;
  auto result = vm.run(unit.chunk);
  EXPECT_TRUE(result.is_nil());
}

TEST(CompileRunTest, DataAgentWithGovernanceCompiles)
{
  Pipeline pipeline;
  auto unit = pipeline.compile(R"(
    source UserSource {
      type: "s3"
      connection: "./users.csv"
      format: "csv"
    }

    sink UserSink {
      type: "s3"
      connection: "./users_out.parquet"
      format: "parquet"
    }

    governance GDPR {
      access: [
        { "role": "admin", "permissions": ["read", "write", "delete"] },
        { "role": "analyst", "permissions": ["read"] }
      ]
      retention: [
        { "classification": "pii", "max_days": 365, "action": "anonymize" }
      ]
    }

    data agent PrivacyAwareETL {
      provider: "openai", model: "gpt-4o-mini",
      sources: [UserSource],
      sinks: [UserSink],
      governance: GDPR
    }
    return nil;
  )", {});
  VirtualMachine vm;
  auto result = vm.run(unit.chunk);
  EXPECT_TRUE(result.is_nil());
}

TEST(CompileRunTest, ContextualDataKeywordNoConflict)
{
  // 'data' is contextual — only a keyword before 'agent'
  auto result = compile_and_run(R"(
    let data = "some_value";
    return data;
  )");
  ASSERT_TRUE(result.is_string());
  auto* str = as_string(result);
  EXPECT_EQ(std::string(str->chars, str->length), "some_value");
}

TEST(CompileRunTest, DataAgentCoexistsWithPlainAgent)
{
  Pipeline pipeline;
  auto unit = pipeline.compile(R"(
    skill Echo {
      description: "Echo."
      params: { text: String }
      impl(text) { return text; }
    }

    agent Bot {
      provider: "openai"
      model: "gpt-4"
      system: "You are helpful."
      skills: [Echo]
    }

    source FileIn {
      type: "s3"
      connection: "./in.csv"
      format: "csv"
    }

    sink FileOut {
      type: "s3"
      connection: "./out.parquet"
      format: "parquet"
    }

    data agent Ingestor {
      provider: "openai", model: "gpt-4o-mini",
      sources: [FileIn],
      sinks: [FileOut]
    }
    return nil;
  )", {});
  VirtualMachine vm;
  auto result = vm.run(unit.chunk);
  EXPECT_TRUE(result.is_nil());
}

}  // namespace
