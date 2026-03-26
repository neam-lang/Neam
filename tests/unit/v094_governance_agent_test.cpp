//
// Neam v0.9.4 — Governance Agent Unit Tests
//
// Tests for Governance agent parsing, compiler validation (GOV-001 through GOV-020),
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
// CatalogSource parsing
// ============================================================================

TEST(V094GovernanceAgent, CatalogSourceParsing)
{
  assert_compiles(R"neam(
    catalog_source SnowflakeMain {
      type: "snowflake",
      connection: "snowflake://account.region",
      credentials: "SNOWFLAKE_CREDS",
      databases: ["ANALYTICS", "RAW_VAULT"],
      scan_interval: "6h",
      include_views: true,
      sync_mode: "pull",
      conflict_resolution: "external_wins"
    }
    return nil;
  )neam");
}

TEST(V094GovernanceAgent, CatalogSourceAllTypes)
{
  // Test various catalog source types
  for (const auto& t : {"snowflake", "oracle", "postgres", "mysql", "bigquery",
                         "s3", "collibra", "atlas", "purview"})
  {
    assert_compiles(std::string(R"neam(
      catalog_source TestSrc {
        type: ")neam") + t + R"neam(",
        connection: "test://conn"
      }
      return nil;
    )neam");
  }
}

// ============================================================================
// Classification policy parsing
// ============================================================================

TEST(V094GovernanceAgent, ClassificationPolicyParsing)
{
  assert_compiles(R"neam(
    classification_policy DataClassification {
      levels: {
        RESTRICTED: {
          level: 4,
          controls: ["encryption_at_rest", "column_masking"],
          retention_max: "7y",
          cross_border: "prohibited"
        },
        PUBLIC: {
          level: 1,
          controls: ["none"],
          retention_max: "indefinite",
          cross_border: "allowed"
        }
      },
      auto_classify: {
        enabled: true,
        provider: "openai",
        model: "gpt-4o-mini",
        semantic: {
          column_name_analysis: true,
          sample_value_analysis: true,
          confidence_threshold: 0.80
        },
        drift_detection: {
          enabled: true,
          scan_interval: "24h"
        }
      },
      propagation: {
        lineage_based: true,
        inheritance: "strictest"
      }
    }
    return nil;
  )neam");
}

// ============================================================================
// Access policy parsing
// ============================================================================

TEST(V094GovernanceAgent, AccessPolicyParsing)
{
  assert_compiles(R"neam(
    access_policy DataAccess {
      model: "rbac",
      roles: {
        data_engineer: {
          description: "Full access to engineering schemas",
          permissions: ["SELECT", "INSERT", "CREATE"],
          databases: ["ANALYTICS"]
        },
        analyst: {
          description: "Read-only access",
          permissions: ["SELECT"],
          databases: ["ANALYTICS"]
        }
      },
      access_review: {
        mode: "quarterly",
        unused_access_threshold: "90d",
        excessive_access_detection: true,
        auto_revoke_unused: false
      }
    }
    return nil;
  )neam");
}

// ============================================================================
// Quality policy parsing
// ============================================================================

TEST(V094GovernanceAgent, QualityPolicyParsing)
{
  assert_compiles(R"neam(
    quality_policy DataQuality {
      profiling: {
        enabled: true,
        scan_interval: "24h",
        sample_size: 10000
      },
      scoring: {
        enabled: true,
        accuracy_weight: 0.25,
        completeness_weight: 0.20,
        consistency_weight: 0.15,
        timeliness_weight: 0.15,
        validity_weight: 0.15,
        uniqueness_weight: 0.10,
        minimum_score: 0.80
      }
    }
    return nil;
  )neam");
}

// ============================================================================
// Lineage policy parsing
// ============================================================================

TEST(V094GovernanceAgent, LineagePolicyParsing)
{
  assert_compiles(R"neam(
    lineage_policy DataLineage {
      auto_discover: {
        enabled: true,
        sources: ["sql_logs", "etl_metadata"],
        methods: ["sql_parsing", "etl_metadata"],
        scan_interval: "6h",
        depth: "column"
      },
      impact_analysis: {
        enabled: true,
        downstream_depth: 10,
        include_reports: true,
        notification_on_breaking_change: true
      },
      tag_propagation: {
        enabled: true,
        direction: "downstream",
        inherit_sensitivity: "strictest",
        inherit_pii_tags: true
      }
    }
    return nil;
  )neam");
}

// ============================================================================
// Compliance policy parsing
// ============================================================================

TEST(V094GovernanceAgent, CompliancePolicyParsing)
{
  assert_compiles(R"neam(
    compliance_policy RegulatoryCompliance {
      regulations: ["GDPR", "CCPA", "HIPAA"],
      monitoring: {
        scan_interval: "24h",
        scoring: true,
        alert_on_non_compliance: true,
        report_channel: "#compliance"
      },
      dsar: {
        enabled: true,
        search_scope: ["all_databases"],
        response_format: "json"
      }
    }
    return nil;
  )neam");
}

// ============================================================================
// Lifecycle policy parsing
// ============================================================================

TEST(V094GovernanceAgent, LifecyclePolicyParsing)
{
  assert_compiles(R"neam(
    lifecycle_policy DataLifecycle {
      retention: {
        RESTRICTED: {
          max_retention: "7y",
          min_retention: "1y",
          action_on_expiry: "archive",
          archive_tier: "glacier"
        },
        PUBLIC: {
          max_retention: "indefinite",
          action_on_expiry: "none"
        }
      },
      tiering: {
        enabled: true,
        hot_to_warm: "30d",
        warm_to_cold: "90d",
        cold_to_archive: "365d"
      },
      legal_hold: {
        enabled: true,
        hold_overrides_retention: true,
        notification_channel: "#legal"
      }
    }
    return nil;
  )neam");
}

// ============================================================================
// Data product parsing
// ============================================================================

TEST(V094GovernanceAgent, DataProductParsing)
{
  assert_compiles(R"neam(
    data_product CustomerAnalytics {
      domain: "marketing",
      owner: "analytics-team",
      description: "Customer analytics data product",
      contract: {
        schema_version: "1.0.0",
        sla: {
          freshness: "1h",
          availability: "99.9",
          quality_score: 0.95
        },
        breaking_change_policy: "notify_and_deprecate",
        consumers: ["dashboard_team", "ml_team"]
      }
    }
    return nil;
  )neam");
}

// ============================================================================
// Contract policy parsing
// ============================================================================

TEST(V094GovernanceAgent, ContractPolicyParsing)
{
  assert_compiles(R"neam(
    contract_policy ContractEnforcement {
      schema_validation_on_deploy: true,
      breaking_change_detection: true,
      notify_consumers: true,
      freshness_check_interval: "5m",
      versioning_strategy: "semantic"
    }
    return nil;
  )neam");
}

// ============================================================================
// Master data parsing
// ============================================================================

TEST(V094GovernanceAgent, MasterDataParsing)
{
  assert_compiles(R"neam(
    master_data CustomerMDM {
      entity: "customer",
      golden_source: "CRM_SYSTEM",
      contributing_sources: ["ERP", "WEB_ANALYTICS"],
      matching: {
        strategy: "probabilistic",
        fields: ["email", "phone", "name"],
        confidence_threshold: 0.90,
        manual_review_threshold: 0.70
      },
      stewardship: {
        review_queue: "#data-stewards",
        auto_merge_above: 0.95,
        require_approval_below: 0.90
      }
    }
    return nil;
  )neam");
}

// ============================================================================
// External tool parsing
// ============================================================================

TEST(V094GovernanceAgent, ExternalToolParsing)
{
  assert_compiles(R"neam(
    external_tool CollibraCloud {
      type: "collibra",
      connection: "https://company.collibra.com/api",
      credentials: "COLLIBRA_API_KEY",
      sync_interval: "1h",
      conflict_resolution: "external_wins"
    }
    return nil;
  )neam");
}

// ============================================================================
// Governance agent — full declaration
// ============================================================================

TEST(V094GovernanceAgent, GovernanceAgentFullDeclaration)
{
  assert_compiles(R"neam(
    budget GovBudget { cost: 5.00, tokens: 100000 }

    catalog_source SnowflakeMain {
      type: "snowflake",
      connection: "snowflake://account",
      scan_interval: "6h"
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
      system: "You are a data governance agent.",
      temperature: 0.3,
      budget: GovBudget,
      classification: DataClassification
    }
    return nil;
  )neam");
}

// ============================================================================
// Governance agent — field rejection
// ============================================================================

TEST(V094GovernanceAgent, GovernanceAgentRejectsETLFields)
{
  assert_compile_error(R"neam(
    budget B { cost: 1.00 }
    classification_policy C {
      levels: {
        A: { level: 1, controls: ["x"] },
        B: { level: 2, controls: ["y"] }
      }
    }
    governance agent Bad {
      budget: B,
      classification: C,
      warehouse: SomeWarehouse
    }
    return nil;
  )neam");
}

TEST(V094GovernanceAgent, GovernanceAgentRejectsMigrationFields)
{
  assert_compile_error(R"neam(
    budget B { cost: 1.00 }
    classification_policy C {
      levels: {
        A: { level: 1, controls: ["x"] },
        B: { level: 2, controls: ["y"] }
      }
    }
    governance agent Bad {
      budget: B,
      classification: C,
      waves: SomeWaves
    }
    return nil;
  )neam");
}

TEST(V094GovernanceAgent, GovernanceAgentRejectsDataOpsFields)
{
  assert_compile_error(R"neam(
    budget B { cost: 1.00 }
    classification_policy C {
      levels: {
        A: { level: 1, controls: ["x"] },
        B: { level: 2, controls: ["y"] }
      }
    }
    governance agent Bad {
      budget: B,
      classification: C,
      schedulers: [SomeSched]
    }
    return nil;
  )neam");
}

// ============================================================================
// Glossary parsing
// ============================================================================

TEST(V094GovernanceAgent, GlossaryParsing)
{
  assert_compiles(R"neam(
    glossary BusinessTerms {
      domains: ["finance", "marketing", "operations"],
      auto_suggest: {
        enabled: true,
        provider: "openai",
        model: "gpt-4o-mini",
        require_approval: true
      },
      synonym_detection: {
        enabled: true,
        confidence_threshold: 0.85,
        cross_database: true
      }
    }
    return nil;
  )neam");
}

// ============================================================================
// Cross-version compatibility: governance + dataops in same program
// ============================================================================

TEST(V094GovernanceAgent, CrossVersionCompatibility)
{
  assert_compiles(R"neam(
    budget OpsBudget { cost: 2.00, tokens: 50000 }
    budget GovBudget { cost: 5.00, tokens: 100000 }

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
    return nil;
  )neam");
}

}  // namespace
