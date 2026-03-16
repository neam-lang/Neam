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

// ============================================================================
// v0.9.0 DataAgent Comprehensive Tests
// ============================================================================

TEST(CompileRunTest, DataAgentSchemaAllConstraints)
{
  Pipeline pipeline;
  auto unit = pipeline.compile(R"neam(
    schema CustomerSchema {
      id: string @primary_key,
      email: string @pattern("^[a-z]+@[a-z]+$") @unique,
      name: string @length(1, 200) @not_null,
      age: int @range(0, 150),
      balance: float @positive,
      status: string @enum(["active", "inactive"]),
      score: float @range(0.0, 100.0)
    }
    return nil;
  )neam", {});
  VirtualMachine vm;
  auto result = vm.run(unit.chunk);
  EXPECT_TRUE(result.is_nil());
}

TEST(CompileRunTest, DataAgentMultipleSourcesAndSinks)
{
  auto result = compile_and_run(R"neam(
    source PostgresDB {
      type: "postgres",
      connection: "pg://localhost/analytics"
    }
    source S3Raw {
      type: "s3",
      connection: "s3://data-lake/raw/",
      format: "parquet"
    }
    sink WarehouseOut {
      type: "s3",
      connection: "s3://output/processed/",
      format: "parquet",
      write_mode: "append"
    }
    sink ArchiveOut {
      type: "s3",
      connection: "s3://archive/",
      format: "csv",
      write_mode: "replace"
    }
    data agent MultiSourcePipeline {
      provider: "openai", model: "gpt-4o-mini",
      sources: [PostgresDB, S3Raw],
      sinks: [WarehouseOut, ArchiveOut]
    }
    return nil;
  )neam");
  EXPECT_TRUE(result.is_nil());
}

TEST(CompileRunTest, DataAgentWithQualityAndCompute)
{
  auto result = compile_and_run(R"neam(
    source Raw { type: "s3", connection: "./data.csv", format: "csv" }
    sink Out { type: "s3", connection: "./out.parquet", format: "parquet" }
    quality HighQuality {
      completeness: 0.999,
      on_violation: "warn"
    }
    compute Spark {
      engine: "spark",
      config: { parallelism: 16, memory: "4g" }
    }
    data agent QualityPipeline {
      provider: "openai", model: "gpt-4o-mini",
      sources: [Raw],
      sinks: [Out],
      quality: HighQuality,
      compute: { default: Spark, available: [Spark] }
    }
    return nil;
  )neam");
  EXPECT_TRUE(result.is_nil());
}

TEST(CompileRunTest, DataAgentWithFullPipeline)
{
  auto result = compile_and_run(R"neam(
    source SalesDB {
      type: "postgres",
      connection: "pg://localhost/sales"
    }
    sink Analytics {
      type: "s3",
      connection: "s3://analytics/",
      format: "parquet",
      write_mode: "replace"
    }
    quality SalesQuality {
      completeness: 0.95,
      on_violation: "warn"
    }
    data agent SalesPipeline {
      provider: "openai", model: "gpt-4o-mini",
      sources: [SalesDB],
      sinks: [Analytics],
      quality: SalesQuality,
      pipeline: {
        extract: [SalesDB],
        transform: [
          filter(column: "amount", op: ">", value: 0),
          filter(column: "status", op: "==", value: "active")
        ],
        load: [Analytics]
      }
    }
    return nil;
  )neam");
  EXPECT_TRUE(result.is_nil());
}

TEST(CompileRunTest, DataAgentGovernanceWithMultipleRoles)
{
  auto result = compile_and_run(R"neam(
    source UserDB { type: "postgres", connection: "pg://users" }
    sink UserOut { type: "s3", connection: "s3://users/", format: "parquet" }
    governance HIPAA {
      access: [
        { "role": "admin", "permissions": ["read", "write", "delete"] },
        { "role": "analyst", "permissions": ["read"] },
        { "role": "auditor", "permissions": ["read"] }
      ],
      retention: [
        { "classification": "phi", "max_days": 365, "action": "anonymize" },
        { "classification": "pii", "max_days": 90, "action": "delete" }
      ]
    }
    data agent ComplianceETL {
      provider: "openai", model: "gpt-4o-mini",
      sources: [UserDB],
      sinks: [UserOut],
      governance: HIPAA
    }
    return nil;
  )neam");
  EXPECT_TRUE(result.is_nil());
}

TEST(CompileRunTest, DataAgentCatalogIntegration)
{
  auto result = compile_and_run(R"neam(
    source S { type: "s3", connection: "s3://raw/", format: "csv" }
    sink Out { type: "s3", connection: "s3://processed/", format: "parquet" }
    catalog UnityCatalog {
      engine: "unity_catalog",
      discovery: true
    }
    data agent CatalogAware {
      provider: "openai", model: "gpt-4o-mini",
      sources: [S],
      sinks: [Out],
      catalog: UnityCatalog
    }
    return nil;
  )neam");
  EXPECT_TRUE(result.is_nil());
}

TEST(CompileRunTest, DataKeywordAsVariable)
{
  auto result = compile_and_run(R"neam(
    let data = 99;
    let result = data + 1;
    return result;
  )neam");
  ASSERT_TRUE(result.is_number());
  EXPECT_DOUBLE_EQ(result.as_number(), 100.0);
}

TEST(CompileRunTest, DataAgentWithSchemaValidation)
{
  auto result = compile_and_run(R"neam(
    schema OrderSchema {
      order_id: string @primary_key,
      amount: float @positive @not_null,
      customer: string @not_null
    }
    source OrderDB { type: "postgres", connection: "pg://orders" }
    sink OrderOut { type: "s3", connection: "s3://orders/", format: "parquet" }
    data agent OrderProcessor {
      provider: "openai", model: "gpt-4o-mini",
      sources: [OrderDB],
      sinks: [OrderOut],
      schema: OrderSchema
    }
    return nil;
  )neam");
  EXPECT_TRUE(result.is_nil());
}

// ============================================================================
// v0.9.1 ETL Agent Comprehensive Tests
// ============================================================================

TEST(CompileRunTest, ETLAgentMinimal)
{
  auto result = compile_and_run(R"neam(
    compute SnowflakeWH { engine: "snowflake" }
    source RawData { type: "postgres", connection: "host=localhost" }
    etl agent BasicETL {
      provider: "openai",
      model: "gpt-4o",
      sources: [RawData],
      warehouse: SnowflakeWH
    }
    return nil;
  )neam");
  EXPECT_TRUE(result.is_nil());
}

TEST(CompileRunTest, ETLAgentFullConfiguration)
{
  auto result = compile_and_run(R"neam(
    compute SnowflakeWH { engine: "snowflake" }
    source RawData { type: "postgres", connection: "host=localhost" }
    semantic SalesMetrics {
      metrics: {
        total_revenue: {
          sql: "SUM(amount)",
          description: "Total revenue across all sales",
          type: "measure"
        },
        avg_order_size: {
          sql: "AVG(amount)",
          description: "Average order value",
          type: "measure"
        }
      },
      entities: {
        Customer: {
          table: "dim_customer",
          key: "customer_id"
        },
        Product: {
          table: "dim_product",
          key: "product_id"
        }
      },
      relationships: [
        { from: "Order", to: "Customer", type: "many_to_one", join: "order.customer_id = customer.id" }
      ],
      synonyms: {
        "sales": "total_revenue",
        "income": "total_revenue"
      },
      time_intelligence: {
        fiscal_year_start: "April",
        week_start: "Monday",
        default_timezone: "UTC"
      }
    }
    etl agent FullETL {
      provider: "openai",
      model: "gpt-4o",
      sources: [RawData],
      warehouse: SnowflakeWH,
      model_type: "star",
      semantic: SalesMetrics,
      role: "ETL Engineer",
      purpose: "Transform and load sales data",
      autonomy: "supervised",
      lineage: true,
      on_failure: "retry",
      incremental: {
        strategy: "timestamp",
        key: "updated_at",
        lookback: "3d"
      },
      self_heal: true,
      auto_model: {
        enabled: true,
        methodology: "kimball",
        approval: "manual"
      },
      layers: {
        staging: {
          prefix: "stg_",
          materialization: "view"
        },
        integration: {
          prefix: "int_",
          materialization: "table"
        },
        marts: [
          mart SalesMart {
            facts: ["fact_sales"],
            dimensions: ["dim_customer", "dim_product"],
            grain: "one row per sale",
            measures: ["total_amount", "quantity"]
          }
        ]
      }
    }
    return nil;
  )neam");
  EXPECT_TRUE(result.is_nil());
}

TEST(CompileRunTest, ETLAgentMultipleMarts)
{
  auto result = compile_and_run(R"neam(
    compute WH { engine: "snowflake" }
    source S { type: "postgres", connection: "x" }
    etl agent MultiMart {
      provider: "openai", model: "gpt-4o",
      sources: [S], warehouse: WH,
      layers: {
        marts: [
          mart SalesMart {
            facts: ["fact_sales"],
            dimensions: ["dim_customer", "dim_product"],
            grain: "per sale",
            measures: ["amount", "quantity"]
          },
          mart InventoryMart {
            facts: ["fact_inventory"],
            dimensions: ["dim_warehouse", "dim_product"],
            grain: "per warehouse per product",
            measures: ["stock_level", "reorder_point"]
          }
        ]
      }
    }
    return nil;
  )neam");
  EXPECT_TRUE(result.is_nil());
}

TEST(CompileRunTest, ETLAgentWithSCD)
{
  auto result = compile_and_run(R"neam(
    compute WH { engine: "snowflake" }
    source S { type: "postgres", connection: "x" }
    etl agent SCDPipeline {
      provider: "openai", model: "gpt-4o",
      sources: [S], warehouse: WH,
      layers: {
        marts: [
          mart CustomerMart {
            facts: ["fact_orders"],
            dimensions: ["dim_customer", "dim_address"],
            grain: "per order",
            measures: ["total_amount"],
            scd: {
              dim_customer: "type2",
              dim_address: "type1"
            }
          }
        ]
      }
    }
    return nil;
  )neam");
  EXPECT_TRUE(result.is_nil());
}

TEST(CompileRunTest, ETLKeywordAsVariable)
{
  auto result = compile_and_run(R"neam(
    let etl = "extract_transform_load";
    return etl;
  )neam");
  ASSERT_TRUE(result.is_string());
  auto* str = as_string(result);
  EXPECT_EQ(std::string(str->chars, str->length), "extract_transform_load");
}

TEST(CompileRunTest, ETLAgentMissingProviderFails)
{
  Pipeline pipeline;
  EXPECT_THROW(pipeline.compile(R"neam(
    compute WH { engine: "snowflake" }
    source S { type: "postgres", connection: "x" }
    etl agent Bad {
      model: "gpt-4o",
      sources: [S], warehouse: WH
    }
  )neam", {}), std::exception);
}

TEST(CompileRunTest, ETLAgentMissingWarehouseFails)
{
  Pipeline pipeline;
  EXPECT_THROW(pipeline.compile(R"neam(
    source S { type: "postgres", connection: "x" }
    etl agent Bad {
      provider: "openai", model: "gpt-4o",
      sources: [S]
    }
  )neam", {}), std::exception);
}

TEST(CompileRunTest, ETLAgentMissingSourcesFails)
{
  Pipeline pipeline;
  EXPECT_THROW(pipeline.compile(R"neam(
    compute WH { engine: "snowflake" }
    etl agent Bad {
      provider: "openai", model: "gpt-4o",
      warehouse: WH
    }
  )neam", {}), std::exception);
}

TEST(CompileRunTest, SemanticLayerCompiles)
{
  auto result = compile_and_run(R"neam(
    semantic FinanceMetrics {
      metrics: {
        gross_margin: {
          sql: "SUM(revenue) - SUM(cost)",
          description: "Gross margin",
          type: "measure"
        },
        net_revenue: {
          sql: "SUM(revenue) - SUM(discounts)",
          description: "Net revenue after discounts",
          type: "measure"
        }
      }
    }
    return nil;
  )neam");
  EXPECT_TRUE(result.is_nil());
}

TEST(CompileRunTest, SemanticLayerMissingMetricsFails)
{
  Pipeline pipeline;
  EXPECT_THROW(pipeline.compile(R"neam(
    semantic EmptyLayer {
    }
  )neam", {}), std::exception);
}

TEST(CompileRunTest, SemanticLayerMissingMetricSqlFails)
{
  Pipeline pipeline;
  EXPECT_THROW(pipeline.compile(R"neam(
    semantic BadLayer {
      metrics: {
        revenue: {
          description: "Revenue",
          type: "measure"
        }
      }
    }
  )neam", {}), std::exception);
}

// ============================================================================
// v0.9.2 Migration Agent Comprehensive Tests
// ============================================================================

TEST(CompileRunTest, MigrationAgentMinimal)
{
  auto result = compile_and_run(R"neam(
    budget MigBudget { cost: 10.0 }
    migration agent SimpleMig {
      provider: "openai",
      model: "gpt-4o-mini",
      source: OracleDB,
      target: SnowflakeDB,
      staging: S3Staging,
      budget: MigBudget,
      strategy: "re_platform"
    }
    return nil;
  )neam");
  EXPECT_TRUE(result.is_nil());
}

TEST(CompileRunTest, MigrationAgentFullDeclaration)
{
  auto result = compile_and_run(R"neam(
    budget MigBudget { cost: 100.0 }
    migration agent FullMig {
      provider: "openai",
      model: "gpt-4o",
      system: "You are a migration orchestrator.",
      temperature: 0.3,
      budget: MigBudget,
      source: OracleDB,
      target: SnowflakeDB,
      staging: S3Staging,
      strategy: "re_architecture",
      waves: {
        mode: "auto",
        max_tables_per_wave: 25,
        max_parallel_extractions: 8
      },
      movement: {
        strategy: "incremental",
        extraction_threads: 8,
        load_threads: 8,
        staging_format: "parquet",
        cdc: {
          mechanism: "log_based",
          tool: "debezium",
          lag_threshold: "5m",
          lag_critical: "30m"
        }
      },
      schema_translation: {
        type_mapping: "auto",
        stored_procedures: "llm_translate",
        views: "translate_and_validate",
        indexes: "re_evaluate"
      },
      validation: {
        mode: "exhaustive",
        reconciliation: {
          row_counts: true,
          column_aggregates: true,
          hash_comparison: "critical_tables"
        },
        tolerances: {
          financial_columns: "exact",
          floating_point: 0.000001,
          timestamp_precision: "1ms"
        }
      },
      cutover: {
        strategy: "blue_green",
        rollback: {
          window: "72h",
          auto_trigger: true,
          trigger_conditions: ["validation_failure_rate > 0.001"]
        }
      },
      self_heal: {
        enabled: true,
        auto_remediate: {
          missing_rows: true,
          duplicate_rows: true,
          type_conversion_errors: true,
          network_failures: true,
          checkpoint_resume: true
        },
        guardrails: {
          max_auto_fix_rows: 500,
          max_auto_fix_percentage: 0.001,
          max_retries_per_table: 5,
          require_dry_run: true,
          audit_all_remediations: true
        }
      },
      assessment: {
        auto_discover: true,
        profile_data: true,
        risk_analysis: true,
        report_format: "html"
      },
      governance: {
        preserve_classification: true,
        pii_detection: true,
        log_all_sql: true,
        log_all_data_movement: true,
        audit_retention: "7y"
      }
    }
    return nil;
  )neam");
  EXPECT_TRUE(result.is_nil());
}

TEST(CompileRunTest, MigrationAgentStrategyLiftAndShift)
{
  auto result = compile_and_run(R"neam(
    budget B { cost: 1.0 }
    migration agent M {
      provider: "openai", model: "gpt-4o-mini",
      source: S, target: T, staging: St,
      budget: B,
      strategy: "lift_and_shift"
    }
    return nil;
  )neam");
  EXPECT_TRUE(result.is_nil());
}

TEST(CompileRunTest, MigrationAgentStrategyRePlatform)
{
  auto result = compile_and_run(R"neam(
    budget B { cost: 1.0 }
    migration agent M {
      provider: "openai", model: "gpt-4o-mini",
      source: S, target: T, staging: St,
      budget: B,
      strategy: "re_platform"
    }
    return nil;
  )neam");
  EXPECT_TRUE(result.is_nil());
}

TEST(CompileRunTest, MigrationAgentInvalidStrategyFails)
{
  Pipeline pipeline;
  EXPECT_THROW(pipeline.compile(R"neam(
    budget B { cost: 1.0 }
    migration agent M {
      provider: "openai", model: "gpt-4o-mini",
      source: S, target: T, staging: St,
      budget: B,
      strategy: "invalid_strategy"
    }
    return nil;
  )neam", {}), std::exception);
}

TEST(CompileRunTest, MigrationAgentMIG001_MissingSourceFails)
{
  Pipeline pipeline;
  EXPECT_THROW(pipeline.compile(R"neam(
    budget B { cost: 1.0 }
    migration agent M {
      provider: "openai", model: "gpt-4o-mini",
      target: T, staging: St,
      budget: B
    }
    return nil;
  )neam", {}), std::exception);
}

TEST(CompileRunTest, MigrationAgentMIG001_MissingTargetFails)
{
  Pipeline pipeline;
  EXPECT_THROW(pipeline.compile(R"neam(
    budget B { cost: 1.0 }
    migration agent M {
      provider: "openai", model: "gpt-4o-mini",
      source: S, staging: St,
      budget: B
    }
    return nil;
  )neam", {}), std::exception);
}

TEST(CompileRunTest, MigrationAgentMIG005_ZeroRollbackWindowFails)
{
  Pipeline pipeline;
  EXPECT_THROW(pipeline.compile(R"neam(
    budget B { cost: 1.0 }
    migration agent M {
      provider: "openai", model: "gpt-4o-mini",
      source: S, target: T, staging: St,
      budget: B,
      cutover: {
        strategy: "big_bang",
        rollback: { window: "0" }
      }
    }
    return nil;
  )neam", {}), std::exception);
}

TEST(CompileRunTest, MigrationAgentMIG006_NoRowCountsFails)
{
  Pipeline pipeline;
  EXPECT_THROW(pipeline.compile(R"neam(
    budget B { cost: 1.0 }
    migration agent M {
      provider: "openai", model: "gpt-4o-mini",
      source: S, target: T, staging: St,
      budget: B,
      validation: {
        reconciliation: {
          row_counts: false
        }
      }
    }
    return nil;
  )neam", {}), std::exception);
}

TEST(CompileRunTest, MigrationAgentMIG008_MissingBudgetFails)
{
  Pipeline pipeline;
  EXPECT_THROW(pipeline.compile(R"neam(
    migration agent M {
      provider: "openai", model: "gpt-4o-mini",
      source: S, target: T, staging: St
    }
    return nil;
  )neam", {}), std::exception);
}

TEST(CompileRunTest, MigrationAgentMIG009_NoStagingFullDumpFails)
{
  Pipeline pipeline;
  EXPECT_THROW(pipeline.compile(R"neam(
    budget B { cost: 1.0 }
    migration agent M {
      provider: "openai", model: "gpt-4o-mini",
      source: S, target: T,
      budget: B,
      movement: { strategy: "full_dump" }
    }
    return nil;
  )neam", {}), std::exception);
}

TEST(CompileRunTest, MigrationAgentMIG009_TrickleNoStagingOK)
{
  auto result = compile_and_run(R"neam(
    budget B { cost: 1.0 }
    migration agent M {
      provider: "openai", model: "gpt-4o-mini",
      source: S, target: T,
      budget: B,
      movement: { strategy: "trickle" }
    }
    return nil;
  )neam");
  EXPECT_TRUE(result.is_nil());
}

TEST(CompileRunTest, MigrationAgentMIG013_NonExactFinancialFails)
{
  Pipeline pipeline;
  EXPECT_THROW(pipeline.compile(R"neam(
    budget B { cost: 1.0 }
    migration agent M {
      provider: "openai", model: "gpt-4o-mini",
      source: S, target: T, staging: St,
      budget: B,
      validation: {
        tolerances: {
          financial_columns: "approximate"
        }
      }
    }
    return nil;
  )neam", {}), std::exception);
}

TEST(CompileRunTest, MigrationAgentMovementBlueGreen)
{
  auto result = compile_and_run(R"neam(
    budget B { cost: 1.0 }
    migration agent M {
      provider: "openai", model: "gpt-4o-mini",
      source: S, target: T,
      budget: B,
      movement: { strategy: "blue_green" }
    }
    return nil;
  )neam");
  EXPECT_TRUE(result.is_nil());
}

TEST(CompileRunTest, MigrationAgentMovementParallelRun)
{
  auto result = compile_and_run(R"neam(
    budget B { cost: 1.0 }
    migration agent M {
      provider: "openai", model: "gpt-4o-mini",
      source: S, target: T,
      budget: B,
      movement: { strategy: "parallel_run" }
    }
    return nil;
  )neam");
  EXPECT_TRUE(result.is_nil());
}

TEST(CompileRunTest, MigrationAgentCutoverCanary)
{
  auto result = compile_and_run(R"neam(
    budget B { cost: 1.0 }
    migration agent M {
      provider: "openai", model: "gpt-4o-mini",
      source: S, target: T, staging: St,
      budget: B,
      cutover: { strategy: "canary" }
    }
    return nil;
  )neam");
  EXPECT_TRUE(result.is_nil());
}

TEST(CompileRunTest, MigrationAgentCutoverTrickle)
{
  auto result = compile_and_run(R"neam(
    budget B { cost: 1.0 }
    migration agent M {
      provider: "openai", model: "gpt-4o-mini",
      source: S, target: T, staging: St,
      budget: B,
      cutover: { strategy: "trickle" }
    }
    return nil;
  )neam");
  EXPECT_TRUE(result.is_nil());
}

TEST(CompileRunTest, MigrationKeywordAsVariable)
{
  auto result = compile_and_run(R"neam(
    let migration = 42;
    return migration;
  )neam");
  ASSERT_TRUE(result.is_number());
  EXPECT_DOUBLE_EQ(result.as_number(), 42.0);
}

TEST(CompileRunTest, MigrationAgentWithCDCConfig)
{
  auto result = compile_and_run(R"neam(
    budget B { cost: 5.0 }
    migration agent CDCMig {
      provider: "openai", model: "gpt-4o-mini",
      source: OracleDB, target: SnowflakeDB, staging: S3,
      budget: B,
      movement: {
        strategy: "incremental",
        cdc: {
          mechanism: "log_based",
          tool: "debezium",
          lag_threshold: "5m",
          lag_critical: "30m"
        }
      }
    }
    return nil;
  )neam");
  EXPECT_TRUE(result.is_nil());
}

TEST(CompileRunTest, MigrationAgentWithSelfHeal)
{
  auto result = compile_and_run(R"neam(
    budget B { cost: 5.0 }
    migration agent HealingMig {
      provider: "openai", model: "gpt-4o-mini",
      source: S, target: T, staging: St,
      budget: B,
      self_heal: {
        enabled: true,
        auto_remediate: {
          missing_rows: true,
          duplicate_rows: true,
          type_conversion_errors: true,
          network_failures: true,
          checkpoint_resume: true
        },
        guardrails: {
          max_auto_fix_rows: 1000,
          max_auto_fix_percentage: 0.001,
          max_retries_per_table: 3,
          require_dry_run: false,
          audit_all_remediations: true
        }
      }
    }
    return nil;
  )neam");
  EXPECT_TRUE(result.is_nil());
}

TEST(CompileRunTest, MigrationAgentWithAssessment)
{
  auto result = compile_and_run(R"neam(
    budget B { cost: 5.0 }
    migration agent AssessMig {
      provider: "openai", model: "gpt-4o-mini",
      source: S, target: T, staging: St,
      budget: B,
      assessment: {
        auto_discover: true,
        profile_data: true,
        risk_analysis: true,
        report_format: "json"
      }
    }
    return nil;
  )neam");
  EXPECT_TRUE(result.is_nil());
}

TEST(CompileRunTest, MigrationAgentWithGovernance)
{
  auto result = compile_and_run(R"neam(
    budget B { cost: 5.0 }
    migration agent GovMig {
      provider: "openai", model: "gpt-4o-mini",
      source: S, target: T, staging: St,
      budget: B,
      governance: {
        preserve_classification: true,
        pii_detection: true,
        log_all_sql: true,
        log_all_data_movement: true,
        audit_retention: "10y"
      }
    }
    return nil;
  )neam");
  EXPECT_TRUE(result.is_nil());
}

TEST(CompileRunTest, MigrationAgentManualWaves)
{
  auto result = compile_and_run(R"neam(
    budget B { cost: 5.0 }
    migration agent WaveMig {
      provider: "openai", model: "gpt-4o-mini",
      source: S, target: T, staging: St,
      budget: B,
      waves: {
        mode: "manual",
        max_tables_per_wave: 10,
        max_parallel_extractions: 4
      }
    }
    return nil;
  )neam");
  EXPECT_TRUE(result.is_nil());
}

// ============================================================================
// Cross-Version Compatibility Tests
// ============================================================================

TEST(CompileRunTest, AllAgentTypesCoexist)
{
  auto result = compile_and_run(R"neam(
    skill Echo {
      description: "Echo input."
      params: { text: String }
      impl(text) { return text; }
    }

    agent ChatBot {
      provider: "openai",
      model: "gpt-4o-mini",
      system: "You are helpful.",
      skills: [Echo]
    }

    source RawCSV {
      type: "s3",
      connection: "s3://data/raw.csv",
      format: "csv"
    }

    sink ProcessedOut {
      type: "s3",
      connection: "s3://data/processed/",
      format: "parquet"
    }

    data agent DataPipeline {
      provider: "openai", model: "gpt-4o-mini",
      sources: [RawCSV],
      sinks: [ProcessedOut]
    }

    compute WH { engine: "snowflake" }

    etl agent ETLPipeline {
      provider: "openai", model: "gpt-4o",
      sources: [RawCSV],
      warehouse: WH
    }

    budget MigBudget { cost: 50.0 }

    migration agent MigPipeline {
      provider: "openai", model: "gpt-4o",
      source: OracleDB,
      target: SnowflakeDB,
      staging: S3Staging,
      budget: MigBudget,
      strategy: "re_platform"
    }

    return nil;
  )neam");
  EXPECT_TRUE(result.is_nil());
}

TEST(CompileRunTest, MixedKeywordsAsVariables)
{
  auto result = compile_and_run(R"neam(
    let data = 10;
    let etl = 20;
    let migration = 30;
    return data + etl + migration;
  )neam");
  ASSERT_TRUE(result.is_number());
  EXPECT_DOUBLE_EQ(result.as_number(), 60.0);
}

// ============================================================================
// Complex Expression + Agent Integration Tests
// ============================================================================

TEST(CompileRunTest, FunctionCallWithDataAgent)
{
  auto result = compile_and_run(R"neam(
    fun compute_total(a, b) { return a + b; }

    source S { type: "s3", connection: "s3://data/", format: "csv" }
    sink Out { type: "s3", connection: "s3://output/", format: "parquet" }

    data agent Proc {
      provider: "openai", model: "gpt-4o-mini",
      sources: [S],
      sinks: [Out]
    }

    return compute_total(10, 20);
  )neam");
  ASSERT_TRUE(result.is_number());
  EXPECT_DOUBLE_EQ(result.as_number(), 30.0);
}

TEST(CompileRunTest, ControlFlowWithMigrationAgent)
{
  auto result = compile_and_run(R"neam(
    budget B { cost: 5.0 }
    migration agent Mig {
      provider: "openai", model: "gpt-4o-mini",
      source: S, target: T, staging: St,
      budget: B
    }

    let status = "ready";
    if (status == "ready") {
      return true;
    }
    return false;
  )neam");
  ASSERT_TRUE(result.is_bool());
  EXPECT_TRUE(result.as_bool());
}

TEST(CompileRunTest, LoopWithETLAgent)
{
  auto result = compile_and_run(R"neam(
    compute WH { engine: "snowflake" }
    source S { type: "postgres", connection: "x" }
    etl agent E {
      provider: "openai", model: "gpt-4o",
      sources: [S], warehouse: WH
    }

    let total = 0;
    let i = 0;
    while (i < 5) {
      total = total + i;
      i = i + 1;
    }
    return total;
  )neam");
  ASSERT_TRUE(result.is_number());
  EXPECT_DOUBLE_EQ(result.as_number(), 10.0);
}

TEST(CompileRunTest, NestedFunctionsWithSchemaAndSource)
{
  auto result = compile_and_run(R"neam(
    schema InputSchema {
      id: int @primary_key,
      value: float @positive
    }

    source DB { type: "postgres", connection: "pg://localhost/db" }

    fun square(x) { return x * x; }
    fun sum_squares(a, b) { return square(a) + square(b); }

    return sum_squares(3, 4);
  )neam");
  ASSERT_TRUE(result.is_number());
  EXPECT_DOUBLE_EQ(result.as_number(), 25.0);
}

// ============================================================================
// Edge Cases and Error Handling
// ============================================================================

TEST(CompileRunTest, EmptyMovementBlock)
{
  auto result = compile_and_run(R"neam(
    budget B { cost: 1.0 }
    migration agent M {
      provider: "openai", model: "gpt-4o-mini",
      source: S, target: T, staging: St,
      budget: B,
      movement: {}
    }
    return nil;
  )neam");
  EXPECT_TRUE(result.is_nil());
}

TEST(CompileRunTest, EmptyValidationBlock)
{
  auto result = compile_and_run(R"neam(
    budget B { cost: 1.0 }
    migration agent M {
      provider: "openai", model: "gpt-4o-mini",
      source: S, target: T, staging: St,
      budget: B,
      validation: {}
    }
    return nil;
  )neam");
  EXPECT_TRUE(result.is_nil());
}

TEST(CompileRunTest, SchemaWithAllTypes)
{
  auto result = compile_and_run(R"neam(
    schema AllTypes {
      text_field: string,
      int_field: int,
      float_field: float,
      bool_field: bool
    }
    return nil;
  )neam");
  EXPECT_TRUE(result.is_nil());
}

TEST(CompileRunTest, MultipleSchemaDeclarations)
{
  auto result = compile_and_run(R"neam(
    schema Input {
      id: int @primary_key,
      name: string @not_null
    }
    schema Output {
      id: int @primary_key,
      processed_name: string @not_null,
      score: float @range(0.0, 1.0)
    }
    return nil;
  )neam");
  EXPECT_TRUE(result.is_nil());
}

TEST(CompileRunTest, GovernanceStandalone)
{
  auto result = compile_and_run(R"neam(
    governance SOX {
      access: [
        { "role": "auditor", "permissions": ["read"] }
      ],
      retention: [
        { "classification": "financial", "max_days": 2555, "action": "archive" }
      ]
    }
    return nil;
  )neam");
  EXPECT_TRUE(result.is_nil());
}

TEST(CompileRunTest, BudgetDeclaration)
{
  auto result = compile_and_run(R"neam(
    budget SmallBudget { cost: 0.50, tokens: 10000, time: 30000 }
    budget LargeBudget { cost: 100.0 }
    return nil;
  )neam");
  EXPECT_TRUE(result.is_nil());
}

// ============================================================================
// v0.9.3 DataOps Agent Integration Tests
// ============================================================================

TEST(CompileRunTest, DataOpsAgentFullIntegration)
{
  auto result = compile_and_run(R"neam(
    budget OpsBudget { cost: 100.0 }
    scheduler AirflowProd {
      type: "airflow",
      connection: "https://airflow.internal",
      poll_interval: "30s"
    }
    audit_table ETLAudit {
      source: AirflowProd,
      table: "etl_audit_log",
      column_map: {
        job_id: "dag_run_id",
        timestamp: "execution_date",
        status: "state",
        status_values: {
          success: ["success"],
          failure: ["failed"]
        }
      },
      poll_interval: "60s"
    }
    log_source SnowflakeLogs {
      type: "snowflake",
      connection: "snowflake://account.snowflakecomputing.com",
      views: ["QUERY_HISTORY"]
    }
    platform SnowflakePlatform {
      type: "snowflake",
      connection: "snowflake://account.snowflakecomputing.com",
      database: "ANALYTICS",
      health_checks: {
        storage_growth: true,
        warehouse_utilization: true
      },
      finops: {
        daily_budget: 500.0,
        auto_suspend_idle: "5m",
        auto_kill_queries: "30m",
        cost_anomaly_threshold: 1.5
      }
    }
    incident_policy OpsPolicy {
      severity: {
        P1_CRITICAL: {
          conditions: ["SLA breach", "data loss"],
          response: "immediate",
          escalation: "oncall_pager",
          channels: ["#incidents"]
        }
      },
      auto_heal: {
        enabled: true,
        max_auto_retries: 3,
        retry_backoff: "exponential",
        allowed_actions: ["retry_job", "restart_warehouse"]
      }
    }
    correlation PipelineCorrelation {
      scope: {
        schedulers: [AirflowProd],
        audit_tables: [ETLAudit],
        log_sources: [SnowflakeLogs]
      },
      time_window: "30m"
    }
    dataops agent DataOpsManager {
      provider: "openai",
      model: "gpt-4o",
      system: "You are a DataOps monitoring agent.",
      temperature: 0.3,
      budget: OpsBudget,
      platforms: [SnowflakePlatform],
      schedulers: [AirflowProd],
      audit_tables: [ETLAudit],
      log_sources: [SnowflakeLogs],
      correlations: [PipelineCorrelation],
      incident_policy: OpsPolicy,
      mode: "continuous",
      reports: {
        daily_digest: {
          time: "08:00",
          channel: "#data-ops"
        }
      }
    }

    let status = dataops_status(DataOpsManager);
    assert_eq(status, "idle");
    dataops_start_monitor(DataOpsManager);
    assert_true(dataops_is_monitoring(DataOpsManager));
    let id = dataops_create_incident(DataOpsManager, "P1", "SLA breach on fact_orders");
    assert_eq(dataops_incidents_open(DataOpsManager), 1);
    dataops_triage(DataOpsManager);
    dataops_investigate(DataOpsManager);
    dataops_remediate(DataOpsManager);
    assert_eq(dataops_remediations_today(DataOpsManager), 1);
    dataops_close_incident(DataOpsManager, id);
    assert_eq(dataops_incidents_open(DataOpsManager), 0);
    dataops_stop_monitor(DataOpsManager);
    assert_eq(dataops_status(DataOpsManager), "idle");
    return true;
  )neam");
  EXPECT_TRUE(result.is_bool());
  EXPECT_TRUE(result.as_bool());
}

// ============================================================================
// v0.9.4 Governance Agent Integration Tests
// ============================================================================

TEST(CompileRunTest, GovernanceAgentDefine)
{
  auto result = compile_and_run(R"neam(
    budget GovBudget { cost: 5.00 }

    classification_policy DataClassification {
      levels: {
        HIGH: { level: 3, controls: ["encrypt"] },
        LOW: { level: 1, controls: ["none"] }
      }
    }

    governance agent DataGovernor {
      provider: "openai",
      model: "gpt-4o",
      budget: GovBudget,
      classification: DataClassification
    }

    let status = governance_status(DataGovernor);
    return status == "idle";
  )neam");
  EXPECT_TRUE(result.is_bool());
  EXPECT_TRUE(result.as_bool());
}

TEST(CompileRunTest, GovernanceAgentScore)
{
  auto result = compile_and_run(R"neam(
    budget GovBudget { cost: 5.00 }

    classification_policy DataClassification {
      levels: {
        HIGH: { level: 3, controls: ["encrypt"] },
        LOW: { level: 1, controls: ["none"] }
      }
    }

    governance agent Gov {
      provider: "openai",
      model: "gpt-4o",
      budget: GovBudget,
      classification: DataClassification
    }

    governance_set_score(Gov, 0.85);
    return governance_score(Gov);
  )neam");
  EXPECT_TRUE(result.is_number());
  EXPECT_DOUBLE_EQ(result.as_number(), 0.85);
}

TEST(CompileRunTest, GovernanceAgentViolations)
{
  auto result = compile_and_run(R"neam(
    budget GovBudget { cost: 5.00 }

    classification_policy DataClassification {
      levels: {
        HIGH: { level: 3, controls: ["encrypt"] },
        LOW: { level: 1, controls: ["none"] }
      }
    }

    governance agent Gov {
      provider: "openai",
      model: "gpt-4o",
      budget: GovBudget,
      classification: DataClassification
    }

    governance_add_violation(Gov, "Missing encryption on PII column");
    governance_add_violation(Gov, "Stale dataset not archived");
    return governance_violations(Gov);
  )neam");
  EXPECT_TRUE(result.is_number());
  EXPECT_DOUBLE_EQ(result.as_number(), 2.0);
}

TEST(CompileRunTest, GovernanceAgentFullReport)
{
  auto result = compile_and_run(R"neam(
    budget GovBudget { cost: 5.00 }

    classification_policy DataClassification {
      levels: {
        HIGH: { level: 3, controls: ["encrypt"] },
        LOW: { level: 1, controls: ["none"] }
      }
    }

    governance agent Gov {
      provider: "openai",
      model: "gpt-4o",
      budget: GovBudget,
      classification: DataClassification
    }

    governance_set_score(Gov, 0.92);
    let report = governance_full_report(Gov);
    return report != nil;
  )neam");
  EXPECT_TRUE(result.is_bool());
  EXPECT_TRUE(result.as_bool());
}

TEST(CompileRunTest, GovernancePoliciesDefine)
{
  auto result = compile_and_run(R"neam(
    access_policy DataAccess {
      model: "rbac",
      roles: {
        engineer: {
          description: "Engineering access",
          permissions: ["SELECT", "INSERT"],
          databases: ["ANALYTICS"]
        }
      }
    }

    quality_policy DataQuality {
      profiling: {
        enabled: true,
        scan_interval: "24h",
        sample_size: 10000
      }
    }

    lineage_policy DataLineage {
      auto_discover: {
        enabled: true,
        sources: ["sql_logs"],
        methods: ["sql_parsing"],
        scan_interval: "6h",
        depth: "column"
      }
    }

    compliance_policy Compliance {
      regulations: ["GDPR", "CCPA"],
      monitoring: {
        scan_interval: "24h",
        scoring: true,
        alert_on_non_compliance: true
      }
    }

    lifecycle_policy Lifecycle {
      retention: {
        RESTRICTED: {
          max_retention: "7y",
          min_retention: "1y",
          action_on_expiry: "archive"
        }
      }
    }

    return true;
  )neam");
  EXPECT_TRUE(result.is_bool());
  EXPECT_TRUE(result.as_bool());
}

TEST(CompileRunTest, GovernanceCrossVersionWithDataOps)
{
  auto result = compile_and_run(R"neam(
    budget OpsBudget { cost: 2.00 }
    budget GovBudget { cost: 5.00 }

    scheduler AirflowProd {
      type: "airflow",
      connection: "https://airflow.internal"
    }
    platform SnowflakeProd {
      type: "snowflake",
      connection: "snowflake://prod"
    }
    incident_policy OpsPolicy {
      severity: {
        P1_critical: { conditions: ["data_loss"], response: "immediate" }
      }
    }

    dataops agent OpsManager {
      provider: "openai",
      model: "gpt-4o",
      budget: OpsBudget,
      platforms: [SnowflakeProd],
      schedulers: [AirflowProd],
      incident_policy: OpsPolicy,
      mode: "continuous"
    }

    classification_policy DataClassification {
      levels: {
        RESTRICTED: { level: 4, controls: ["encrypt"] },
        PUBLIC: { level: 1, controls: ["none"] }
      }
    }

    governance agent DataGovernor {
      provider: "openai",
      model: "gpt-4o",
      budget: GovBudget,
      classification: DataClassification,
      coordinates_with: [OpsManager]
    }

    if (dataops_status(OpsManager) != "idle") { return false; }
    if (governance_status(DataGovernor) != "idle") { return false; }
    return true;
  )neam");
  EXPECT_TRUE(result.is_bool());
  EXPECT_TRUE(result.as_bool());
}

// ============================================================================
// v0.9.5 Modeling Agent integration tests
// ============================================================================

TEST(CompileRunTest, ModelingAgentDefine)
{
  auto result = compile_and_run(R"neam(
    budget TestBudget { cost: 10.00, tokens: 100000 }

    schema_source TestSource {
      type: "snowflake",
      connection: "test_account",
      credentials: "test_creds",
      databases: ["TEST_DB"],
      scan_interval: "1h"
    }

    modeling agent TestArchitect {
      provider: "openai",
      model: "gpt-4o-mini",
      sources: [TestSource],
      capabilities: { reverse_engineer: true },
      budget: TestBudget
    }

    let status = modeling_status(TestArchitect);
    return status != nil;
  )neam");
  ASSERT_TRUE(result.is_bool());
  EXPECT_TRUE(result.as_bool());
}

TEST(CompileRunTest, ModelingAgentReport)
{
  auto result = compile_and_run(R"neam(
    schema_source TestSource {
      type: "postgres",
      connection: "test://conn"
    }

    modeling agent Reporter {
      provider: "openai",
      model: "gpt-4o-mini",
      sources: [TestSource]
    }

    return modeling_report(Reporter);
  )neam");
  ASSERT_TRUE(result.is_obj());
}

TEST(CompileRunTest, ModelingSchemaSourceAndEntity)
{
  auto result = compile_and_run(R"neam(
    schema_source TestSrc {
      type: "snowflake",
      connection: "test://conn",
      databases: ["DB1"],
      include_views: true,
      read_constraints: true
    }

    entity Customer {
      domain: "Sales",
      attributes: {
        customer_id: { type: "identifier", primary_key: true },
        name: { type: "string" }
      },
      relationships: {
        places: { target: "Order", cardinality: "1:N" }
      }
    }

    return true;
  )neam");
  EXPECT_TRUE(result.is_bool());
  EXPECT_TRUE(result.as_bool());
}

TEST(CompileRunTest, ModelingDimensionalAndMart)
{
  auto result = compile_and_run(R"neam(
    schema_source TestSrc {
      type: "snowflake",
      connection: "test://conn"
    }

    dimensional_model SalesModel {
      methodology: "star",
      source: TestSrc,
      facts: {
        FCT_SALES: {
          grain: "one row per line item",
          measures: { amount: { type: "additive" } }
        }
      },
      dimensions: {
        DIM_DATE: { scd_type: 0 },
        DIM_CUSTOMER: { scd_type: 2 }
      },
      conformed: [DIM_DATE],
      target_platform: "snowflake"
    }

    datamart FinanceMart {
      dimensional_model: SalesModel,
      purpose: "Finance reporting",
      facts: [FCT_SALES],
      dimensions: [DIM_DATE, DIM_CUSTOMER]
    }

    return true;
  )neam");
  EXPECT_TRUE(result.is_bool());
  EXPECT_TRUE(result.as_bool());
}

TEST(CompileRunTest, ModelingNormalizationAnalysis)
{
  auto result = compile_and_run(R"neam(
    normalization_analysis NormCheck {
      scope: {
        schemas: ["PUBLIC"],
        exclude_marts: true
      },
      target_nf: "3NF",
      on_violation: "report"
    }

    return true;
  )neam");
  EXPECT_TRUE(result.is_bool());
  EXPECT_TRUE(result.as_bool());
}

TEST(CompileRunTest, ModelingAmendmentWorkflow)
{
  auto result = compile_and_run(R"neam(
    schema_source TestSrc {
      type: "snowflake",
      connection: "test://conn"
    }

    er_model EnterpriseModel {
      version: "3.2",
      levels: ["physical", "logical"],
      source: TestSrc
    }

    amendment_config ChangeManagement {
      monitor: {
        databases: ["ANALYTICS"]
      },
      approval: {
        non_breaking: "auto_approve",
        breaking: "committee"
      }
    }

    amendment SalesV4 {
      model: EnterpriseModel,
      type: "breaking",
      description: "Split CUSTOMERS table for multi-address",
      auto_analyze: true,
      require_approval: true
    }

    return true;
  )neam");
  EXPECT_TRUE(result.is_bool());
  EXPECT_TRUE(result.as_bool());
}

TEST(CompileRunTest, ModelingToolSync)
{
  auto result = compile_and_run(R"neam(
    modeling_tool ErwinSync {
      type: "erwin",
      path: "./models/enterprise.erwin",
      sync: {
        direction: "bidirectional",
        conflict_resolution: "database_wins"
      }
    }

    return true;
  )neam");
  EXPECT_TRUE(result.is_bool());
  EXPECT_TRUE(result.as_bool());
}

TEST(CompileRunTest, ModelingFullPipeline)
{
  auto result = compile_and_run(R"neam(
    budget ArchBudget { cost: 50.00, tokens: 1000000 }

    schema_source Warehouse {
      type: "snowflake",
      connection: "test://conn",
      databases: ["ANALYTICS", "RAW"],
      scan_interval: "1h",
      include_views: true,
      read_constraints: true,
      read_indexes: true
    }

    schema_source ErwinFile {
      type: "erwin",
      path: "./models/enterprise.erwin",
      format: "xml",
      model_type: "physical_logical",
      sync_direction: "bidirectional"
    }

    er_model EnterpriseModel {
      version: "3.2",
      levels: ["conceptual", "logical", "physical"],
      source: Warehouse,
      notation: {
        conceptual: "chen",
        logical: "idef1x",
        physical: "crows_foot"
      }
    }

    entity Customer {
      domain: "Sales",
      attributes: {
        customer_id: { type: "identifier", primary_key: true },
        name: { type: "string" },
        email: { type: "string", classification: "pii" }
      },
      relationships: {
        places: { target: "Order", cardinality: "1:N" }
      },
      glossary_term: "Customer",
      owner: "sales_team"
    }

    dimensional_model SalesAnalytics {
      methodology: "star",
      source: Warehouse,
      facts: {
        FCT_SALES: {
          grain: "one row per order line item",
          measures: {
            quantity: { type: "additive" },
            amount: { type: "additive" }
          }
        }
      },
      dimensions: {
        DIM_DATE: { scd_type: 0 },
        DIM_CUSTOMER: { scd_type: 2 },
        DIM_PRODUCT: { scd_type: 1 }
      },
      conformed: [DIM_DATE, DIM_CUSTOMER],
      target_platform: "snowflake",
      target_schema: "ANALYTICS.SALES_MART"
    }

    datamart FinanceMart {
      dimensional_model: SalesAnalytics,
      purpose: "Finance team P&L reporting",
      owner: "finance_data_team",
      facts: [FCT_SALES],
      dimensions: [DIM_DATE, DIM_CUSTOMER],
      materialization: {
        facts: "incremental",
        dimensions: "table"
      }
    }

    normalization_analysis NormCheck {
      scope: {
        schemas: ["PUBLIC", "STAGING"],
        exclude_marts: true
      },
      target_nf: "3NF",
      on_violation: "report"
    }

    modeling_tool ErwinSync {
      type: "erwin",
      path: "./models/enterprise.erwin",
      sync: {
        direction: "bidirectional",
        conflict_resolution: "database_wins"
      }
    }

    modeling agent DataArchitect {
      provider: "anthropic",
      model: "claude-opus-4-6",
      sources: [Warehouse, ErwinFile],
      modeling_tools: [ErwinSync],
      capabilities: {
        reverse_engineer: true,
        normalization_analysis: { target_nf: "3NF" },
        dimensional_design: { default_methodology: "star" },
        amendment_proposals: true,
        data_profiling: { detect_pii: true }
      },
      budget: ArchBudget,
      enrich_from_governance: true,
      role: "data_architect",
      purpose: "Enterprise data modeling"
    }

    if (modeling_status(DataArchitect) != "initialized") {
      return false;
    }
    return true;
  )neam");
  EXPECT_TRUE(result.is_bool());
  EXPECT_TRUE(result.as_bool());
}

TEST(CompileRunTest, ModelingCrossVersionAllAgents)
{
  // Test that data, ETL, governance, and modeling agents coexist
  auto result = compile_and_run(R"neam(
    budget B { cost: 10.00, tokens: 100000 }

    schema TestSchema {
      id: int
    }
    source Src {
      schema: TestSchema,
      type: "postgres",
      connection: "c"
    }
    sink Snk {
      schema: TestSchema,
      type: "postgres",
      connection: "c"
    }

    data agent DA {
      provider: "openai",
      model: "gpt-4o-mini",
      sources: [Src],
      sinks: [Snk],
      budget: B
    }

    etl agent EA {
      provider: "openai",
      model: "gpt-4o-mini",
      sources: [Src],
      sinks: [Snk],
      warehouse: W,
      budget: B
    }

    catalog_source CS {
      type: "snowflake",
      connection: "c"
    }
    governance agent GA {
      provider: "openai",
      model: "gpt-4o-mini",
      catalog: CS,
      budget: B
    }

    schema_source MS {
      type: "snowflake",
      connection: "c"
    }
    modeling agent ModelArch {
      provider: "openai",
      model: "gpt-4o-mini",
      sources: [MS],
      budget: B,
      coordinates_with: [DA, EA, GA]
    }

    return true;
  )neam");
  EXPECT_TRUE(result.is_bool());
  EXPECT_TRUE(result.as_bool());
}

}  // namespace
