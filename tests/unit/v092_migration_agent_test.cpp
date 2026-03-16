//
// Neam v0.9.2 — Migration Agent Unit Tests
//
// Tests for migration agent parsing, compiler validation (MIG-001 through MIG-015),
// strategy enums, sub-blocks, contextual keyword, VM registration, and cross-version compat.
//

#include <gtest/gtest.h>

#include "neamc/pipeline.hpp"
#include "neamc/vm/vm.hpp"

#include <string>

namespace
{
using namespace neamc;
using namespace neamc::vm;

// Helper: compile source and run
static Value compile_and_run(const std::string& source)
{
  Pipeline pipeline;
  auto unit = pipeline.compile(source, {});
  VirtualMachine vm;
  return vm.run(unit.chunk);
}

// Helper: assert compilation succeeds
static void assert_compiles(const std::string& source)
{
  Pipeline pipeline;
  ASSERT_NO_THROW(pipeline.compile(source, {}));
}

// Helper: assert compilation fails
static void assert_compile_error(const std::string& source)
{
  Pipeline pipeline;
  EXPECT_THROW(pipeline.compile(source, {}), std::exception);
}

// ============================================================================
// Minimal migration agent parsing
// ============================================================================

TEST(V092MigrationAgent, MinimalParsing)
{
  assert_compiles(R"neam(
    budget MigBudget { cost: 10.0 }
    migration agent TestMig {
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
}

// ============================================================================
// Full migration agent with all sub-blocks
// ============================================================================

TEST(V092MigrationAgent, FullDeclaration)
{
  assert_compiles(R"neam(
    budget MigBudget { cost: 50.0 }
    migration agent FullMig {
      provider: "openai",
      model: "gpt-4o",
      system: "You are a migration orchestrator.",
      temperature: 0.3,
      budget: MigBudget,
      source: SrcDB,
      target: TgtDB,
      staging: StagingArea,
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
}

// ============================================================================
// Strategy enum parsing
// ============================================================================

TEST(V092MigrationAgent, StrategyLiftAndShift)
{
  assert_compiles(R"neam(
    budget B { cost: 1.0 }
    migration agent M {
      provider: "openai", model: "gpt-4o-mini",
      source: S, target: T, staging: St,
      budget: B,
      strategy: "lift_and_shift"
    }
    return nil;
  )neam");
}

TEST(V092MigrationAgent, StrategyReArchitecture)
{
  assert_compiles(R"neam(
    budget B { cost: 1.0 }
    migration agent M {
      provider: "openai", model: "gpt-4o-mini",
      source: S, target: T, staging: St,
      budget: B,
      strategy: "re_architecture"
    }
    return nil;
  )neam");
}

TEST(V092MigrationAgent, InvalidStrategy)
{
  assert_compile_error(R"neam(
    budget B { cost: 1.0 }
    migration agent M {
      provider: "openai", model: "gpt-4o-mini",
      source: S, target: T, staging: St,
      budget: B,
      strategy: "invalid_strategy"
    }
    return nil;
  )neam");
}

// ============================================================================
// MIG-001: source and target required
// ============================================================================

TEST(V092MigrationAgent, MIG001_MissingSource)
{
  assert_compile_error(R"neam(
    budget B { cost: 1.0 }
    migration agent M {
      provider: "openai", model: "gpt-4o-mini",
      target: T, staging: St,
      budget: B
    }
    return nil;
  )neam");
}

TEST(V092MigrationAgent, MIG001_MissingTarget)
{
  assert_compile_error(R"neam(
    budget B { cost: 1.0 }
    migration agent M {
      provider: "openai", model: "gpt-4o-mini",
      source: S, staging: St,
      budget: B
    }
    return nil;
  )neam");
}

// ============================================================================
// MIG-005: rollback window > 0
// ============================================================================

TEST(V092MigrationAgent, MIG005_ZeroRollbackWindow)
{
  assert_compile_error(R"neam(
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
  )neam");
}

// ============================================================================
// MIG-006: row_counts required
// ============================================================================

TEST(V092MigrationAgent, MIG006_NoRowCounts)
{
  assert_compile_error(R"neam(
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
  )neam");
}

// ============================================================================
// MIG-008: budget required
// ============================================================================

TEST(V092MigrationAgent, MIG008_MissingBudget)
{
  assert_compile_error(R"neam(
    migration agent M {
      provider: "openai", model: "gpt-4o-mini",
      source: S, target: T, staging: St
    }
    return nil;
  )neam");
}

// ============================================================================
// MIG-009: staging required for full_dump
// ============================================================================

TEST(V092MigrationAgent, MIG009_NoStagingFullDump)
{
  assert_compile_error(R"neam(
    budget B { cost: 1.0 }
    migration agent M {
      provider: "openai", model: "gpt-4o-mini",
      source: S, target: T,
      budget: B,
      movement: { strategy: "full_dump" }
    }
    return nil;
  )neam");
}

TEST(V092MigrationAgent, MIG009_NoStagingTrickleOK)
{
  // trickle strategy doesn't require staging
  assert_compiles(R"neam(
    budget B { cost: 1.0 }
    migration agent M {
      provider: "openai", model: "gpt-4o-mini",
      source: S, target: T,
      budget: B,
      movement: { strategy: "trickle" }
    }
    return nil;
  )neam");
}

// ============================================================================
// MIG-013: financial_columns must be exact
// ============================================================================

TEST(V092MigrationAgent, MIG013_NonExactFinancial)
{
  assert_compile_error(R"neam(
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
  )neam");
}

// ============================================================================
// Contextual keyword: "migration" as variable name
// ============================================================================

TEST(V092MigrationAgent, ContextualKeyword)
{
  // "migration" can be used as a variable when not followed by "agent"
  assert_compiles(R"neam(
    let migration = 42;
    emit migration;
  )neam");
}

// ============================================================================
// VM registration
// ============================================================================

TEST(V092MigrationAgent, VMRegistration)
{
  // Verify agent is registered and can be looked up without crash
  auto result = compile_and_run(R"neam(
    budget MigBudget { cost: 10.0 }
    migration agent TestMig {
      provider: "openai",
      model: "gpt-4o-mini",
      source: OracleDB,
      target: SnowflakeDB,
      staging: S3Staging,
      budget: MigBudget
    }
    return nil;
  )neam");
  EXPECT_TRUE(result.is_nil());
}

// ============================================================================
// Cross-version compatibility (v0.9/v0.9.1 constructs still work)
// ============================================================================

TEST(V092MigrationAgent, CrossVersionDataAgent)
{
  assert_compiles(R"neam(
    data agent SimpleData {
      provider: "openai",
      model: "gpt-4o-mini",
      sources: [SomeSource]
    }
    return nil;
  )neam");
}

TEST(V092MigrationAgent, CrossVersionETLAgent)
{
  assert_compiles(R"neam(
    etl agent SimpleETL {
      provider: "openai",
      model: "gpt-4o-mini",
      sources: [SomeSource],
      warehouse: MyWarehouse
    }
    return nil;
  )neam");
}

// ============================================================================
// Movement strategies
// ============================================================================

TEST(V092MigrationAgent, MovementBlueGreen)
{
  assert_compiles(R"neam(
    budget B { cost: 1.0 }
    migration agent M {
      provider: "openai", model: "gpt-4o-mini",
      source: S, target: T,
      budget: B,
      movement: { strategy: "blue_green" }
    }
    return nil;
  )neam");
}

TEST(V092MigrationAgent, MovementParallelRun)
{
  assert_compiles(R"neam(
    budget B { cost: 1.0 }
    migration agent M {
      provider: "openai", model: "gpt-4o-mini",
      source: S, target: T,
      budget: B,
      movement: { strategy: "parallel_run" }
    }
    return nil;
  )neam");
}

// ============================================================================
// Cutover strategies
// ============================================================================

TEST(V092MigrationAgent, CutoverCanary)
{
  assert_compiles(R"neam(
    budget B { cost: 1.0 }
    migration agent M {
      provider: "openai", model: "gpt-4o-mini",
      source: S, target: T, staging: St,
      budget: B,
      cutover: { strategy: "canary" }
    }
    return nil;
  )neam");
}

TEST(V092MigrationAgent, CutoverTrickle)
{
  assert_compiles(R"neam(
    budget B { cost: 1.0 }
    migration agent M {
      provider: "openai", model: "gpt-4o-mini",
      source: S, target: T, staging: St,
      budget: B,
      cutover: { strategy: "trickle" }
    }
    return nil;
  )neam");
}

}  // namespace
