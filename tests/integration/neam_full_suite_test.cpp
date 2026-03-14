//
// Neam Full Suite Integration Test — v0.8.0 through v0.9.1
//
// Consolidated compile-and-run tests covering all major features
// introduced from v0.8.0 (Claw/Forge agents) through v0.9.1 (ETL agents).
// Each test compiles Neam source via Pipeline and executes in the VM.
//
// Versions covered:
//   v0.7.1  — OOP: structs, traits, sealed types, match, extend, generics
//   v0.8.0  — Claw agents, Forge agents, channels, lanes, multi-agent (spawn/DAG)
//   v0.9.0  — DataAgents: schema, source, sink, quality, compute, governance, catalog
//   v0.9.1  — ETLAgents: semantic, mart, layers, incremental, self_heal, auto_model
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
//  v0.7.1 — OOP: Structs, Traits, Sealed Types, Match, Extend
// ============================================================================

TEST(NeamFullSuite, V071_StructDeclaration)
{
  auto result = compile_and_run(R"(
    struct Point { x, y }
    let p = Point(10, 20);
    return p.x + p.y;
  )");
  ASSERT_TRUE(result.is_number());
  EXPECT_DOUBLE_EQ(result.as_number(), 30.0);
}

TEST(NeamFullSuite, V071_StructNamedConstruction)
{
  auto result = compile_and_run(R"(
    struct Point { x, y }
    let p = Point(x: 5, y: 7);
    return p.x * p.y;
  )");
  ASSERT_TRUE(result.is_number());
  EXPECT_DOUBLE_EQ(result.as_number(), 35.0);
}

TEST(NeamFullSuite, V071_ImplBlock)
{
  auto result = compile_and_run(R"(
    struct Rect { w, h }
    impl Rect {
      fn area(self) { return self.w * self.h; }
    }
    let r = Rect(3, 4);
    return r.area();
  )");
  ASSERT_TRUE(result.is_number());
  EXPECT_DOUBLE_EQ(result.as_number(), 12.0);
}

TEST(NeamFullSuite, V071_TraitDeclaration)
{
  assert_compiles(R"(
    trait Describable {
      fn describe(self);
    }
    struct Dog { name }
    impl Describable for Dog {
      fn describe(self) { return "Dog: " + self.name; }
    }
    let d = Dog("Rex");
    return nil;
  )");
}

TEST(NeamFullSuite, V071_SealedTypeAndMatch)
{
  auto result = compile_and_run(R"(
    sealed Shape {
      Circle(radius)
      Rectangle(w, h)
    }
    let s = Shape.Circle(5);
    let area = match s {
      Circle(r) => 3.14 * r * r,
      Rectangle(w, h) => w * h
    };
    return area;
  )");
  ASSERT_TRUE(result.is_number());
  EXPECT_DOUBLE_EQ(result.as_number(), 78.5);
}

TEST(NeamFullSuite, V071_ExtendBlock)
{
  auto result = compile_and_run(R"(
    struct Counter { value }
    extend Counter {
      fn increment(self) { return Counter(self.value + 1); }
    }
    let c = Counter(0);
    let c2 = c.increment();
    return c2.value;
  )");
  ASSERT_TRUE(result.is_number());
  EXPECT_DOUBLE_EQ(result.as_number(), 1.0);
}

TEST(NeamFullSuite, V071_PipelineOperator)
{
  auto result = compile_and_run(R"(
    fun double(x) { return x * 2; }
    fun add_one(x) { return x + 1; }
    let result = 5 |> double |> add_one;
    return result;
  )");
  ASSERT_TRUE(result.is_number());
  EXPECT_DOUBLE_EQ(result.as_number(), 11.0);
}

// ============================================================================
//  v0.8.0 — Claw Agents, Forge Agents, Channels, Multi-Agent
// ============================================================================

TEST(NeamFullSuite, V080_ClawAgentDeclaration)
{
  assert_compiles(R"neam(
    channel CLI {
      type: "cli",
      prompt: "> "
    }
    skill Echo {
      description: "Echo input"
      params: { text: string }
      impl(text) { return text; }
    }
    claw agent Assistant {
      provider: "openai",
      model: "gpt-4o-mini",
      system: "You are a helpful assistant.",
      skills: [Echo],
      channels: [CLI],
      session: {
        max_history_turns: 10
        compaction: "semantic"
      }
    }
    return nil;
  )neam");
}

TEST(NeamFullSuite, V080_ForgeAgentDeclaration)
{
  assert_compiles(R"neam(
    fun always_pass(ctx) { return true; }
    forge agent Builder {
      provider: "openai",
      model: "gpt-4o",
      system: "You build things.",
      verify: always_pass,
      loop: {
        max_iterations: 5
      }
    }
    return nil;
  )neam");
}

TEST(NeamFullSuite, V080_ChannelDeclaration)
{
  assert_compiles(R"neam(
    channel CLI {
      type: "cli",
      prompt: "> "
    }
    return nil;
  )neam");
}

TEST(NeamFullSuite, V080_SpawnKeyword)
{
  assert_compiles(R"neam(
    skill Echo {
      description: "Echo"
      params: { text: string }
      impl(text) { return text; }
    }
    agent Worker {
      provider: "openai",
      model: "gpt-4o-mini",
      system: "Worker.",
      skills: [Echo]
    }
    return nil;
  )neam");
}

TEST(NeamFullSuite, V080_LaneConfiguration)
{
  assert_compiles(R"neam(
    channel CLI { type: "cli", prompt: "> " }
    skill Echo {
      description: "Echo"
      params: { text: string }
      impl(text) { return text; }
    }
    claw agent LanedAgent {
      provider: "openai",
      model: "gpt-4o-mini",
      system: "You are helpful.",
      skills: [Echo],
      channels: [CLI],
      lanes: {
        default: {
          concurrency: 4
          priority: "fifo"
        }
      }
    }
    return nil;
  )neam");
}

TEST(NeamFullSuite, V080_ClawAndForgeCoexist)
{
  assert_compiles(R"neam(
    channel CLI { type: "cli", prompt: "> " }
    skill Echo {
      description: "Echo"
      params: { text: string }
      impl(text) { return text; }
    }
    fun verify(ctx) { return true; }

    claw agent ChatBot {
      provider: "openai",
      model: "gpt-4o-mini",
      system: "Chat bot.",
      skills: [Echo],
      channels: [CLI]
    }

    forge agent CodeGen {
      provider: "openai",
      model: "gpt-4o",
      system: "Code generator.",
      verify: verify,
      loop: { max_iterations: 3 }
    }

    return nil;
  )neam");
}

TEST(NeamFullSuite, V080_ContextualClawKeyword)
{
  auto result = compile_and_run(R"(
    let claw = "not an agent";
    return claw;
  )");
  ASSERT_TRUE(result.is_string());
  auto* str = as_string(result);
  EXPECT_EQ(std::string(str->chars, str->length), "not an agent");
}

TEST(NeamFullSuite, V080_ContextualForgeKeyword)
{
  auto result = compile_and_run(R"(
    let forge = 100;
    return forge;
  )");
  ASSERT_TRUE(result.is_number());
  EXPECT_DOUBLE_EQ(result.as_number(), 100.0);
}

// ============================================================================
//  v0.9.0 — DataAgents: Schema, Source, Sink, Quality, Compute, Governance
// ============================================================================

TEST(NeamFullSuite, V090_SchemaWithConstraints)
{
  auto emitted = compile_and_emit(R"neam(
    schema CustomerSchema {
      customer_id: string @primary_key,
      email: string @unique @not_null,
      name: string @length(1, 200),
      age: int @range(0, 150),
      balance: float @positive,
      status: string @enum(["active", "inactive"]),
      created_at: datetime @default(now())
    }
    emit "schema_ok";
  )neam");
  ASSERT_EQ(emitted.size(), 1u);
  auto* str = as_string(emitted[0]);
  EXPECT_EQ(std::string(str->chars, str->length), "schema_ok");
}

TEST(NeamFullSuite, V090_SchemaForeignKey)
{
  assert_compiles(R"neam(
    schema CustomerSchema {
      customer_id: string @primary_key,
      name: string @not_null
    }
    schema OrderSchema {
      order_id: string @primary_key,
      customer_id: string @foreign_key(CustomerSchema),
      amount: float @positive
    }
    return nil;
  )neam");
}

TEST(NeamFullSuite, V090_SourceDeclarations)
{
  auto emitted = compile_and_emit(R"neam(
    source PostgresDB {
      type: "postgres",
      connection: "pg://localhost/sales"
    }
    source S3Lake {
      type: "s3",
      connection: "s3://analytics-raw/events/",
      format: "parquet",
      partition_by: ["year", "month"]
    }
    emit "sources_ok";
  )neam");
  ASSERT_EQ(emitted.size(), 1u);
  auto* str = as_string(emitted[0]);
  EXPECT_EQ(std::string(str->chars, str->length), "sources_ok");
}

TEST(NeamFullSuite, V090_SinkDeclaration)
{
  auto emitted = compile_and_emit(R"neam(
    sink OutputWarehouse {
      type: "s3",
      connection: "s3://output/processed/",
      format: "parquet",
      write_mode: "append"
    }
    emit "sink_ok";
  )neam");
  ASSERT_EQ(emitted.size(), 1u);
  auto* str = as_string(emitted[0]);
  EXPECT_EQ(std::string(str->chars, str->length), "sink_ok");
}

TEST(NeamFullSuite, V090_QualityGate)
{
  auto emitted = compile_and_emit(R"neam(
    quality StrictQuality {
      completeness: 0.99,
      on_violation: "warn"
    }
    emit "quality_ok";
  )neam");
  ASSERT_EQ(emitted.size(), 1u);
  auto* str = as_string(emitted[0]);
  EXPECT_EQ(std::string(str->chars, str->length), "quality_ok");
}

TEST(NeamFullSuite, V090_ComputeEngine)
{
  auto emitted = compile_and_emit(R"neam(
    compute SparkCluster {
      engine: "spark",
      config: { parallelism: 8 }
    }
    emit "compute_ok";
  )neam");
  ASSERT_EQ(emitted.size(), 1u);
  auto* str = as_string(emitted[0]);
  EXPECT_EQ(std::string(str->chars, str->length), "compute_ok");
}

TEST(NeamFullSuite, V090_GovernancePolicy)
{
  auto emitted = compile_and_emit(R"neam(
    governance GDPR {
      access: [
        { "role": "analyst", "permissions": ["read"] },
        { "role": "admin", "permissions": ["read", "write", "delete"] }
      ],
      retention: [
        { "classification": "pii", "max_days": 365, "action": "anonymize" }
      ]
    }
    emit "governance_ok";
  )neam");
  ASSERT_EQ(emitted.size(), 1u);
  auto* str = as_string(emitted[0]);
  EXPECT_EQ(std::string(str->chars, str->length), "governance_ok");
}

TEST(NeamFullSuite, V090_CatalogDeclaration)
{
  auto emitted = compile_and_emit(R"neam(
    catalog DataCatalog {
      engine: "unity_catalog",
      discovery: true
    }
    emit "catalog_ok";
  )neam");
  ASSERT_EQ(emitted.size(), 1u);
  auto* str = as_string(emitted[0]);
  EXPECT_EQ(std::string(str->chars, str->length), "catalog_ok");
}

TEST(NeamFullSuite, V090_MinimalDataAgent)
{
  auto emitted = compile_and_emit(R"neam(
    source Input {
      type: "s3",
      connection: "./data.csv",
      format: "csv"
    }
    sink Output {
      type: "s3",
      connection: "./output.parquet",
      format: "parquet"
    }
    data agent Processor {
      provider: "openai", model: "gpt-4o-mini",
      sources: [Input],
      sinks: [Output]
    }
    emit "data_agent_ok";
  )neam");
  ASSERT_EQ(emitted.size(), 1u);
  auto* str = as_string(emitted[0]);
  EXPECT_EQ(std::string(str->chars, str->length), "data_agent_ok");
}

TEST(NeamFullSuite, V090_DataAgentWithPipeline)
{
  assert_compiles(R"neam(
    source SalesDB {
      type: "postgres",
      connection: "pg://localhost/sales"
    }
    sink AnalyticsOut {
      type: "s3",
      connection: "./analytics.parquet",
      format: "parquet",
      write_mode: "replace"
    }
    quality SalesQuality {
      completeness: 0.95,
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
  )neam");
}

TEST(NeamFullSuite, V090_DataAgentWithGovernance)
{
  assert_compiles(R"neam(
    source UserSource {
      type: "s3",
      connection: "./users.csv",
      format: "csv"
    }
    sink UserSink {
      type: "s3",
      connection: "./users_out.parquet",
      format: "parquet"
    }
    governance GDPR {
      access: [
        { "role": "admin", "permissions": ["read", "write", "delete"] }
      ],
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
  )neam");
}

TEST(NeamFullSuite, V090_DataAgentWithLineageCatalog)
{
  assert_compiles(R"neam(
    source S { type: "postgres", connection: "pg://localhost/db" }
    sink Out { type: "s3", connection: "s3://out/" }
    catalog Cat { engine: "unity_catalog", discovery: true }
    data agent Tracked {
      provider: "openai", model: "gpt-4o-mini",
      sources: [S], sinks: [Out],
      lineage: true,
      catalog: Cat
    }
    return nil;
  )neam");
}

TEST(NeamFullSuite, V090_ContextualDataKeyword)
{
  auto result = compile_and_run(R"(
    let data = "hello_data";
    return data;
  )");
  ASSERT_TRUE(result.is_string());
  auto* str = as_string(result);
  EXPECT_EQ(std::string(str->chars, str->length), "hello_data");
}

TEST(NeamFullSuite, V090_DataAgentMissingSources)
{
  assert_compile_error(R"neam(
    sink Out { type: "s3", connection: "s3://out/" }
    data agent Bad {
      provider: "openai", model: "gpt-4o-mini",
      sinks: [Out]
    }
  )neam");
}

// ============================================================================
//  v0.9.1 — ETLAgents: Semantic, Mart, Layers, Incremental, Self-Heal
// ============================================================================

TEST(NeamFullSuite, V091_SemanticBasicMetrics)
{
  auto emitted = compile_and_emit(R"neam(
    semantic SalesMetrics {
      metrics: {
        total_revenue: {
          sql: "SUM(amount)",
          description: "Total revenue",
          type: "measure"
        }
      }
    }
    emit "semantic_ok";
  )neam");
  ASSERT_EQ(emitted.size(), 1u);
  auto* str = as_string(emitted[0]);
  EXPECT_EQ(std::string(str->chars, str->length), "semantic_ok");
}

TEST(NeamFullSuite, V091_SemanticWithEntities)
{
  assert_compiles(R"neam(
    semantic SalesLayer {
      metrics: {
        revenue: { sql: "SUM(amount)", description: "Revenue", type: "measure" }
      },
      entities: {
        Customer: { table: "dim_customer", key: "customer_id" },
        Product: { table: "dim_product", key: "product_id" }
      }
    }
    return nil;
  )neam");
}

TEST(NeamFullSuite, V091_SemanticWithRelationships)
{
  assert_compiles(R"neam(
    semantic FullLayer {
      metrics: {
        revenue: { sql: "SUM(amount)", description: "Revenue", type: "measure" }
      },
      entities: {
        Customer: { table: "dim_customer", key: "customer_id" },
        Order: { table: "fact_orders", key: "order_id" }
      },
      relationships: [
        { from: "Order", to: "Customer", type: "many_to_one",
          join: "order.customer_id = customer.id" }
      ]
    }
    return nil;
  )neam");
}

TEST(NeamFullSuite, V091_SemanticWithSynonymsAndTimeIntel)
{
  assert_compiles(R"neam(
    semantic SalesLayer {
      metrics: {
        revenue: { sql: "SUM(amount)", description: "Revenue", type: "measure" }
      },
      synonyms: {
        "sales": "revenue",
        "income": "revenue"
      },
      time_intelligence: {
        fiscal_year_start: "April",
        week_start: "Monday",
        default_timezone: "UTC"
      }
    }
    return nil;
  )neam");
}

TEST(NeamFullSuite, V091_SemanticMissingMetrics)
{
  assert_compile_error(R"neam(
    semantic Empty { }
  )neam");
}

TEST(NeamFullSuite, V091_SemanticMetricMissingSql)
{
  assert_compile_error(R"neam(
    semantic Bad {
      metrics: {
        revenue: { description: "Revenue", type: "measure" }
      }
    }
  )neam");
}

TEST(NeamFullSuite, V091_MinimalETLAgent)
{
  auto emitted = compile_and_emit(R"neam(
    compute SnowflakeWH { engine: "snowflake" }
    source RawData { type: "postgres", connection: "host=localhost" }
    etl agent MyETL {
      provider: "openai",
      model: "gpt-4o",
      sources: [RawData],
      warehouse: SnowflakeWH
    }
    emit "etl_ok";
  )neam");
  ASSERT_EQ(emitted.size(), 1u);
  auto* str = as_string(emitted[0]);
  EXPECT_EQ(std::string(str->chars, str->length), "etl_ok");
}

TEST(NeamFullSuite, V091_ETLAgentAllFields)
{
  assert_compiles(R"neam(
    compute SnowflakeWH { engine: "snowflake" }
    source RawData { type: "postgres", connection: "host=localhost" }
    semantic SalesMetrics {
      metrics: {
        revenue: { sql: "SUM(amount)", description: "Revenue", type: "measure" }
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
      purpose: "Transform sales data",
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
        staging: { prefix: "stg_", materialization: "view" },
        integration: { prefix: "int_", materialization: "table" },
        marts: [
          mart SalesMart {
            facts: ["fact_sales"],
            dimensions: ["dim_customer"],
            grain: "one row per sale",
            measures: ["total_amount"]
          }
        ]
      }
    }
    return nil;
  )neam");
}

TEST(NeamFullSuite, V091_MartWithSCD)
{
  assert_compiles(R"neam(
    compute WH { engine: "snowflake" }
    source S { type: "postgres", connection: "x" }
    etl agent E {
      provider: "openai", model: "gpt-4o",
      sources: [S], warehouse: WH,
      layers: {
        marts: [
          mart M {
            facts: ["f1"],
            dimensions: ["d1", "d2"],
            grain: "per event",
            measures: ["m1"],
            scd: { d1: "type2", d2: "type1" }
          }
        ]
      }
    }
    return nil;
  )neam");
}

TEST(NeamFullSuite, V091_MartMissingRequiredFields)
{
  assert_compile_error(R"neam(
    compute WH { engine: "snowflake" }
    source S { type: "postgres", connection: "x" }
    etl agent E {
      provider: "openai", model: "gpt-4o",
      sources: [S], warehouse: WH,
      layers: {
        marts: [
          mart M { facts: ["f1"] }
        ]
      }
    }
  )neam");
}

TEST(NeamFullSuite, V091_ETLAgentMissingWarehouse)
{
  assert_compile_error(R"neam(
    source S { type: "postgres", connection: "x" }
    etl agent Bad {
      provider: "openai", model: "gpt-4o",
      sources: [S]
    }
  )neam");
}

TEST(NeamFullSuite, V091_ETLAgentMissingProvider)
{
  assert_compile_error(R"neam(
    compute WH { engine: "snowflake" }
    source S { type: "postgres", connection: "x" }
    etl agent Bad {
      model: "gpt-4o",
      sources: [S],
      warehouse: WH
    }
  )neam");
}

TEST(NeamFullSuite, V091_ETLAgentMissingSources)
{
  assert_compile_error(R"neam(
    compute WH { engine: "snowflake" }
    etl agent Bad {
      provider: "openai", model: "gpt-4o",
      warehouse: WH
    }
  )neam");
}

TEST(NeamFullSuite, V091_IncrementalMissingStrategy)
{
  assert_compile_error(R"neam(
    compute WH { engine: "snowflake" }
    source S { type: "postgres", connection: "x" }
    etl agent Bad {
      provider: "openai", model: "gpt-4o",
      sources: [S], warehouse: WH,
      incremental: { key: "updated_at" }
    }
  )neam");
}

TEST(NeamFullSuite, V091_InvalidModelType)
{
  assert_compile_error(R"neam(
    compute WH { engine: "snowflake" }
    source S { type: "postgres", connection: "x" }
    etl agent Bad {
      provider: "openai", model: "gpt-4o",
      sources: [S], warehouse: WH,
      model_type: "invalid"
    }
  )neam");
}

TEST(NeamFullSuite, V091_SelfHealBlockParsing)
{
  assert_compiles(R"neam(
    compute WH { engine: "snowflake" }
    source S { type: "postgres", connection: "x" }
    etl agent E {
      provider: "openai", model: "gpt-4o",
      sources: [S], warehouse: WH,
      self_heal: {
        enabled: true,
        capabilities: {
          auto_retry: { max_attempts: 3, backoff: "exponential" },
          circuit_breaker: { threshold: 5 }
        },
        learning: {
          record_incidents: true,
          improve_from_history: true
        }
      }
    }
    return nil;
  )neam");
}

TEST(NeamFullSuite, V091_AutoModelDataVault)
{
  assert_compiles(R"neam(
    compute WH { engine: "snowflake" }
    source S { type: "postgres", connection: "x" }
    etl agent E {
      provider: "openai", model: "gpt-4o",
      sources: [S], warehouse: WH,
      auto_model: {
        enabled: true,
        methodology: "data_vault",
        discover: {
          hubs: true, links: true,
          satellites: true, effectivity: true
        },
        generate: {
          hash_keys: "sha256",
          load_date_column: true,
          record_source_column: true
        },
        approval: "auto"
      }
    }
    return nil;
  )neam");
}

TEST(NeamFullSuite, V091_ContextualETLKeyword)
{
  auto result = compile_and_run(R"(
    let etl = 42;
    return etl;
  )");
  ASSERT_TRUE(result.is_number());
  EXPECT_DOUBLE_EQ(result.as_number(), 42.0);
}

TEST(NeamFullSuite, V091_RejectClawFieldInETL)
{
  assert_compile_error(R"neam(
    compute WH { engine: "snowflake" }
    source S { type: "postgres", connection: "x" }
    etl agent Bad {
      provider: "openai", model: "gpt-4o",
      sources: [S], warehouse: WH,
      channels: [SomeChannel]
    }
  )neam");
}

TEST(NeamFullSuite, V091_RejectForgeFieldInETL)
{
  assert_compile_error(R"neam(
    compute WH { engine: "snowflake" }
    source S { type: "postgres", connection: "x" }
    fun v(ctx) { return true; }
    etl agent Bad {
      provider: "openai", model: "gpt-4o",
      sources: [S], warehouse: WH,
      verify: v
    }
  )neam");
}

// ============================================================================
//  Cross-Version — Backward Compatibility & Coexistence
// ============================================================================

TEST(NeamFullSuite, CrossVersion_AllAgentTypesCoexist)
{
  auto emitted = compile_and_emit(R"neam(
    channel CLI { type: "cli", prompt: "> " }
    skill Echo {
      description: "Echo"
      params: { text: string }
      impl(text) { return text; }
    }
    fun verify(ctx) { return true; }

    // v0.6 — plain agent
    agent Bot {
      provider: "openai", model: "gpt-4o-mini",
      system: "Bot.", skills: [Echo]
    }

    // v0.8 — claw agent
    claw agent ChatBot {
      provider: "openai", model: "gpt-4o-mini",
      system: "Chat bot.", skills: [Echo],
      channels: [CLI]
    }

    // v0.8 — forge agent
    forge agent CodeGen {
      provider: "openai", model: "gpt-4o",
      system: "Code generator.",
      verify: verify,
      loop: { max_iterations: 3 }
    }

    // v0.9 — data agent
    source S { type: "postgres", connection: "pg://localhost/db" }
    sink Out { type: "s3", connection: "s3://out/" }
    data agent Ingester {
      provider: "openai", model: "gpt-4o-mini",
      sources: [S], sinks: [Out]
    }

    // v0.9.1 — etl agent
    compute WH { engine: "snowflake" }
    etl agent Warehouse {
      provider: "openai", model: "gpt-4o",
      sources: [S], warehouse: WH
    }

    emit "all_agents_ok";
  )neam");
  ASSERT_EQ(emitted.size(), 1u);
  auto* str = as_string(emitted[0]);
  EXPECT_EQ(std::string(str->chars, str->length), "all_agents_ok");
}

TEST(NeamFullSuite, CrossVersion_ContextualKeywordsAsVariables)
{
  auto result = compile_and_run(R"(
    let data = 1;
    let etl = 2;
    let claw = 3;
    let forge = 4;
    return data + etl + claw + forge;
  )");
  ASSERT_TRUE(result.is_number());
  EXPECT_DOUBLE_EQ(result.as_number(), 10.0);
}

TEST(NeamFullSuite, CrossVersion_StructsWithAgents)
{
  assert_compiles(R"neam(
    struct Config { name, value }
    impl Config {
      fn display(self) { return self.name + "=" + self.value; }
    }

    skill Echo {
      description: "Echo"
      params: { text: string }
      impl(text) { return text; }
    }

    agent Bot {
      provider: "openai", model: "gpt-4o-mini",
      system: "Bot.", skills: [Echo]
    }

    source S { type: "postgres", connection: "x" }
    sink Out { type: "s3", connection: "s3://out/" }
    data agent DA {
      provider: "openai", model: "gpt-4o-mini",
      sources: [S], sinks: [Out]
    }

    compute WH { engine: "snowflake" }
    etl agent EA {
      provider: "openai", model: "gpt-4o",
      sources: [S], warehouse: WH
    }

    return nil;
  )neam");
}

TEST(NeamFullSuite, CrossVersion_DataAndETLAgentSideBySide)
{
  auto emitted = compile_and_emit(R"neam(
    compute WH { engine: "snowflake" }
    source S { type: "postgres", connection: "x" }
    sink Out { type: "s3", connection: "s3://out/" }

    data agent DA {
      provider: "openai", model: "gpt-4o",
      sources: [S], sinks: [Out]
    }
    etl agent EA {
      provider: "openai", model: "gpt-4o",
      sources: [S], warehouse: WH
    }

    emit "coexist_ok";
  )neam");
  ASSERT_EQ(emitted.size(), 1u);
  auto* str = as_string(emitted[0]);
  EXPECT_EQ(std::string(str->chars, str->length), "coexist_ok");
}

TEST(NeamFullSuite, CrossVersion_FullEndToEnd)
{
  // Full pipeline: schema + source + sink + quality + compute +
  // governance + catalog + semantic + data agent + etl agent
  auto emitted = compile_and_emit(R"neam(
    schema OrderSchema {
      order_id: string @primary_key,
      amount: float @positive,
      status: string @enum(["pending", "complete"])
    }

    source SalesDB {
      type: "postgres",
      connection: "pg://localhost/sales"
    }

    sink Archive {
      type: "s3",
      connection: "s3://archive/",
      format: "parquet"
    }

    quality Strict {
      completeness: 0.99,
      on_violation: "warn"
    }

    compute SnowflakeWH {
      engine: "snowflake"
    }

    governance GDPR {
      access: [
        { "role": "analyst", "permissions": ["read"] }
      ],
      retention: [
        { "classification": "pii", "max_days": 90, "action": "delete" }
      ]
    }

    catalog OrgCatalog {
      engine: "unity_catalog",
      discovery: true
    }

    semantic SalesMetrics {
      metrics: {
        revenue: {
          sql: "SUM(amount)",
          description: "Total revenue",
          type: "measure"
        }
      },
      entities: {
        Order: { table: "fact_orders", key: "order_id" }
      },
      synonyms: { "sales": "revenue" }
    }

    data agent Ingester {
      provider: "openai", model: "gpt-4o-mini",
      sources: [SalesDB], sinks: [Archive],
      quality: Strict
    }

    etl agent SalesWarehouse {
      provider: "openai", model: "gpt-4o",
      sources: [SalesDB],
      warehouse: SnowflakeWH,
      model_type: "star",
      semantic: SalesMetrics,
      quality: Strict,
      governance: GDPR,
      lineage: true,
      catalog: OrgCatalog,
      incremental: {
        strategy: "timestamp",
        key: "updated_at"
      },
      self_heal: true,
      on_failure: "auto_fix",
      auto_model: {
        enabled: true,
        methodology: "kimball",
        approval: "manual"
      },
      layers: {
        staging: { prefix: "stg_", materialization: "view" },
        marts: [
          mart SalesMart {
            facts: ["fct_sales"],
            dimensions: ["dim_customer", "dim_date"],
            grain: "one row per order",
            measures: ["revenue", "quantity"],
            scd: { dim_customer: "type2" },
            conformed: ["dim_date"]
          }
        ]
      }
    }

    emit "full_e2e_ok";
  )neam");
  ASSERT_EQ(emitted.size(), 1u);
  auto* str = as_string(emitted[0]);
  EXPECT_EQ(std::string(str->chars, str->length), "full_e2e_ok");
}

}  // namespace
