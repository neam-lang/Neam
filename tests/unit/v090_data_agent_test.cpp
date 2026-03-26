//
// Neam v0.9 — DataAgent Unit Tests
//
// Tests for all Phase 0-2 features: schema/source/sink/quality/compute/
// governance/catalog/data-agent parsing, compilation, field rejection,
// contextual keyword, and validation rules.
//

#include <gtest/gtest.h>

#include "neamc/pipeline.hpp"
#include "neamc/vm/vm.hpp"

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

// Helper: compile and capture emitted values
static std::vector<Value> compile_and_emit(const std::string& source)
{
  Pipeline pipeline;
  auto unit = pipeline.compile(source, {});
  VirtualMachine vm;
  vm.run(unit.chunk);
  return vm.emitted();
}

// Helper: assert that compilation succeeds without error
static void assert_compiles(const std::string& source)
{
  Pipeline pipeline;
  ASSERT_NO_THROW(pipeline.compile(source, {}));
}

// Helper: assert that compilation fails
static void assert_compile_error(const std::string& source)
{
  Pipeline pipeline;
  EXPECT_THROW(pipeline.compile(source, {}), std::exception);
}

// ============================================================================
// Schema Declaration Tests
// ============================================================================

TEST(DataAgentSchemaTest, BasicSchemaCompiles)
{
  assert_compiles(R"(
    schema TestSchema {
      id: string @primary_key,
      name: string @not_null,
      age: int @range(0, 150),
      active: bool
    }
    return nil;
  )");
}

TEST(DataAgentSchemaTest, SchemaAllConstraintTypes)
{
  assert_compiles(R"(
    schema FullSchema {
      id: string @primary_key,
      email: string @pattern("^[a-z]+@[a-z]+$") @unique,
      name: string @length(1, 200) @not_null,
      age: int @range(0, 150),
      balance: float @positive,
      status: string @enum(["active", "inactive"]),
      created: datetime @default(now())
    }
    return nil;
  )");
}

TEST(DataAgentSchemaTest, SchemaWithVersion)
{
  assert_compiles(R"(
    schema Versioned {
      version: 2,
      id: string @primary_key,
      name: string @not_null
    }
    return nil;
  )");
}

TEST(DataAgentSchemaTest, SchemaWithForeignKey)
{
  assert_compiles(R"(
    schema Parent {
      id: string @primary_key
    }
    schema Child {
      id: string @primary_key,
      parent_id: string @foreign_key(Parent)
    }
    return nil;
  )");
}

// ============================================================================
// Source Declaration Tests
// ============================================================================

TEST(DataAgentSourceTest, PostgresSource)
{
  assert_compiles(R"(
    source SalesDB {
      type: "postgres",
      connection: "postgresql://localhost/sales",
      refresh: "5m"
    }
    return nil;
  )");
}

TEST(DataAgentSourceTest, S3Source)
{
  assert_compiles(R"(
    source DataLake {
      type: "s3",
      connection: "s3://bucket/prefix/",
      format: "parquet",
      partition_by: ["date", "region"]
    }
    return nil;
  )");
}

TEST(DataAgentSourceTest, HTTPSource)
{
  assert_compiles(R"(
    source API {
      type: "http",
      connection: "https://api.example.com/data",
      format: "json",
      refresh: "1h"
    }
    return nil;
  )");
}

TEST(DataAgentSourceTest, KafkaSource)
{
  assert_compiles(R"(
    source Events {
      type: "kafka",
      connection: "kafka://broker:9092",
      mode: "batch"
    }
    return nil;
  )");
}

TEST(DataAgentSourceTest, SourceWithSchemaRef)
{
  assert_compiles(R"(
    schema MySchema { id: string @primary_key, name: string }
    source DB {
      type: "postgres",
      connection: "pg://localhost",
      schema: MySchema
    }
    return nil;
  )");
}

TEST(DataAgentSourceTest, SourceWithClassification)
{
  assert_compiles(R"(
    source Classified {
      type: "postgres",
      connection: "pg://localhost",
      classification: "confidential"
    }
    return nil;
  )");
}

// ============================================================================
// Sink Declaration Tests
// ============================================================================

TEST(DataAgentSinkTest, SnowflakeSink)
{
  assert_compiles(R"(
    sink Warehouse {
      type: "snowflake",
      connection: "sf://account",
      write_mode: "upsert",
      batch_size: 5000
    }
    return nil;
  )");
}

TEST(DataAgentSinkTest, WebhookSink)
{
  assert_compiles(R"(
    sink Dashboard {
      type: "webhook",
      connection: "https://hook.example.com",
      format: "json"
    }
    return nil;
  )");
}

TEST(DataAgentSinkTest, S3Sink)
{
  assert_compiles(R"(
    sink Archive {
      type: "s3",
      connection: "s3://archive/output/",
      format: "parquet",
      write_mode: "append"
    }
    return nil;
  )");
}

TEST(DataAgentSinkTest, AllWriteModes)
{
  for (auto mode : {"append", "upsert", "replace", "merge"})
  {
    std::string src = R"(
      sink TestSink {
        type: "postgres",
        connection: "pg://localhost",
        write_mode: ")" + std::string(mode) + R"("
      }
      return nil;
    )";
    assert_compiles(src);
  }
}

// ============================================================================
// Quality Declaration Tests
// ============================================================================

TEST(DataAgentQualityTest, BasicQuality)
{
  assert_compiles(R"(
    quality Basic {
      completeness: 0.95,
      on_violation: "gate"
    }
    return nil;
  )");
}

TEST(DataAgentQualityTest, FullQuality)
{
  assert_compiles(R"(
    quality Full {
      freshness: "1h",
      completeness: 0.98,
      uniqueness: ["id", "email"],
      drift_detection: true,
      anomaly_threshold: 3.0,
      on_violation: "gate_and_alert"
    }
    return nil;
  )");
}

TEST(DataAgentQualityTest, AllViolationActions)
{
  for (auto action : {"warn", "gate", "gate_and_alert",
                       "gate_and_escalate", "quarantine_and_alert"})
  {
    std::string src = R"(
      quality Q {
        completeness: 0.90,
        on_violation: ")" + std::string(action) + R"("
      }
      return nil;
    )";
    assert_compiles(src);
  }
}

// ============================================================================
// Compute Declaration Tests
// ============================================================================

TEST(DataAgentComputeTest, SparkCompute)
{
  assert_compiles(R"(
    compute Spark {
      engine: "spark",
      config: {
        "spark.executor.memory": "8g",
        "spark.executor.cores": "4"
      }
    }
    return nil;
  )");
}

TEST(DataAgentComputeTest, SnowflakeCompute)
{
  assert_compiles(R"(
    compute SF {
      engine: "snowflake",
      config: { "warehouse": "WH", "warehouse_size": "MEDIUM" }
    }
    return nil;
  )");
}

TEST(DataAgentComputeTest, LocalCompute)
{
  assert_compiles(R"(
    compute Local { engine: "local" }
    return nil;
  )");
}

TEST(DataAgentComputeTest, AllEngineTypes)
{
  for (auto engine : {"spark", "snowflake", "databricks", "bigquery", "local"})
  {
    std::string src = R"(
      compute Eng { engine: ")" + std::string(engine) + R"(" }
      return nil;
    )";
    assert_compiles(src);
  }
}

// ============================================================================
// Governance Declaration Tests
// ============================================================================

TEST(DataAgentGovernanceTest, MinimalGovernance)
{
  assert_compiles(R"(
    governance MinPolicy {
      access: {
        model: "rbac",
        roles: {
          "analyst": { read: ["public", "internal"], write: ["public"] }
        }
      }
    }
    return nil;
  )");
}

TEST(DataAgentGovernanceTest, FullGovernance)
{
  assert_compiles(R"(
    governance FullPolicy {
      classification: {
        auto_classify: [
          { pattern: "ssn", level: "restricted" },
          { pattern: "email", level: "confidential" }
        ]
      },
      access: {
        model: "rbac",
        roles: {
          "engineer": { read: ["public", "internal", "confidential"],
                        write: ["public", "internal"] }
        },
        masking: { "confidential": "partial", "restricted": "full" }
      },
      retention: {
        default: "7y",
        auto_purge: true
      },
      jurisdiction: {
        rules: [
          { data_type: "eu_data",
            allowed_regions: ["eu-west-1"],
            regulation: "GDPR",
            requirements: ["right_to_erasure"] }
        ],
        enforce_data_residency: true
      },
      consent: {
        tracking: true,
        purposes: ["analytics", "marketing"],
        honor_withdrawal: true
      },
      audit: {
        log_all_access: true,
        immutable: true,
        format: "opentelemetry"
      }
    }
    return nil;
  )");
}

// ============================================================================
// Catalog Declaration Tests
// ============================================================================

TEST(DataAgentCatalogTest, UnityCatalog)
{
  assert_compiles(R"(
    catalog UC {
      engine: "unity_catalog",
      register: { schemas: true, lineage: true },
      discovery: true
    }
    return nil;
  )");
}

TEST(DataAgentCatalogTest, DataHubCatalog)
{
  assert_compiles(R"(
    catalog DH {
      engine: "datahub",
      register: { schemas: true, quality: true },
      discovery: true
    }
    return nil;
  )");
}

// ============================================================================
// Data Agent Declaration Tests
// ============================================================================

TEST(DataAgentDeclTest, MinimalDataAgent)
{
  assert_compiles(R"(
    source DB { type: "postgres", connection: "pg://localhost" }
    sink Out { type: "s3", connection: "s3://bucket/" }

    data agent Minimal {
      provider: "openai",
      model: "gpt-4o-mini",
      sources: [DB],
      sinks: [Out]
    }
    return nil;
  )");
}

TEST(DataAgentDeclTest, FullDataAgent)
{
  assert_compiles(R"(
    schema S { id: string @primary_key, val: float }
    source DB { type: "postgres", connection: "pg://localhost", schema: S }
    sink Out { type: "snowflake", connection: "sf://account", write_mode: "upsert" }
    quality Q { completeness: 0.95, on_violation: "gate" }
    compute C { engine: "local" }
    governance G {
      access: {
        model: "rbac",
        roles: { "analyst": { read: ["public", "internal"], write: ["public"] } }
      }
    }
    catalog Cat { engine: "datahub", discovery: true }

    data agent Full {
      provider: "anthropic",
      model: "claude-sonnet-4-6",
      system: "Full featured agent.",
      temperature: 0.3,
      sources: [DB],
      sinks: [Out],
      schema: S,
      quality: Q,
      compute: { default: C },
      governance: G,
      catalog: Cat,
      lineage: true,
      role: "analyst",
      purpose: "Test pipeline",
      autonomy: "conditional",
      pipeline: {
        extract: [DB],
        transform: [
          filter(column: "val", op: ">", value: 0),
          aggregate(group_by: ["id"], metrics: ["sum_val"])
        ],
        load: [Out]
      }
    }
    return nil;
  )");
}

TEST(DataAgentDeclTest, DataAgentWithBudgetAndGuards)
{
  assert_compiles(R"(
    source DB { type: "postgres", connection: "pg://localhost" }
    sink Out { type: "s3", connection: "s3://bucket/" }
    budget B { cost: 10.00, tokens: 10000, time: 60000 }

    skill MySkill {
      description: "Test skill"
      params: { q: string }
      impl(q) { return q; }
    }

    guard MyGuard {
      description: "Test guard"
      on_observation(text) { return text; }
    }
    guardchain Chain = [MyGuard];

    data agent Guarded {
      provider: "openai",
      model: "gpt-4o-mini",
      sources: [DB],
      sinks: [Out],
      budget: B,
      skills: [MySkill],
      guardchains: [Chain]
    }
    return nil;
  )");
}

TEST(DataAgentDeclTest, MultipleSources)
{
  assert_compiles(R"(
    source A { type: "postgres", connection: "pg://a" }
    source B { type: "s3", connection: "s3://b/" }
    source C { type: "http", connection: "https://c.com" }
    sink Out { type: "s3", connection: "s3://out/" }

    data agent Multi {
      provider: "openai", model: "gpt-4o-mini",
      sources: [A, B, C],
      sinks: [Out]
    }
    return nil;
  )");
}

TEST(DataAgentDeclTest, MultipleSinks)
{
  assert_compiles(R"(
    source DB { type: "postgres", connection: "pg://localhost" }
    sink S1 { type: "s3", connection: "s3://out1/" }
    sink S2 { type: "webhook", connection: "https://hook.com" }
    sink S3 { type: "postgres", connection: "pg://analytics" }

    data agent MultiSink {
      provider: "openai", model: "gpt-4o-mini",
      sources: [DB],
      sinks: [S1, S2, S3]
    }
    return nil;
  )");
}

// ============================================================================
// Pipeline Tests
// ============================================================================

TEST(DataAgentPipelineTest, EmptyTransform)
{
  assert_compiles(R"(
    source DB { type: "postgres", connection: "pg://localhost" }
    sink Out { type: "s3", connection: "s3://out/" }

    data agent PassThrough {
      provider: "openai", model: "gpt-4o-mini",
      sources: [DB],
      sinks: [Out],
      pipeline: {
        extract: [DB],
        transform: [],
        load: [Out]
      }
    }
    return nil;
  )");
}

TEST(DataAgentPipelineTest, AllTransformOperators)
{
  assert_compiles(R"(
    source DB { type: "postgres", connection: "pg://localhost" }
    sink Out { type: "s3", connection: "s3://out/" }
    quality Q { completeness: 0.90, on_violation: "warn" }

    data agent AllOps {
      provider: "openai", model: "gpt-4o-mini",
      sources: [DB],
      sinks: [Out],
      pipeline: {
        extract: [DB],
        transform: [
          filter(column: "status", op: "==", value: "active"),
          deduplicate(keys: ["id"]),
          validate(rules: Q),
          aggregate(group_by: ["region"], metrics: ["sum_val"]),
          derive(column: "norm", expr: "val / max_val"),
          sort(column: "val", ascending: false),
          limit(n: 1000),
          enrich(column: "sentiment", agent: AllOps),
          tag_pii(),
          normalize(),
          profile()
        ],
        load: [Out]
      }
    }
    return nil;
  )");
}

TEST(DataAgentPipelineTest, JoinPipeline)
{
  assert_compiles(R"(
    source A { type: "postgres", connection: "pg://a" }
    source B { type: "postgres", connection: "pg://b" }
    sink Out { type: "s3", connection: "s3://out/" }

    data agent Joiner {
      provider: "openai", model: "gpt-4o-mini",
      sources: [A, B],
      sinks: [Out],
      pipeline: {
        extract: [A, B],
        transform: [
          join(source: B, on: ["id"], type: "left"),
          filter(column: "status", op: "!=", value: "deleted")
        ],
        load: [Out]
      }
    }
    return nil;
  )");
}

TEST(DataAgentPipelineTest, MultiSourceMultiSinkPipeline)
{
  assert_compiles(R"(
    source S1 { type: "postgres", connection: "pg://s1" }
    source S2 { type: "s3", connection: "s3://s2/" }
    sink D1 { type: "snowflake", connection: "sf://account" }
    sink D2 { type: "webhook", connection: "https://hook.com" }

    data agent Complex {
      provider: "openai", model: "gpt-4o-mini",
      sources: [S1, S2],
      sinks: [D1, D2],
      pipeline: {
        extract: [S1, S2],
        transform: [
          join(source: S2, on: ["key"], type: "inner"),
          aggregate(group_by: ["category"], metrics: ["sum_amount"])
        ],
        load: [D1, D2]
      }
    }
    return nil;
  )");
}

// ============================================================================
// Compute Routing Tests
// ============================================================================

TEST(DataAgentComputeRoutingTest, SimpleRouting)
{
  assert_compiles(R"(
    source DB { type: "postgres", connection: "pg://localhost" }
    sink Out { type: "s3", connection: "s3://out/" }
    compute Spark { engine: "spark" }
    compute SF { engine: "snowflake" }
    compute Local { engine: "local" }

    data agent Routed {
      provider: "openai", model: "gpt-4o-mini",
      sources: [DB],
      sinks: [Out],
      compute: {
        default: Local,
        available: [Spark, SF, Local],
        routing: [
          { stage: "aggregate", engine: Spark },
          { stage: "window", engine: SF }
        ]
      }
    }
    return nil;
  )");
}

// ============================================================================
// Contextual Keyword Tests
// ============================================================================

TEST(DataAgentContextualKeywordTest, DataAsVariable)
{
  auto result = compile_and_run(R"(
    let data = 42;
    return data;
  )");
  ASSERT_TRUE(result.is_number());
  EXPECT_DOUBLE_EQ(result.as_number(), 42.0);
}

TEST(DataAgentContextualKeywordTest, DataInExpressions)
{
  auto result = compile_and_run(R"(
    let data = 10;
    let data_count = 5;
    return data * data_count;
  )");
  ASSERT_TRUE(result.is_number());
  EXPECT_DOUBLE_EQ(result.as_number(), 50.0);
}

TEST(DataAgentContextualKeywordTest, DataAsMapKey)
{
  auto result = compile_and_run(R"(
    let m = {"data": 99};
    return m["data"];
  )");
  ASSERT_TRUE(result.is_number());
  EXPECT_DOUBLE_EQ(result.as_number(), 99.0);
}

TEST(DataAgentContextualKeywordTest, DataAgentAndDataVariable)
{
  // Both 'data agent' and 'data' as variable in same program
  assert_compiles(R"(
    source DB { type: "postgres", connection: "pg://localhost" }
    sink Out { type: "s3", connection: "s3://out/" }

    let data = "hello";
    let data_items = [1, 2, 3];

    data agent MyAgent {
      provider: "openai", model: "gpt-4o-mini",
      sources: [DB],
      sinks: [Out]
    }

    emit data;
    return nil;
  )");
}

// ============================================================================
// Backward Compatibility Tests
// ============================================================================

TEST(DataAgentBackwardCompatTest, PlainAgentStillWorks)
{
  assert_compiles(R"(
    skill Echo {
      description: "Echo"
      params: { text: string }
      impl(text) { return text; }
    }
    agent Bot {
      provider: "openai", model: "gpt-4o-mini",
      system: "Test.", skills: [Echo]
    }
    return nil;
  )");
}

TEST(DataAgentBackwardCompatTest, ClawAgentStillWorks)
{
  assert_compiles(R"(
    skill Echo {
      description: "Echo"
      params: { text: string }
      impl(text) { return text; }
    }
    claw agent Bot {
      provider: "openai", model: "gpt-4o-mini",
      channels: [Echo]
    }
    return nil;
  )");
}

TEST(DataAgentBackwardCompatTest, ForgeAgentStillWorks)
{
  assert_compiles(R"(
    fun verify(ctx) { return true; }
    forge agent Bot {
      provider: "openai", model: "gpt-4o-mini",
      verify: verify
    }
    return nil;
  )");
}

TEST(DataAgentBackwardCompatTest, AllAgentTypesTogether)
{
  assert_compiles(R"(
    skill Echo {
      description: "Echo"
      params: { text: string }
      impl(text) { return text; }
    }
    fun verify(ctx) { return true; }

    source DB { type: "postgres", connection: "pg://localhost" }
    sink Out { type: "s3", connection: "s3://out/" }

    agent PlainBot {
      provider: "openai", model: "gpt-4o-mini",
      skills: [Echo]
    }
    claw agent ClawBot {
      provider: "openai", model: "gpt-4o-mini",
      channels: [Echo]
    }
    forge agent ForgeBot {
      provider: "openai", model: "gpt-4o-mini",
      verify: verify
    }
    data agent DataBot {
      provider: "openai", model: "gpt-4o-mini",
      sources: [DB], sinks: [Out]
    }

    return nil;
  )");
}

// ============================================================================
// Field Rejection Tests
// ============================================================================

TEST(DataAgentFieldRejectionTest, SourcesInClawAgentRejected)
{
  assert_compile_error(R"(
    source DB { type: "postgres", connection: "pg://localhost" }
    claw agent Bad {
      provider: "openai", model: "gpt-4o-mini",
      channels: [],
      sources: [DB]
    }
  )");
}

TEST(DataAgentFieldRejectionTest, SourcesInForgeAgentRejected)
{
  assert_compile_error(R"(
    source DB { type: "postgres", connection: "pg://localhost" }
    fun v(ctx) { return true; }
    forge agent Bad {
      provider: "openai", model: "gpt-4o-mini",
      verify: v,
      sources: [DB]
    }
  )");
}

TEST(DataAgentFieldRejectionTest, PipelineInPlainAgentRejected)
{
  assert_compile_error(R"(
    source DB { type: "postgres", connection: "pg://localhost" }
    agent Bad {
      provider: "openai", model: "gpt-4o-mini",
      pipeline: { extract: [DB], transform: [], load: [] }
    }
  )");
}

TEST(DataAgentFieldRejectionTest, VerifyInDataAgentRejected)
{
  assert_compile_error(R"(
    source DB { type: "postgres", connection: "pg://localhost" }
    fun v(ctx) { return true; }
    data agent Bad {
      provider: "openai", model: "gpt-4o-mini",
      sources: [DB],
      verify: v
    }
  )");
}

TEST(DataAgentFieldRejectionTest, ChannelsInDataAgentRejected)
{
  assert_compile_error(R"(
    source DB { type: "postgres", connection: "pg://localhost" }
    skill S { description: "s" params: { x: string } impl(x) { return x; } }
    data agent Bad {
      provider: "openai", model: "gpt-4o-mini",
      sources: [DB],
      channels: [S]
    }
  )");
}

// ============================================================================
// Compiler Validation Rule Tests (DATA-001 through DATA-025)
// ============================================================================

TEST(DataAgentValidationTest, DATA001_NoSourcesError)
{
  // Data agent must have at least one source
  assert_compile_error(R"(
    data agent NoSources {
      provider: "openai", model: "gpt-4o-mini"
    }
  )");
}

TEST(DataAgentValidationTest, DATA006_UnknownSourceType)
{
  assert_compile_error(R"(
    source Bad { type: "oracle_magic", connection: "..." }
  )");
}

TEST(DataAgentValidationTest, DATA008_InvalidWriteMode)
{
  assert_compile_error(R"(
    sink Bad { type: "postgres", connection: "...", write_mode: "overwrite" }
  )");
}

TEST(DataAgentValidationTest, DATA010_InvalidFieldType)
{
  assert_compile_error(R"(
    schema Bad { field1: uuid @primary_key }
  )");
}

TEST(DataAgentValidationTest, DATA012_RangeMinGtMax)
{
  assert_compile_error(R"(
    schema Bad { age: int @range(150, 0) }
  )");
}

TEST(DataAgentValidationTest, DATA013_EmptyEnum)
{
  assert_compile_error(R"(
    schema Bad { status: string @enum([]) }
  )");
}

TEST(DataAgentValidationTest, DATA016_UnknownComputeEngine)
{
  assert_compile_error(R"(
    compute Bad { engine: "quantum_computer" }
  )");
}

TEST(DataAgentValidationTest, DATA021_InvalidOnViolation)
{
  assert_compile_error(R"(
    quality Bad { completeness: 0.90, on_violation: "explode" }
  )");
}

TEST(DataAgentValidationTest, DATA022_CompletenessOutOfRange)
{
  assert_compile_error(R"(
    quality Bad { completeness: 1.5 }
  )");
}

TEST(DataAgentValidationTest, DATA022_CompletenessNegative)
{
  assert_compile_error(R"(
    quality Bad { completeness: -0.1 }
  )");
}

TEST(DataAgentValidationTest, DATA024_UnknownCatalogEngine)
{
  assert_compile_error(R"(
    catalog Bad { engine: "google_sheets" }
  )");
}

// ============================================================================
// DAG Orchestration Compile Tests
// ============================================================================

TEST(DataAgentDAGTest, MedallionDAGCompiles)
{
  assert_compiles(R"(
    source Raw { type: "kafka", connection: "kafka://broker" }
    sink Bronze { type: "s3", connection: "s3://lake/bronze/" }
    sink Silver { type: "s3", connection: "s3://lake/silver/" }
    sink Gold { type: "s3", connection: "s3://lake/gold/" }
    source BronzeRead { type: "s3", connection: "s3://lake/bronze/" }
    source SilverRead { type: "s3", connection: "s3://lake/silver/" }

    data agent BA {
      provider: "openai", model: "gpt-4o-mini",
      sources: [Raw], sinks: [Bronze],
      pipeline: { extract: [Raw], transform: [], load: [Bronze] }
    }
    data agent SA {
      provider: "openai", model: "gpt-4o-mini",
      sources: [BronzeRead], sinks: [Silver],
      pipeline: { extract: [BronzeRead], transform: [deduplicate(keys: ["id"])], load: [Silver] }
    }
    data agent GA {
      provider: "openai", model: "gpt-4o-mini",
      sources: [SilverRead], sinks: [Gold],
      pipeline: {
        extract: [SilverRead],
        transform: [aggregate(group_by: ["cat"], metrics: ["sum_val"])],
        load: [Gold]
      }
    }

    let result = dag_execute([
      { id: "b", agent: BA, task: "Bronze" },
      { id: "s", agent: SA, task: "Silver", depends_on: ["b"] },
      { id: "g", agent: GA, task: "Gold", depends_on: ["s"] }
    ]);

    return nil;
  )");
}

// ============================================================================
// Emit + Runtime Tests (compile and run)
// ============================================================================

TEST(DataAgentRuntimeTest, DataAgentRegistersInVM)
{
  auto emitted = compile_and_emit(R"(
    source DB { type: "postgres", connection: "pg://localhost" }
    sink Out { type: "s3", connection: "s3://out/" }

    data agent TestAgent {
      provider: "openai", model: "gpt-4o-mini",
      sources: [DB], sinks: [Out],
      lineage: true
    }

    emit "registered";
  )");

  ASSERT_EQ(emitted.size(), 1u);
  ASSERT_TRUE(is_obj_type(emitted[0], ObjType::OBJ_STRING));
  auto* str = as_string(emitted[0]);
  EXPECT_EQ(std::string(str->chars, str->length), "registered");
}

TEST(DataAgentRuntimeTest, SchemaRegistersInVM)
{
  auto emitted = compile_and_emit(R"(
    schema TestSchema {
      id: string @primary_key,
      name: string @not_null,
      value: float @positive
    }
    emit "schema_ok";
  )");

  ASSERT_EQ(emitted.size(), 1u);
  auto* str = as_string(emitted[0]);
  EXPECT_EQ(std::string(str->chars, str->length), "schema_ok");
}

TEST(DataAgentRuntimeTest, SourceRegistersInVM)
{
  auto emitted = compile_and_emit(R"(
    source TestDB {
      type: "postgres",
      connection: "pg://localhost/test",
      refresh: "5m"
    }
    emit "source_ok";
  )");

  ASSERT_EQ(emitted.size(), 1u);
  auto* str = as_string(emitted[0]);
  EXPECT_EQ(std::string(str->chars, str->length), "source_ok");
}

}  // namespace
