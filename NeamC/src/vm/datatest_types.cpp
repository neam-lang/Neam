#include "neamc/vm/datatest_types.hpp"
namespace neamc::vm {
ObjTestStrategy* new_test_strategy() { auto* o = new ObjTestStrategy(); o->type = ObjType::OBJ_TEST_STRATEGY; o->marked = false; o->next = nullptr; return o; }
ObjTestCaseGenerator* new_test_case_generator() { auto* o = new ObjTestCaseGenerator(); o->type = ObjType::OBJ_TEST_CASE_GENERATOR; o->marked = false; o->next = nullptr; return o; }
ObjTestCaseDef* new_test_case_def() { auto* o = new ObjTestCaseDef(); o->type = ObjType::OBJ_TEST_CASE_DEF; o->marked = false; o->next = nullptr; return o; }
ObjETLTestSuite* new_etl_test_suite() { auto* o = new ObjETLTestSuite(); o->type = ObjType::OBJ_ETL_TEST_SUITE; o->marked = false; o->next = nullptr; return o; }
ObjDWTestSuite* new_dw_test_suite() { auto* o = new ObjDWTestSuite(); o->type = ObjType::OBJ_DW_TEST_SUITE; o->marked = false; o->next = nullptr; return o; }
ObjMLTestSuite* new_ml_test_suite() { auto* o = new ObjMLTestSuite(); o->type = ObjType::OBJ_ML_TEST_SUITE; o->marked = false; o->next = nullptr; return o; }
ObjAPITestSuite* new_api_test_suite() { auto* o = new ObjAPITestSuite(); o->type = ObjType::OBJ_API_TEST_SUITE; o->marked = false; o->next = nullptr; return o; }
ObjPerformanceTestSuite* new_performance_test_suite() { auto* o = new ObjPerformanceTestSuite(); o->type = ObjType::OBJ_PERFORMANCE_TEST_SUITE; o->marked = false; o->next = nullptr; return o; }
ObjEdgeCaseTests* new_edge_case_tests() { auto* o = new ObjEdgeCaseTests(); o->type = ObjType::OBJ_EDGE_CASE_TESTS; o->marked = false; o->next = nullptr; return o; }
ObjSITSuite* new_sit_suite() { auto* o = new ObjSITSuite(); o->type = ObjType::OBJ_SIT_SUITE; o->marked = false; o->next = nullptr; return o; }
ObjUATSuite* new_uat_suite() { auto* o = new ObjUATSuite(); o->type = ObjType::OBJ_UAT_SUITE; o->marked = false; o->next = nullptr; return o; }
ObjRegressionSuite* new_regression_suite() { auto* o = new ObjRegressionSuite(); o->type = ObjType::OBJ_REGRESSION_SUITE; o->marked = false; o->next = nullptr; return o; }
ObjQualityGateTest* new_quality_gate_test() { auto* o = new ObjQualityGateTest(); o->type = ObjType::OBJ_QUALITY_GATE_TEST; o->marked = false; o->next = nullptr; return o; }
ObjTestReportConfig* new_test_report_config() { auto* o = new ObjTestReportConfig(); o->type = ObjType::OBJ_TEST_REPORT_CONFIG; o->marked = false; o->next = nullptr; return o; }
ObjDefectManagement* new_defect_management() { auto* o = new ObjDefectManagement(); o->type = ObjType::OBJ_DEFECT_MANAGEMENT; o->marked = false; o->next = nullptr; return o; }
ObjDataTestAgent* new_datatest_agent() { auto* o = new ObjDataTestAgent(); o->type = ObjType::OBJ_DATATEST_AGENT; o->marked = false; o->next = nullptr; o->status = "initialized"; return o; }
}  // namespace neamc::vm
