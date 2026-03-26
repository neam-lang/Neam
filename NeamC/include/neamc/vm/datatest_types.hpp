#pragma once
#include "neamc/vm/object.hpp"
#include <string>
#include <mutex>
#include <nlohmann/json.hpp>

namespace neamc::vm {

struct ObjTestStrategy : Obj { std::string name; std::string source_spec_ref; std::string source_nfr_ref; std::string levels_json; std::string test_data_json; std::string gates_json; std::string reporting_json; };
struct ObjTestCaseGenerator : Obj { std::string name; std::string source_ref; std::string generation_json; std::string output_json; };
struct ObjTestCaseDef : Obj { std::string name; std::string requirement_ref; std::string acceptance_criteria; std::string test_type; std::string priority; std::string preconditions_json; std::string steps_json; std::string expected_result; std::string automation_json; };
struct ObjETLTestSuite : Obj { std::string name; std::string pipeline_ref; std::string connection_ref; std::string tests_json; mutable std::mutex state_mutex; };
struct ObjDWTestSuite : Obj { std::string name; std::string connection_ref; std::string tests_json; };
struct ObjMLTestSuite : Obj { std::string name; std::string model_ref; std::string tests_json; };
struct ObjAPITestSuite : Obj { std::string name; std::string endpoint; std::string auth_json; std::string tests_json; };
struct ObjPerformanceTestSuite : Obj { std::string name; std::string pipeline_performance_json; std::string query_performance_json; std::string api_performance_json; };
struct ObjEdgeCaseTests : Obj { std::string name; std::string generation_json; std::string tests_json; };
struct ObjSITSuite : Obj { std::string name; std::string scope; std::string tests_json; };
struct ObjUATSuite : Obj { std::string name; std::string business_validation_json; std::string data_quality_uat_json; };
struct ObjRegressionSuite : Obj { std::string name; std::string baseline_json; std::string checks_json; std::string trigger; };
struct ObjQualityGateTest : Obj { std::string name; std::string data_quality_gate_json; std::string model_quality_gate_json; std::string api_quality_gate_json; std::string performance_gate_json; std::string uat_gate_json; std::string deployment_decision_json; };
struct ObjTestReportConfig : Obj { std::string name; std::string execution_json; std::string report_json; std::string notify_json; };
struct ObjDefectManagement : Obj { std::string name; std::string on_failure_json; std::string rca_json; std::string tracking_json; };

struct ObjDataTestAgent : Obj {
  std::string name;
  std::string provider;
  std::string llm_model;
  double temperature = 0.2;
  std::string agent_md_path;
  std::string sub_agents_json;
  std::string forge_ref;
  std::string test_strategy_ref;
  std::string test_generator_ref;
  std::string etl_tests_ref;
  std::string dw_tests_ref;
  std::string ml_tests_ref;
  std::string api_tests_ref;
  std::string performance_tests_ref;
  std::string edge_tests_ref;
  std::string sit_suite_ref;
  std::string uat_suite_ref;
  std::string regression_suite_ref;
  std::string quality_gate_ref;
  std::string report_config_ref;
  std::string defect_mgmt_ref;
  std::string coordinates_with;
  std::string handoffs;
  std::string role;
  std::string purpose;
  std::string autonomy;
  std::string budget_ref;
  std::string status = "initialized";
  int tests_executed = 0;
  int tests_passed = 0;
  int tests_failed = 0;
  mutable std::mutex state_mutex;
};

ObjTestStrategy* new_test_strategy();
ObjTestCaseGenerator* new_test_case_generator();
ObjTestCaseDef* new_test_case_def();
ObjETLTestSuite* new_etl_test_suite();
ObjDWTestSuite* new_dw_test_suite();
ObjMLTestSuite* new_ml_test_suite();
ObjAPITestSuite* new_api_test_suite();
ObjPerformanceTestSuite* new_performance_test_suite();
ObjEdgeCaseTests* new_edge_case_tests();
ObjSITSuite* new_sit_suite();
ObjUATSuite* new_uat_suite();
ObjRegressionSuite* new_regression_suite();
ObjQualityGateTest* new_quality_gate_test();
ObjTestReportConfig* new_test_report_config();
ObjDefectManagement* new_defect_management();
ObjDataTestAgent* new_datatest_agent();

}  // namespace neamc::vm
