//
// Neam v0.9.3 — DataOps Agent Unit Tests
//
// Tests for DataOps agent parsing, compiler validation (OPS-001 through OPS-010),
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
// Scheduler parsing
// ============================================================================

TEST(V093DataOpsAgent, SchedulerParsing)
{
  assert_compiles(R"neam(
    scheduler AirflowProd {
      type: "airflow",
      connection: "https://airflow.internal",
      credentials: "AIRFLOW_API_KEY",
      poll_interval: "30s",
      dag_filter: ["data_team_*", "ml_pipeline_*"],
      timezone: "UTC",
      datacenter: "us-east-1"
    }
    return nil;
  )neam");
}

// ============================================================================
// Audit table parsing
// ============================================================================

TEST(V093DataOpsAgent, AuditTableParsing)
{
  assert_compiles(R"neam(
    scheduler AirflowProd {
      type: "airflow",
      connection: "https://airflow.internal"
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
          failure: ["failed"],
          running: ["running"],
          skipped: ["skipped"]
        },
        rows_in: "input_count",
        rows_out: "output_count",
        error: "error_message",
        duration: "duration_seconds"
      },
      poll_interval: "60s",
      lookback_window: "24h",
      anomalies: {
        row_count_drop: 0.20,
        row_count_spike: 3.0,
        duration_spike: 2.0,
        failure_rate: 0.05,
        zero_rows_consecutive: 3
      }
    }
    return nil;
  )neam");
}

// ============================================================================
// Log source parsing
// ============================================================================

TEST(V093DataOpsAgent, LogSourceParsing)
{
  assert_compiles(R"neam(
    log_source SnowflakeLogs {
      type: "snowflake",
      connection: "snowflake://account.snowflakecomputing.com",
      credentials: "SF_CREDENTIALS",
      views: ["QUERY_HISTORY", "WAREHOUSE_METERING_HISTORY"],
      poll_interval: "60s",
      lookback_window: "1h",
      alerts: {
        query_timeout: true,
        warehouse_credit_spike: 2.0
      }
    }
    return nil;
  )neam");
}

// ============================================================================
// Platform monitor parsing
// ============================================================================

TEST(V093DataOpsAgent, PlatformParsing)
{
  assert_compiles(R"neam(
    platform SnowflakePlatform {
      type: "snowflake",
      connection: "snowflake://account.snowflakecomputing.com",
      credentials: "SF_CREDENTIALS",
      database: "ANALYTICS",
      health_checks: {
        storage_growth: true,
        warehouse_utilization: true,
        query_performance: {
          p95_threshold: "5m"
        },
        freshness: {
          "dim_customer": "24h",
          "fact_orders": "1h"
        }
      },
      finops: {
        daily_budget: 500.0,
        warehouse_budgets: {
          "TRANSFORM_WH": 200.0,
          "ANALYTICS_WH": 150.0
        },
        auto_suspend_idle: "5m",
        auto_kill_queries: "30m",
        cost_anomaly_threshold: 1.5
      }
    }
    return nil;
  )neam");
}

// ============================================================================
// Incident policy parsing
// ============================================================================

TEST(V093DataOpsAgent, IncidentPolicyParsing)
{
  assert_compiles(R"neam(
    incident_policy OpsPolicy {
      severity: {
        P1_CRITICAL: {
          conditions: ["SLA breach", "data loss"],
          response: "immediate",
          escalation: "oncall_pager",
          channels: ["#incidents", "#data-oncall"]
        },
        P4_LOW: {
          conditions: ["minor delay"],
          response: "backlog"
        }
      },
      auto_heal: {
        enabled: true,
        max_auto_retries: 3,
        retry_backoff: "exponential",
        allowed_actions: ["retry_job", "restart_warehouse", "clear_cache"],
        requires_approval: ["drop_table", "schema_change"],
        guardrails: {
          max_cost_per_action: 10.0,
          max_retries_per_hour: 5,
          no_actions_during: ["deploy_window"],
          require_dry_run: true
        }
      }
    }
    return nil;
  )neam");
}

// ============================================================================
// Correlation parsing
// ============================================================================

TEST(V093DataOpsAgent, CorrelationParsing)
{
  assert_compiles(R"neam(
    scheduler AirflowProd {
      type: "airflow",
      connection: "https://airflow.internal"
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
      }
    }
    log_source SnowflakeLogs {
      type: "snowflake",
      connection: "snowflake://account.snowflakecomputing.com"
    }
    correlation PipelineCorrelation {
      scope: {
        schedulers: [AirflowProd],
        audit_tables: [ETLAudit],
        log_sources: [SnowflakeLogs]
      },
      time_window: "30m",
      dependencies: {
        "fact_orders": ["dim_customer", "dim_product"],
        "agg_revenue": ["fact_orders"]
      },
      sla: {
        deadline: "06:00",
        timezone: "America/New_York",
        business_days_only: true,
        escalation: "oncall_pager"
      }
    }
    return nil;
  )neam");
}

// ============================================================================
// Full DataOps agent declaration
// ============================================================================

TEST(V093DataOpsAgent, FullDeclaration)
{
  assert_compiles(R"neam(
    budget OpsBudget { cost: 100.0 }
    scheduler AirflowProd {
      type: "airflow",
      connection: "https://airflow.internal"
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
      }
    }
    log_source SnowflakeLogs {
      type: "snowflake",
      connection: "snowflake://account.snowflakecomputing.com"
    }
    platform SnowflakePlatform {
      type: "snowflake",
      connection: "snowflake://account.snowflakecomputing.com",
      database: "ANALYTICS"
    }
    incident_policy OpsPolicy {
      severity: {
        P1_CRITICAL: {
          conditions: ["SLA breach"],
          response: "immediate"
        }
      },
      auto_heal: {
        enabled: true,
        max_auto_retries: 3,
        retry_backoff: "exponential",
        allowed_actions: ["retry_job"]
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
    return nil;
  )neam");
}

// ============================================================================
// VM registration and native functions
// ============================================================================

TEST(V093DataOpsAgent, VMRegistrationAndNatives)
{
  auto result = compile_and_run(R"neam(
    budget OpsBudget { cost: 100.0 }
    scheduler AirflowProd {
      type: "airflow",
      connection: "https://airflow.internal"
    }
    platform SnowflakePlatform {
      type: "snowflake",
      connection: "snowflake://account.snowflakecomputing.com",
      database: "ANALYTICS"
    }
    incident_policy OpsPolicy {
      severity: {
        P1_CRITICAL: {
          conditions: ["SLA breach"],
          response: "immediate"
        }
      },
      auto_heal: {
        enabled: true,
        max_auto_retries: 3,
        retry_backoff: "exponential",
        allowed_actions: ["retry_job"]
      }
    }
    dataops agent OpsAgent {
      provider: "openai",
      model: "gpt-4o-mini",
      budget: OpsBudget,
      platforms: [SnowflakePlatform],
      schedulers: [AirflowProd],
      incident_policy: OpsPolicy,
      mode: "continuous"
    }

    let status = dataops_status(OpsAgent);
    assert_eq(status, "idle");

    dataops_start_monitor(OpsAgent);
    let monitoring = dataops_is_monitoring(OpsAgent);
    assert_true(monitoring);

    let status2 = dataops_status(OpsAgent);
    assert_eq(status2, "monitoring");

    dataops_stop_monitor(OpsAgent);
    let status3 = dataops_status(OpsAgent);
    assert_eq(status3, "idle");

    let mode = dataops_mode(OpsAgent);
    assert_eq(mode, "continuous");

    return true;
  )neam");

  EXPECT_TRUE(result.is_bool());
  EXPECT_TRUE(result.as_bool());
}

// ============================================================================
// Incident lifecycle
// ============================================================================

TEST(V093DataOpsAgent, IncidentLifecycle)
{
  auto result = compile_and_run(R"neam(
    budget OpsBudget { cost: 100.0 }
    platform SnowflakePlatform {
      type: "snowflake",
      connection: "snowflake://account.snowflakecomputing.com",
      database: "ANALYTICS"
    }
    incident_policy OpsPolicy {
      severity: {
        P1_CRITICAL: {
          conditions: ["SLA breach"],
          response: "immediate"
        }
      },
      auto_heal: {
        enabled: false,
        max_auto_retries: 0,
        retry_backoff: "none",
        allowed_actions: []
      }
    }
    dataops agent OpsAgent {
      provider: "openai",
      model: "gpt-4o-mini",
      budget: OpsBudget,
      platforms: [SnowflakePlatform],
      incident_policy: OpsPolicy,
      mode: "on_demand"
    }

    let id = dataops_create_incident(OpsAgent, "P1", "SLA breach on fact_orders");
    let open = dataops_incidents_open(OpsAgent);
    assert_eq(open, 1);

    let today = dataops_incidents_today(OpsAgent);
    assert_eq(today, 1);

    dataops_triage(OpsAgent);
    let s1 = dataops_status(OpsAgent);
    assert_eq(s1, "triaging");

    dataops_investigate(OpsAgent);
    let s2 = dataops_status(OpsAgent);
    assert_eq(s2, "investigating");

    dataops_remediate(OpsAgent);
    let remediations = dataops_remediations_today(OpsAgent);
    assert_eq(remediations, 1);

    dataops_close_incident(OpsAgent, id);
    let open2 = dataops_incidents_open(OpsAgent);
    assert_eq(open2, 0);

    return true;
  )neam");

  EXPECT_TRUE(result.is_bool());
  EXPECT_TRUE(result.as_bool());
}

// ============================================================================
// Compiler validation errors
// ============================================================================

TEST(V093DataOpsAgent, OPS001_MustHavePlatform)
{
  assert_compile_error(R"neam(
    budget OpsBudget { cost: 100.0 }
    incident_policy OpsPolicy {
      severity: {},
      auto_heal: {
        enabled: false,
        max_auto_retries: 0,
        retry_backoff: "none",
        allowed_actions: []
      }
    }
    dataops agent Bad {
      provider: "openai",
      model: "gpt-4o-mini",
      budget: OpsBudget,
      incident_policy: OpsPolicy,
      mode: "on_demand"
    }
    return nil;
  )neam");
}

TEST(V093DataOpsAgent, OPS002_MustHaveIncidentPolicy)
{
  assert_compile_error(R"neam(
    budget OpsBudget { cost: 100.0 }
    platform SnowflakePlatform {
      type: "snowflake",
      connection: "snowflake://account.snowflakecomputing.com",
      database: "ANALYTICS"
    }
    dataops agent Bad {
      provider: "openai",
      model: "gpt-4o-mini",
      budget: OpsBudget,
      platforms: [SnowflakePlatform],
      mode: "on_demand"
    }
    return nil;
  )neam");
}

TEST(V093DataOpsAgent, OPS003_UndefinedPlatformRef)
{
  assert_compile_error(R"neam(
    budget OpsBudget { cost: 100.0 }
    incident_policy OpsPolicy {
      severity: {},
      auto_heal: {
        enabled: false,
        max_auto_retries: 0,
        retry_backoff: "none",
        allowed_actions: []
      }
    }
    dataops agent Bad {
      provider: "openai",
      model: "gpt-4o-mini",
      budget: OpsBudget,
      platforms: [NonExistentPlatform],
      incident_policy: OpsPolicy,
      mode: "on_demand"
    }
    return nil;
  )neam");
}

TEST(V093DataOpsAgent, OPS009_MustHaveBudget)
{
  assert_compile_error(R"neam(
    platform SnowflakePlatform {
      type: "snowflake",
      connection: "snowflake://account.snowflakecomputing.com",
      database: "ANALYTICS"
    }
    incident_policy OpsPolicy {
      severity: {},
      auto_heal: {
        enabled: false,
        max_auto_retries: 0,
        retry_backoff: "none",
        allowed_actions: []
      }
    }
    dataops agent Bad {
      provider: "openai",
      model: "gpt-4o-mini",
      platforms: [SnowflakePlatform],
      incident_policy: OpsPolicy,
      mode: "on_demand"
    }
    return nil;
  )neam");
}

// ============================================================================
// Contextual keyword — "dataops" usable as identifier
// ============================================================================

TEST(V093DataOpsAgent, ContextualKeyword)
{
  auto result = compile_and_run(R"neam(
    let dataops = "monitoring";
    return dataops;
  )neam");

  EXPECT_TRUE(result.is_string());
}

// ============================================================================
// Cross-version: DataOps + stateless agent coexistence
// ============================================================================

TEST(V093DataOpsAgent, CrossVersionCoexistence)
{
  assert_compiles(R"neam(
    budget OpsBudget { cost: 100.0 }

    scheduler AirflowProd {
      type: "airflow",
      connection: "https://airflow.internal"
    }
    platform SnowflakePlatform {
      type: "snowflake",
      connection: "snowflake://account.snowflakecomputing.com",
      database: "ANALYTICS"
    }
    incident_policy OpsPolicy {
      severity: {
        P1_CRITICAL: {
          conditions: ["SLA breach"],
          response: "immediate"
        }
      },
      auto_heal: {
        enabled: true,
        max_auto_retries: 3,
        retry_backoff: "exponential",
        allowed_actions: ["retry_job"]
      }
    }
    dataops agent OpsAgent {
      provider: "openai",
      model: "gpt-4o-mini",
      budget: OpsBudget,
      platforms: [SnowflakePlatform],
      schedulers: [AirflowProd],
      incident_policy: OpsPolicy,
      mode: "continuous"
    }

    agent SimpleAgent {
      provider: "openai",
      model: "gpt-4o-mini",
      system: "You are a helpful assistant."
    }

    return nil;
  )neam");
}

}  // namespace
