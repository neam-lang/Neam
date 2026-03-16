//
// Neam v0.9.5 — Modeling Agent Unit Tests
//
// Tests for Modeling agent parsing, compiler validation (MOD-001 through MOD-015),
// sub-declarations, contextual keywords, VM registration, native functions,
// and cross-version compatibility.
//

#include <gtest/gtest.h>

#include "neamc/pipeline.hpp"
#include "neamc/vm/vm.hpp"

#include <string>

namespace
{
using namespace neamc;
using namespace neamc::vm;

// Helper: compile source and run (VM is destroyed so returned ObjString* is dangling)
static Value compile_and_run(const std::string& source)
{
  Pipeline pipeline;
  auto unit = pipeline.compile(source, {});
  VirtualMachine vm;
  return vm.run(unit.chunk);
}

// Helper: compile, run, and return string result (safe for string values)
struct RunResult {
  std::string str_value;
  bool is_string{false};
  bool is_nil{false};
  bool is_obj{false};
  double num_value{0.0};
  bool bool_value{false};
};

static RunResult compile_and_run_safe(const std::string& source)
{
  Pipeline pipeline;
  auto unit = pipeline.compile(source, {});
  VirtualMachine vm;
  Value result = vm.run(unit.chunk);
  RunResult r;
  r.is_nil = result.is_nil();
  r.is_obj = result.is_obj();
  if (result.is_number()) r.num_value = result.as_number();
  if (result.is_bool()) r.bool_value = result.as_bool();
  if (result.is_obj() && is_obj_type(result, ObjType::OBJ_STRING)) {
    auto* s = as_string(result);
    r.str_value = std::string(s->chars, s->length);
    r.is_string = true;
  }
  return r;
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
// SchemaSource parsing
// ============================================================================

TEST(V095ModelingAgent, SchemaSourceDatabaseParsing)
{
  assert_compiles(R"neam(
    schema_source WarehouseSchema {
      type: "snowflake",
      connection: "snowflake://account.region",
      credentials: "SNOWFLAKE_CREDS",
      databases: ["ANALYTICS", "RAW_VAULT"],
      scan_interval: "1h",
      include_views: true,
      read_constraints: true,
      read_indexes: true
    }
    return nil;
  )neam");
}

TEST(V095ModelingAgent, SchemaSourceErwinParsing)
{
  assert_compiles(R"neam(
    schema_source ErwinModel {
      type: "erwin",
      path: "./models/enterprise.erwin",
      format: "xml",
      model_type: "physical_logical",
      watch: true,
      sync_direction: "bidirectional"
    }
    return nil;
  )neam");
}

TEST(V095ModelingAgent, SchemaSourceDataLakeParsing)
{
  assert_compiles(R"neam(
    schema_source DataLake {
      type: "s3",
      connection: "s3://data-lake-bucket",
      credentials: "AWS_PROFILE",
      prefixes: ["/raw/events/", "/curated/customers/"],
      sample_size: 5000,
      detect_formats: true
    }
    return nil;
  )neam");
}

TEST(V095ModelingAgent, SchemaSourceDbtParsing)
{
  assert_compiles(R"neam(
    schema_source DbtProject {
      type: "dbt",
      project_path: "./dbt_project",
      manifest_path: "./target/manifest.json",
      read_sources: true,
      read_models: true,
      read_tests: true
    }
    return nil;
  )neam");
}

TEST(V095ModelingAgent, SchemaSourceAllTypes)
{
  for (const auto& t : {"snowflake", "oracle", "postgres", "mysql", "sqlserver",
                         "redshift", "bigquery", "databricks",
                         "s3", "adls", "gcs", "hdfs",
                         "erwin", "erstudio", "powerdesigner", "sparx_ea",
                         "ddl", "dbt", "data_lake", "delta_lake", "iceberg", "hudi"})
  {
    assert_compiles(std::string(R"neam(
      schema_source TestSrc {
        type: ")neam") + t + R"neam(",
        connection: "test://conn"
      }
      return nil;
    )neam");
  }
}

// ============================================================================
// ERModel parsing
// ============================================================================

TEST(V095ModelingAgent, ERModelParsing)
{
  assert_compiles(R"neam(
    schema_source TestSrc {
      type: "snowflake",
      connection: "test://conn"
    }
    er_model EnterpriseDataModel {
      version: "3.2",
      levels: ["conceptual", "logical", "physical"],
      source: TestSrc,
      notation: {
        conceptual: "chen",
        logical: "idef1x",
        physical: "crows_foot"
      }
    }
    return nil;
  )neam");
}

TEST(V095ModelingAgent, ERModelWithDomains)
{
  assert_compiles(R"neam(
    schema_source TestSrc {
      type: "postgres",
      connection: "test://conn"
    }
    er_model DomainModel {
      version: "1.0",
      levels: ["logical"],
      source: TestSrc,
      domains: {
        Sales: {
          entities: ["Customer", "Order", "Product"],
          color: "#4A90D9"
        },
        Finance: {
          entities: ["Invoice", "Payment"],
          color: "#7ED321"
        }
      }
    }
    return nil;
  )neam");
}

// ============================================================================
// Entity parsing
// ============================================================================

TEST(V095ModelingAgent, EntityParsing)
{
  assert_compiles(R"neam(
    entity Customer {
      domain: "Sales",
      attributes: {
        customer_id: { type: "identifier", primary_key: true },
        name: { type: "string", max_length: 200 },
        email: { type: "string", classification: "pii" }
      },
      relationships: {
        places: { target: "Order", cardinality: "1:N" },
        belongs_to: { target: "Region", cardinality: "N:1" }
      },
      glossary_term: "Customer",
      owner: "sales_team",
      description: "A person or organization that purchases products"
    }
    return nil;
  )neam");
}

TEST(V095ModelingAgent, EntityMinimal)
{
  assert_compiles(R"neam(
    entity Product {
      domain: "Sales",
      attributes: {
        product_id: { type: "identifier", primary_key: true }
      }
    }
    return nil;
  )neam");
}

// ============================================================================
// DimensionalModel parsing
// ============================================================================

TEST(V095ModelingAgent, DimensionalModelStarParsing)
{
  assert_compiles(R"neam(
    schema_source TestSrc {
      type: "snowflake",
      connection: "test://conn"
    }
    dimensional_model SalesAnalytics {
      methodology: "star",
      source: TestSrc,
      facts: {
        FCT_SALES: {
          grain: "one row per order line item",
          measures: {
            quantity: { type: "additive" },
            amount: { type: "additive" },
            discount_pct: { type: "non_additive" }
          }
        }
      },
      dimensions: {
        DIM_DATE: { scd_type: 0, role: "transaction_date" },
        DIM_CUSTOMER: { scd_type: 2 },
        DIM_PRODUCT: { scd_type: 1 }
      },
      conformed: [DIM_DATE, DIM_CUSTOMER],
      target_platform: "snowflake",
      target_schema: "ANALYTICS.SALES_MART"
    }
    return nil;
  )neam");
}

TEST(V095ModelingAgent, DimensionalModelSnowflakeParsing)
{
  assert_compiles(R"neam(
    schema_source TestSrc {
      type: "postgres",
      connection: "test://conn"
    }
    dimensional_model HRAnalytics {
      methodology: "snowflake",
      source: TestSrc,
      facts: {
        FCT_ATTENDANCE: {
          grain: "one row per employee per day"
        }
      },
      dimensions: {
        DIM_EMPLOYEE: { scd_type: 2 },
        DIM_DEPARTMENT: { scd_type: 1 }
      },
      target_platform: "postgres"
    }
    return nil;
  )neam");
}

TEST(V095ModelingAgent, DimensionalModelDataVaultParsing)
{
  assert_compiles(R"neam(
    schema_source TestSrc {
      type: "snowflake",
      connection: "test://conn"
    }
    dimensional_model VaultModel {
      methodology: "data_vault",
      source: TestSrc,
      facts: {
        SAT_CUSTOMER_DETAILS: {
          grain: "one row per change event"
        }
      },
      dimensions: {
        HUB_CUSTOMER: { scd_type: 0 },
        LNK_CUSTOMER_ORDER: { scd_type: 0 }
      }
    }
    return nil;
  )neam");
}

TEST(V095ModelingAgent, DimensionalModelAllMethodologies)
{
  for (const auto& m : {"star", "snowflake", "starflake", "data_vault"})
  {
    assert_compiles(std::string(R"neam(
      schema_source TestSrc {
        type: "postgres",
        connection: "test://conn"
      }
      dimensional_model TestModel {
        methodology: ")neam") + m + R"neam(",
        source: TestSrc,
        facts: { FCT_TEST: { grain: "test" } },
        dimensions: { DIM_TEST: { scd_type: 1 } }
      }
      return nil;
    )neam");
  }
}

// ============================================================================
// DataMart parsing
// ============================================================================

TEST(V095ModelingAgent, DataMartParsing)
{
  assert_compiles(R"neam(
    schema_source TestSrc {
      type: "snowflake",
      connection: "test://conn"
    }
    dimensional_model SalesModel {
      methodology: "star",
      source: TestSrc,
      facts: { FCT_SALES: { grain: "line item" } },
      dimensions: { DIM_DATE: { scd_type: 0 } }
    }
    datamart FinanceMart {
      dimensional_model: SalesModel,
      purpose: "Finance team P&L reporting",
      owner: "finance_data_team",
      facts: [FCT_SALES],
      dimensions: [DIM_DATE],
      materialization: {
        facts: "incremental",
        dimensions: "table"
      }
    }
    return nil;
  )neam");
}

// ============================================================================
// NormalizationAnalysis parsing
// ============================================================================

TEST(V095ModelingAgent, NormalizationAnalysisParsing)
{
  assert_compiles(R"neam(
    normalization_analysis NormCheck {
      scope: {
        schemas: ["PUBLIC", "STAGING"],
        exclude_patterns: ["STG_%", "TMP_%"],
        exclude_marts: true
      },
      target_nf: "3NF",
      fd_discovery: {
        method: "data_profiling",
        sample_size: 10000
      },
      report: {
        violations_only: false,
        include_decomposition: true
      },
      on_violation: "report"
    }
    return nil;
  )neam");
}

TEST(V095ModelingAgent, NormalizationAllForms)
{
  for (const auto& nf : {"1NF", "2NF", "3NF", "BCNF", "4NF", "5NF"})
  {
    assert_compiles(std::string(R"neam(
      normalization_analysis TestNorm {
        target_nf: ")neam") + nf + R"neam("
      }
      return nil;
    )neam");
  }
}

// ============================================================================
// AmendmentConfig parsing
// ============================================================================

TEST(V095ModelingAgent, AmendmentConfigParsing)
{
  assert_compiles(R"neam(
    amendment_config ChangeManagement {
      monitor: {
        databases: ["ANALYTICS"],
        modeling_tools: ["ErwinSync"]
      },
      change_types: {
        non_breaking: ["add_column", "add_table"],
        breaking: ["drop_column", "rename_column"]
      },
      approval: {
        non_breaking: "auto_approve",
        breaking: "committee"
      },
      document: {
        format: "markdown",
        include_ddl: true
      }
    }
    return nil;
  )neam");
}

// ============================================================================
// Amendment parsing
// ============================================================================

TEST(V095ModelingAgent, AmendmentParsing)
{
  assert_compiles(R"neam(
    schema_source TestSrc {
      type: "snowflake",
      connection: "test://conn"
    }
    er_model TestModel {
      version: "1.0",
      levels: ["physical"],
      source: TestSrc
    }
    amendment SalesSchemaV4 {
      model: TestModel,
      type: "breaking",
      description: "Split CUSTOMERS table to support multi-address",
      changes: {
        action: "create_table",
        table: "CUSTOMER_ADDRESSES"
      },
      auto_analyze: true,
      require_approval: true
    }
    return nil;
  )neam");
}

TEST(V095ModelingAgent, AmendmentAllTypes)
{
  for (const auto& t : {"non_breaking", "breaking", "requires_review"})
  {
    assert_compiles(std::string(R"neam(
      schema_source TestSrc {
        type: "snowflake",
        connection: "test://conn"
      }
      er_model TestModel {
        version: "1.0",
        levels: ["physical"],
        source: TestSrc
      }
      amendment TestAmendment {
        model: TestModel,
        type: ")neam") + t + R"neam(",
        description: "Test amendment"
      }
      return nil;
    )neam");
  }
}

// ============================================================================
// DataProfile parsing
// ============================================================================

TEST(V095ModelingAgent, DataProfileParsing)
{
  assert_compiles(R"neam(
    data_profile LakeProfile {
      sources: {
        structured: ["ANALYTICS.PUBLIC"],
        semi_structured: {
          s3: "s3://data-lake/json/",
          sample_size: 5000
        }
      },
      profiling: {
        infer_types: true,
        detect_pii: true,
        compute_statistics: true,
        sample_size: 10000
      },
      output: {
        register_in_catalog: true,
        generate_entity_model: true
      }
    }
    return nil;
  )neam");
}

// ============================================================================
// ModelingTool parsing
// ============================================================================

TEST(V095ModelingAgent, ModelingToolErwinParsing)
{
  assert_compiles(R"neam(
    modeling_tool ErwinSync {
      type: "erwin",
      path: "./models/enterprise.erwin",
      credentials: "ERWIN_CREDS",
      submodels: ["Sales", "Finance", "HR"],
      sync: {
        direction: "bidirectional",
        conflict_resolution: "database_wins",
        auto_sync_interval: "1h"
      },
      mapping: {
        physical_to_logical: true,
        naming_standard: {
          tables: "pascal_case",
          columns: "snake_case"
        }
      },
      on_conflict: {
        notify: "slack_channel",
        require_resolution: true
      }
    }
    return nil;
  )neam");
}

TEST(V095ModelingAgent, ModelingToolDbtParsing)
{
  assert_compiles(R"neam(
    modeling_tool DbtSync {
      type: "dbt",
      path: "./dbt_project",
      sync: {
        direction: "bidirectional"
      }
    }
    return nil;
  )neam");
}

TEST(V095ModelingAgent, ModelingToolAllTypes)
{
  for (const auto& t : {"erwin", "erstudio", "powerdesigner", "sparx_ea",
                         "oracle_sddm", "dbdiagram", "dbt", "liquibase", "flyway"})
  {
    assert_compiles(std::string(R"neam(
      modeling_tool TestTool {
        type: ")neam") + t + R"neam(",
        path: "./test/model"
      }
      return nil;
    )neam");
  }
}

// ============================================================================
// Modeling Agent parsing
// ============================================================================

TEST(V095ModelingAgent, ModelingAgentMinimal)
{
  assert_compiles(R"neam(
    schema_source TestSrc {
      type: "snowflake",
      connection: "test://conn"
    }
    modeling agent TestArchitect {
      provider: "openai",
      model: "gpt-4o-mini",
      sources: [TestSrc]
    }
    return nil;
  )neam");
}

TEST(V095ModelingAgent, ModelingAgentFull)
{
  assert_compiles(R"neam(
    budget ModelingBudget { cost: 50.00, tokens: 1000000 }

    schema_source WarehouseSchema {
      type: "snowflake",
      connection: "test://conn",
      databases: ["ANALYTICS"],
      scan_interval: "1h"
    }

    schema_source ErwinModel {
      type: "erwin",
      path: "./models/enterprise.erwin",
      format: "xml",
      sync_direction: "bidirectional"
    }

    modeling_tool ErwinSync {
      type: "erwin",
      path: "./models/enterprise.erwin",
      sync: { direction: "bidirectional" }
    }

    modeling agent DataArchitect {
      provider: "anthropic",
      model: "claude-opus-4-6",
      sources: [WarehouseSchema, ErwinModel],
      modeling_tools: [ErwinSync],
      capabilities: {
        reverse_engineer: true,
        normalization_analysis: {
          target_nf: "3NF",
          exclude_marts: true
        },
        dimensional_design: {
          default_methodology: "star"
        },
        amendment_proposals: true,
        data_profiling: {
          detect_pii: true
        }
      },
      budget: ModelingBudget,
      enrich_from_governance: true,
      role: "data_architect",
      purpose: "Enterprise data modeling and schema governance"
    }
    return nil;
  )neam");
}

TEST(V095ModelingAgent, ModelingAgentWithCoordination)
{
  assert_compiles(R"neam(
    schema_source TestSrc {
      type: "postgres",
      connection: "test://conn"
    }
    modeling agent CoordinatedArchitect {
      provider: "openai",
      model: "gpt-4o-mini",
      sources: [TestSrc],
      coordinates_with: [ETLPipeline, GovernanceManager],
      enrich_from_governance: true
    }
    return nil;
  )neam");
}

// ============================================================================
// Native function tests
// ============================================================================

TEST(V095ModelingAgent, NativeModelingStatus)
{
  auto result = compile_and_run_safe(R"neam(
    schema_source TestSrc {
      type: "snowflake",
      connection: "test://conn"
    }
    modeling agent TestAgent {
      provider: "openai",
      model: "gpt-4o-mini",
      sources: [TestSrc]
    }
    return modeling_status(TestAgent);
  )neam");
  ASSERT_TRUE(result.is_string);
  EXPECT_EQ(result.str_value, "initialized");
}

TEST(V095ModelingAgent, NativeModelingReport)
{
  auto result = compile_and_run(R"neam(
    schema_source TestSrc {
      type: "snowflake",
      connection: "test://conn"
    }
    modeling agent TestAgent {
      provider: "openai",
      model: "gpt-4o-mini",
      sources: [TestSrc]
    }
    return modeling_report(TestAgent);
  )neam");
  // Report returns a map object
  ASSERT_TRUE(result.is_obj());
}

TEST(V095ModelingAgent, NativeModelingReverseEngineer)
{
  auto result = compile_and_run_safe(R"neam(
    schema_source TestSrc {
      type: "snowflake",
      connection: "test://conn"
    }
    modeling agent TestAgent {
      provider: "openai",
      model: "gpt-4o-mini",
      sources: [TestSrc]
    }
    return modeling_reverse_engineer(TestAgent, "TestSrc");
  )neam");
  ASSERT_TRUE(result.is_string);
}

TEST(V095ModelingAgent, NativeModelingAnalyzeNF)
{
  auto result = compile_and_run_safe(R"neam(
    schema_source TestSrc {
      type: "snowflake",
      connection: "test://conn"
    }
    modeling agent TestAgent {
      provider: "openai",
      model: "gpt-4o-mini",
      sources: [TestSrc]
    }
    return modeling_normalize(TestAgent, "TestSrc", "3NF");
  )neam");
  ASSERT_TRUE(result.is_string);
}

TEST(V095ModelingAgent, NativeModelingDetectChanges)
{
  auto result = compile_and_run_safe(R"neam(
    schema_source TestSrc {
      type: "snowflake",
      connection: "test://conn"
    }
    modeling agent TestAgent {
      provider: "openai",
      model: "gpt-4o-mini",
      sources: [TestSrc]
    }
    return modeling_compare_models(TestAgent, "ModelA", "ModelB");
  )neam");
  ASSERT_TRUE(result.is_string);
}

TEST(V095ModelingAgent, NativeModelingProfileData)
{
  auto result = compile_and_run_safe(R"neam(
    schema_source TestSrc {
      type: "snowflake",
      connection: "test://conn"
    }
    modeling agent TestAgent {
      provider: "openai",
      model: "gpt-4o-mini",
      sources: [TestSrc]
    }
    return modeling_profile_data(TestAgent, "TestProfile");
  )neam");
  ASSERT_TRUE(result.is_string);
}

TEST(V095ModelingAgent, NativeModelingInferRelationships)
{
  auto result = compile_and_run_safe(R"neam(
    schema_source TestSrc {
      type: "snowflake",
      connection: "test://conn"
    }
    modeling agent TestAgent {
      provider: "openai",
      model: "gpt-4o-mini",
      sources: [TestSrc]
    }
    return modeling_discover_nature(TestAgent, "TestSrc");
  )neam");
  ASSERT_TRUE(result.is_string);
}

// ============================================================================
// Contextual keyword tests
// ============================================================================

TEST(V095ModelingAgent, ContextualKeywordEntity)
{
  // "entity" as identifier vs declaration
  assert_compiles(R"neam(
    let entity = "Customer";
    entity Product {
      domain: "Sales",
      attributes: { id: { type: "identifier", primary_key: true } }
    }
    return entity;
  )neam");
}

TEST(V095ModelingAgent, ContextualKeywordSchemaSource)
{
  assert_compiles(R"neam(
    let schema_source = "test";
    schema_source TestSrc {
      type: "postgres",
      connection: "test://conn"
    }
    return schema_source;
  )neam");
}

TEST(V095ModelingAgent, ContextualKeywordModeling)
{
  assert_compiles(R"neam(
    let modeling = "enabled";
    schema_source TestSrc {
      type: "postgres",
      connection: "test://conn"
    }
    modeling agent TestArchitect {
      provider: "openai",
      model: "gpt-4o-mini",
      sources: [TestSrc]
    }
    return modeling;
  )neam");
}

TEST(V095ModelingAgent, ContextualKeywordAmendment)
{
  assert_compiles(R"neam(
    let amendment = "pending";
    schema_source TestSrc {
      type: "snowflake",
      connection: "test://conn"
    }
    er_model TestModel {
      version: "1.0",
      levels: ["physical"],
      source: TestSrc
    }
    amendment SalesV4 {
      model: TestModel,
      type: "non_breaking",
      description: "Add column"
    }
    return amendment;
  )neam");
}

TEST(V095ModelingAgent, ContextualKeywordDatamart)
{
  assert_compiles(R"neam(
    let datamart = "finance";
    schema_source TestSrc {
      type: "snowflake",
      connection: "test://conn"
    }
    dimensional_model TestModel {
      methodology: "star",
      source: TestSrc,
      facts: { FCT: { grain: "test" } },
      dimensions: { DIM: { scd_type: 1 } }
    }
    datamart FinanceMart {
      dimensional_model: TestModel,
      purpose: "Test",
      facts: [FCT],
      dimensions: [DIM]
    }
    return datamart;
  )neam");
}

// ============================================================================
// Cross-version compatibility
// ============================================================================

TEST(V095ModelingAgent, CrossVersionWithGovernance)
{
  assert_compiles(R"neam(
    schema_source TestSrc {
      type: "snowflake",
      connection: "test://conn"
    }

    catalog_source GovSource {
      type: "snowflake",
      connection: "test://conn"
    }

    governance agent Steward {
      provider: "openai",
      model: "gpt-4o-mini",
      catalog: GovSource
    }

    modeling agent Architect {
      provider: "openai",
      model: "gpt-4o-mini",
      sources: [TestSrc],
      governance: Steward,
      enrich_from_governance: true
    }
    return nil;
  )neam");
}

TEST(V095ModelingAgent, CrossVersionWithETL)
{
  assert_compiles(R"neam(
    schema TestSchema {
      id: int,
      name: string
    }
    source TestSource {
      schema: TestSchema,
      type: "postgres",
      connection: "test://conn"
    }
    sink TestSink {
      schema: TestSchema,
      type: "postgres",
      connection: "test://conn"
    }
    etl agent Pipeline {
      provider: "openai",
      model: "gpt-4o-mini",
      sources: [TestSource],
      sinks: [TestSink],
      warehouse: ANALYTICS
    }

    schema_source ModelSrc {
      type: "snowflake",
      connection: "test://conn"
    }
    modeling agent Architect {
      provider: "openai",
      model: "gpt-4o-mini",
      sources: [ModelSrc],
      coordinates_with: [Pipeline]
    }
    return nil;
  )neam");
}

TEST(V095ModelingAgent, AllAgentTypesCoexist)
{
  // Test that data, ETL, governance, and modeling agents can coexist in one program
  assert_compiles(R"neam(
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
      sinks: [Snk]
    }
    etl agent EA {
      provider: "openai",
      model: "gpt-4o-mini",
      sources: [Src],
      sinks: [Snk],
      warehouse: W
    }
    catalog_source CS {
      type: "snowflake",
      connection: "c"
    }
    governance agent GA {
      provider: "openai",
      model: "gpt-4o-mini",
      catalog: CS
    }
    schema_source MS {
      type: "snowflake",
      connection: "c"
    }
    modeling agent ModelArch {
      provider: "openai",
      model: "gpt-4o-mini",
      sources: [MS]
    }
    return nil;
  )neam");
}

}  // namespace
