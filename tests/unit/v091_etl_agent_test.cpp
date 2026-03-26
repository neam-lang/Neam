//
// Neam v0.9.1 — ETL Agent Unit Tests
//
// Tests for ETL agent features: semantic layer, mart, etl agent parsing,
// contextual keyword, backward compatibility, field rejection,
// compiler emit, and VM opcode handlers.
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
// Semantic layer parsing
// ============================================================================

TEST(V091ETLAgent, SemanticBasicMetrics)
{
  assert_compiles(R"neam(
    semantic SalesMetrics {
      metrics: {
        total_revenue: {
          sql: "SUM(amount)",
          description: "Total revenue",
          type: "measure"
        }
      }
    }
    return nil;
  )neam");
}

TEST(V091ETLAgent, SemanticWithEntities)
{
  assert_compiles(R"neam(
    semantic SalesLayer {
      metrics: {
        revenue: {
          sql: "SUM(amount)",
          description: "Revenue",
          type: "measure"
        }
      },
      entities: {
        Customer: {
          table: "dim_customer",
          key: "customer_id"
        }
      }
    }
    return nil;
  )neam");
}

TEST(V091ETLAgent, SemanticWithRelationships)
{
  assert_compiles(R"neam(
    semantic FullLayer {
      metrics: {
        revenue: {
          sql: "SUM(amount)",
          description: "Revenue",
          type: "measure"
        }
      },
      entities: {
        Customer: {
          table: "dim_customer",
          key: "customer_id"
        },
        Order: {
          table: "fact_orders",
          key: "order_id"
        }
      },
      relationships: [
        { from: "Order", to: "Customer", type: "many_to_one", join: "order.customer_id = customer.id" }
      ]
    }
    return nil;
  )neam");
}

TEST(V091ETLAgent, SemanticWithSynonyms)
{
  assert_compiles(R"neam(
    semantic SalesLayer {
      metrics: {
        revenue: {
          sql: "SUM(amount)",
          description: "Revenue",
          type: "measure"
        }
      },
      synonyms: {
        "sales": "revenue",
        "income": "revenue"
      }
    }
    return nil;
  )neam");
}

TEST(V091ETLAgent, SemanticWithTimeIntelligence)
{
  assert_compiles(R"neam(
    semantic SalesLayer {
      metrics: {
        revenue: {
          sql: "SUM(amount)",
          description: "Revenue",
          type: "measure"
        }
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

TEST(V091ETLAgent, SemanticMissingMetrics)
{
  assert_compile_error(R"neam(
    semantic EmptyLayer {
    }
  )neam");
}

TEST(V091ETLAgent, SemanticMetricMissingSql)
{
  assert_compile_error(R"neam(
    semantic Bad {
      metrics: {
        revenue: {
          description: "Revenue",
          type: "measure"
        }
      }
    }
  )neam");
}

// ============================================================================
// Mart parsing
// ============================================================================

TEST(V091ETLAgent, MartInsideETLAgent)
{
  assert_compiles(R"neam(
    compute SnowflakeWH {
      engine: "snowflake"
    }
    source RawData {
      type: "postgres",
      connection: "host=localhost"
    }
    etl agent SalesETL {
      provider: "openai",
      model: "gpt-4o",
      sources: [RawData],
      warehouse: SnowflakeWH,
      layers: {
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
}

TEST(V091ETLAgent, MartWithSCD)
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
            scd: {
              d1: "type2",
              d2: "type1"
            }
          }
        ]
      }
    }
    return nil;
  )neam");
}

TEST(V091ETLAgent, MartMissingRequiredFields)
{
  assert_compile_error(R"neam(
    compute WH { engine: "snowflake" }
    source S { type: "postgres", connection: "x" }
    etl agent E {
      provider: "openai", model: "gpt-4o",
      sources: [S], warehouse: WH,
      layers: {
        marts: [
          mart M {
            facts: ["f1"]
          }
        ]
      }
    }
  )neam");
}

// ============================================================================
// ETL agent parsing
// ============================================================================

TEST(V091ETLAgent, MinimalETLAgent)
{
  assert_compiles(R"neam(
    compute SnowflakeWH { engine: "snowflake" }
    source RawData { type: "postgres", connection: "host=localhost" }
    etl agent MyETL {
      provider: "openai",
      model: "gpt-4o",
      sources: [RawData],
      warehouse: SnowflakeWH
    }
    return nil;
  )neam");
}

TEST(V091ETLAgent, ETLAgentAllFields)
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

TEST(V091ETLAgent, ETLAgentMissingProvider)
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

TEST(V091ETLAgent, ETLAgentMissingWarehouse)
{
  assert_compile_error(R"neam(
    source S { type: "postgres", connection: "x" }
    etl agent Bad {
      provider: "openai",
      model: "gpt-4o",
      sources: [S]
    }
  )neam");
}

TEST(V091ETLAgent, ETLAgentMissingSources)
{
  assert_compile_error(R"neam(
    compute WH { engine: "snowflake" }
    etl agent Bad {
      provider: "openai",
      model: "gpt-4o",
      warehouse: WH
    }
  )neam");
}

// ============================================================================
// Contextual keyword — "etl" as variable
// ============================================================================

TEST(V091ETLAgent, ETLAsVariable)
{
  auto result = compile_and_run(R"neam(
    let etl = 42;
    return etl;
  )neam");
  ASSERT_TRUE(result.is_number());
  EXPECT_DOUBLE_EQ(result.as_number(), 42.0);
}

// ============================================================================
// Backward compatibility
// ============================================================================

TEST(V091ETLAgent, DataAgentStillWorks)
{
  assert_compiles(R"neam(
    source S { type: "postgres", connection: "x" }
    sink Out { type: "s3", connection: "s3://out/" }
    data agent D {
      provider: "openai",
      model: "gpt-4o",
      sources: [S],
      sinks: [Out]
    }
    return nil;
  )neam");
}

TEST(V091ETLAgent, PlainAgentStillWorks)
{
  assert_compiles(R"neam(
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
  )neam");
}

// ============================================================================
// Field rejection
// ============================================================================

TEST(V091ETLAgent, RejectClawFieldInETL)
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

TEST(V091ETLAgent, RejectForgeFieldInETL)
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
// Compiler emit + validation tests
// ============================================================================

TEST(V091ETLAgent, CompileSemanticLayer)
{
  assert_compiles(R"neam(
    semantic SalesMetrics {
      metrics: {
        revenue: { sql: "SUM(amount)", description: "Revenue", type: "measure" }
      }
    }
    return nil;
  )neam");
}

TEST(V091ETLAgent, CompileMinimalETLAgent)
{
  assert_compiles(R"neam(
    compute WH { engine: "snowflake" }
    source S { type: "postgres", connection: "x" }
    etl agent E {
      provider: "openai",
      model: "gpt-4o",
      sources: [S],
      warehouse: WH
    }
    return nil;
  )neam");
}

TEST(V091ETLAgent, CompileFullETLAgent)
{
  assert_compiles(R"neam(
    compute WH { engine: "snowflake" }
    source S { type: "postgres", connection: "x" }
    semantic SM {
      metrics: {
        revenue: { sql: "SUM(amount)", description: "Revenue", type: "measure" }
      }
    }
    etl agent FullETL {
      provider: "openai",
      model: "gpt-4o",
      sources: [S],
      warehouse: WH,
      model_type: "star",
      semantic: SM,
      incremental: {
        strategy: "timestamp",
        key: "updated_at"
      },
      self_heal: true,
      auto_model: {
        enabled: true,
        methodology: "kimball"
      },
      layers: {
        staging: { prefix: "stg_" },
        marts: [
          mart M {
            facts: ["f"],
            dimensions: ["d"],
            grain: "per row",
            measures: ["m"]
          }
        ]
      }
    }
    return nil;
  )neam");
}

// ============================================================================
// Incremental block validation
// ============================================================================

TEST(V091ETLAgent, IncrementalMissingStrategy)
{
  assert_compile_error(R"neam(
    compute WH { engine: "snowflake" }
    source S { type: "postgres", connection: "x" }
    etl agent Bad {
      provider: "openai", model: "gpt-4o",
      sources: [S], warehouse: WH,
      incremental: {
        key: "updated_at"
      }
    }
  )neam");
}

TEST(V091ETLAgent, InvalidModelType)
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

// ============================================================================
// Self-heal block
// ============================================================================

TEST(V091ETLAgent, SelfHealBlockParsing)
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
          auto_retry: {
            max_attempts: 3,
            backoff: "exponential"
          },
          circuit_breaker: {
            threshold: 5
          }
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

// ============================================================================
// Auto-model discover/generate
// ============================================================================

TEST(V091ETLAgent, AutoModelFullConfig)
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
          hubs: true,
          links: true,
          satellites: true,
          effectivity: true
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

// ============================================================================
// VM opcode handlers (compile + run, check registration in globals)
// ============================================================================

TEST(V091ETLAgent, ETLAgentRegistersInVM)
{
  auto emitted = compile_and_emit(R"neam(
    compute WH { engine: "snowflake" }
    source S { type: "postgres", connection: "x" }
    etl agent TestETL {
      provider: "openai",
      model: "gpt-4o",
      sources: [S],
      warehouse: WH
    }
    emit "etl_registered";
  )neam");

  ASSERT_EQ(emitted.size(), 1u);
  ASSERT_TRUE(is_obj_type(emitted[0], ObjType::OBJ_STRING));
  auto* str = as_string(emitted[0]);
  EXPECT_EQ(std::string(str->chars, str->length), "etl_registered");
}

TEST(V091ETLAgent, SemanticRegistersInVM)
{
  auto emitted = compile_and_emit(R"neam(
    semantic TestSemantic {
      metrics: {
        revenue: { sql: "SUM(amount)", description: "Revenue", type: "measure" }
      }
    }
    emit "semantic_ok";
  )neam");

  ASSERT_EQ(emitted.size(), 1u);
  auto* str = as_string(emitted[0]);
  EXPECT_EQ(std::string(str->chars, str->length), "semantic_ok");
}

TEST(V091ETLAgent, FullETLAgentRunsInVM)
{
  auto emitted = compile_and_emit(R"neam(
    compute WH { engine: "snowflake" }
    source S { type: "postgres", connection: "x" }
    semantic SM {
      metrics: {
        revenue: { sql: "SUM(amount)", description: "Revenue", type: "measure" }
      }
    }
    etl agent FullETL {
      provider: "openai",
      model: "gpt-4o",
      sources: [S],
      warehouse: WH,
      model_type: "star",
      semantic: SM,
      self_heal: true,
      layers: {
        staging: { prefix: "stg_" },
        marts: [
          mart M {
            facts: ["f"],
            dimensions: ["d"],
            grain: "per row",
            measures: ["m"]
          }
        ]
      }
    }
    emit "full_etl_ok";
  )neam");

  ASSERT_EQ(emitted.size(), 1u);
  auto* str = as_string(emitted[0]);
  EXPECT_EQ(std::string(str->chars, str->length), "full_etl_ok");
}

TEST(V091ETLAgent, ETLAndDataAgentCoexistInVM)
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

}  // namespace
