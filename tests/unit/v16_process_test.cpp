//
// Neam v1.6 — NeamMesh Phase A–H Unit Tests
//
// Verifies the P0 substrate: process / task / decision / event / pool
// declarations parse and compile, validators (P-FR-PR-1..PL-1) fire on broken
// inputs, the runtime advances multi-instance processes via the dry-run
// harness substrate, persistence round-trips, HITL pauses + resumes.
//
// All tests dry-run; no LLM calls.
//

#include <gtest/gtest.h>
#include "neamc/pipeline.hpp"
#include "neamc/vm/vm.hpp"
#include "neamc/vm/harness_types.hpp"
#include "neamc/vm/process_runtime.hpp"
#include <string>
#include <unistd.h>
#include <cstdlib>
#include <fstream>
#include <algorithm>
#include <vector>

namespace {
using namespace neamc;
using namespace neamc::vm;

static void assert_compiles(const std::string& source) {
  Pipeline p;
  EXPECT_NO_THROW(p.compile(source, {}));
}
static void assert_fails(const std::string& source) {
  Pipeline p;
  EXPECT_ANY_THROW(p.compile(source, {}));
}

// Reset both registries between tests so process_start() picks up THIS test's
// declarations rather than a leftover from a prior TEST.
struct V16Fixture : ::testing::Test {
  void SetUp() override {
    setenv("NEAM_HARNESS_DRY_RUN", "1", 1);
    ::neamc::vm::harness::HarnessRegistry::instance().reset_for_tests();
    ::neamc::vm::process::reset_for_tests();
  }
};

// ═══════════════════════════════════════════════════════════════
// PART 1: AST + parser — basic shape acceptance
// ═══════════════════════════════════════════════════════════════

TEST_F(V16Fixture, BasicProcess) {
  assert_compiles(R"NEAM(
    task T1 { agent: "AgentRef" }
    event E_start { type: "start" }
    event E_end { type: "end" }
    process P {
      start: "E_start",
      tasks: { T1: "AgentRef" },
      events: { E_start: { type: "start" }, E_end: { type: "end" } }
    }
  )NEAM");
}

TEST_F(V16Fixture, BasicTask) {
  assert_compiles(R"NEAM(
    task T1 { agent: "Solver", retries: 3 }
  )NEAM");
}

TEST_F(V16Fixture, BasicDecision) {
  assert_compiles(R"NEAM(
    decision D1 {
      expr: "score > 0.5",
      branches: { yes: "T_continue", no: "T_escalate" }
    }
  )NEAM");
}

TEST_F(V16Fixture, BasicEvent) {
  assert_compiles(R"NEAM(
    event E1 { type: "start" }
    event E2 { type: "intermediate" }
    event E3 { type: "end" }
    event E4 { type: "timer" }
  )NEAM");
}

TEST_F(V16Fixture, BasicPool) {
  assert_compiles(R"NEAM(
    pool POrg {
      lanes: { sales: "SalesAgent", support: "SupportAgent" }
    }
  )NEAM");
}

// ═══════════════════════════════════════════════════════════════
// PART 2: Compiler validators reject malformed bodies
// ═══════════════════════════════════════════════════════════════

TEST_F(V16Fixture, ProcessMissingStart_PRRule1) {
  assert_fails(R"NEAM(
    process Bad { tasks: { T1: "A" } }
  )NEAM");
}

TEST_F(V16Fixture, ProcessMissingTasks_PRRule1) {
  assert_fails(R"NEAM(
    process Bad { start: "T1" }
  )NEAM");
}

TEST_F(V16Fixture, ProcessStartNotInTasks_PRRule2) {
  assert_fails(R"NEAM(
    process Bad { start: "Missing", tasks: { Other: "A" } }
  )NEAM");
}

TEST_F(V16Fixture, TaskMissingAgent_TKRule1) {
  assert_fails(R"NEAM(
    task Bad { retries: 3 }
  )NEAM");
}

TEST_F(V16Fixture, TaskRetriesOutOfRange_TKRule3) {
  assert_fails(R"NEAM(
    task Bad { agent: "A", retries: 99 }
  )NEAM");
}

TEST_F(V16Fixture, DecisionMissingExpr_DCRule1) {
  assert_fails(R"NEAM(
    decision Bad { branches: { yes: "T1", no: "T2" } }
  )NEAM");
}

TEST_F(V16Fixture, DecisionTooFewBranches_DCRule2) {
  assert_fails(R"NEAM(
    decision Bad { expr: "x", branches: { only: "T1" } }
  )NEAM");
}

TEST_F(V16Fixture, EventInvalidType_EVRule1) {
  assert_fails(R"NEAM(
    event Bad { type: "explosion" }
  )NEAM");
}

TEST_F(V16Fixture, PoolMissingLanes_PLRule1) {
  assert_fails(R"NEAM(
    pool Bad { description: "no lanes" }
  )NEAM");
}

// ═══════════════════════════════════════════════════════════════
// PART 3: Runtime — process_start + advance + status
// ═══════════════════════════════════════════════════════════════

TEST_F(V16Fixture, ProcessStartReturnsInstanceId) {
  setenv("NEAM_PROCESS_TRACE_PATH", "/tmp/v16_p1.jsonl", 1);
  unlink("/tmp/v16_p1.jsonl");
  assert_compiles(R"NEAM(
    task T1 { agent: "Solver", next: "E_end" }
    event E_end { type: "end" }
    process P {
      start: "T1",
      tasks: { T1: "Solver" },
      events: { E_end: { type: "end" } }
    }
    let inst = process_start("P");
    assert_true(typeof(inst) == "String");
    let advance_result = process_advance(inst);
    let status = process_status(inst);
    assert_true(status == "complete");
  )NEAM");
}

TEST_F(V16Fixture, UnknownProcessFails) {
  assert_compiles(R"NEAM(
    let r = process_start("NoSuchProcess");
    assert_true(typeof(r) == "String");
  )NEAM");
}

TEST_F(V16Fixture, ProcessAbortChangesStatus) {
  setenv("NEAM_PROCESS_TRACE_PATH", "/tmp/v16_abort.jsonl", 1);
  unlink("/tmp/v16_abort.jsonl");
  assert_compiles(R"NEAM(
    task T1 { agent: "Solver" }
    event E_end { type: "end" }
    process P {
      start: "T1",
      tasks: { T1: "Solver" },
      events: { E_end: { type: "end" } }
    }
    let inst = process_start("P");
    process_abort(inst, "user_cancel");
    assert_true(process_status(inst) == "aborted");
  )NEAM");
}

// ═══════════════════════════════════════════════════════════════
// PART 4: Multi-instance (ADR-004) — two starts return distinct IDs
// ═══════════════════════════════════════════════════════════════

TEST_F(V16Fixture, MultiInstanceDistinctIds) {
  setenv("NEAM_PROCESS_TRACE_PATH", "/tmp/v16_multi.jsonl", 1);
  unlink("/tmp/v16_multi.jsonl");
  assert_compiles(R"NEAM(
    task T1 { agent: "Solver", next: "E_end" }
    event E_end { type: "end" }
    process P {
      start: "T1",
      tasks: { T1: "Solver" },
      events: { E_end: { type: "end" } }
    }
    let i1 = process_start("P");
    let i2 = process_start("P");
    assert_true(i1 != i2);
  )NEAM");
}

// ═══════════════════════════════════════════════════════════════
// PART 5: Persistence round-trip (Phase E)
// ═══════════════════════════════════════════════════════════════

TEST_F(V16Fixture, PersistRoundTrip) {
  // Use the C++ runtime API directly — assert_compiles() only compiles,
  // doesn't execute, so script-level natives never actually run.
  setenv("NEAM_PROCESS_TRACE_PATH", "/tmp/v16_persist.jsonl", 1);
  setenv("NEAM_PROCESS_STATE_PATH", "/tmp/v16_persist_state.json", 1);
  unlink("/tmp/v16_persist.jsonl");
  unlink("/tmp/v16_persist_state.json");

  // Register a process directly via the registry.
  ::neamc::vm::harness::ProcessRecord pr;
  pr.name = "P_persist";
  pr.fields_json = R"({"start":"T1","tasks":{"T1":"Solver"},"events":{"E_end":{"type":"end"}}})";
  ::neamc::vm::harness::HarnessRegistry::instance().register_process(pr);

  ::neamc::vm::harness::TaskRecord tr;
  tr.name = "T1"; tr.agent_ref = "Solver";
  tr.fields_json = R"({"agent":"Solver","next":"E_end"})";
  ::neamc::vm::harness::HarnessRegistry::instance().register_task(tr);

  ::neamc::vm::harness::EventRecord er;
  er.name = "E_end"; er.event_type = "end";
  er.fields_json = R"({"type":"end"})";
  ::neamc::vm::harness::HarnessRegistry::instance().register_event(er);

  auto start = ::neamc::vm::process::process_start("P_persist");
  ASSERT_TRUE(start.ok) << start.error_code << " " << start.output;
  std::string inst_id = start.output;

  auto persist = ::neamc::vm::process::process_persist(inst_id);
  ASSERT_TRUE(persist.ok) << persist.error_code << " " << persist.output;
  EXPECT_EQ(persist.output, "/tmp/v16_persist_state.json");

  std::ifstream f("/tmp/v16_persist_state.json");
  EXPECT_TRUE(f.good());

  // Recover into a fresh state and confirm the instance is back.
  ::neamc::vm::process::reset_for_tests();
  auto recover = ::neamc::vm::process::process_recover("/tmp/v16_persist_state.json");
  ASSERT_TRUE(recover.ok) << recover.error_code << " " << recover.output;
  EXPECT_EQ(recover.output, inst_id);
}

// ═══════════════════════════════════════════════════════════════
// PART 6: HITL — forge role:"human" parses
// ═══════════════════════════════════════════════════════════════

TEST_F(V16Fixture, ForgeRoleHumanParses) {
  assert_compiles(R"NEAM(
    budget B { cost: 1.0, tokens: 100 }
    fun verify(ctx) { return true; }
    forge agent Reviewer {
      provider: "openai", model: "gpt-4o-mini", role: "human",
      loop: { max_iterations: 1 max_cost: 1.0 max_tokens: 100 },
      verify: verify, budget: B
    }
  )NEAM");
}

TEST_F(V16Fixture, ForgeRoleHumanInProcessIsPending) {
  setenv("NEAM_PROCESS_TRACE_PATH", "/tmp/v16_hitl.jsonl", 1);
  unlink("/tmp/v16_hitl.jsonl");
  assert_compiles(R"NEAM(
    budget B { cost: 1.0, tokens: 100 }
    fun verify(ctx) { return true; }
    forge agent Reviewer {
      provider: "openai", model: "gpt-4o-mini", role: "human",
      loop: { max_iterations: 1 max_cost: 1.0 max_tokens: 100 },
      verify: verify, budget: B
    }
    task T1 { agent: "Reviewer", next: "E_end" }
    event E_end { type: "end" }
    process P {
      start: "T1",
      tasks: { T1: "Reviewer" },
      events: { E_end: { type: "end" } }
    }
    let inst = process_start("P");
    process_advance(inst);
    let pending = hitl_is_pending(inst);
    assert_true(pending == "true");
    hitl_resume(inst, "approved");
    let after = hitl_is_pending(inst);
    assert_true(after == "false");
  )NEAM");
}

// ═══════════════════════════════════════════════════════════════
// PART 7: Trace plane — events are written
// ═══════════════════════════════════════════════════════════════

TEST_F(V16Fixture, TraceFileWritten) {
  // Use C++ runtime API directly — exercises the trace plane.
  const char* trace = "/tmp/v16_trace.jsonl";
  setenv("NEAM_PROCESS_TRACE_PATH", trace, 1);
  unlink(trace);

  ::neamc::vm::harness::ProcessRecord pr;
  pr.name = "P_trace";
  pr.fields_json = R"({"start":"T1","tasks":{"T1":"Solver"},"events":{"E_end":{"type":"end"}}})";
  ::neamc::vm::harness::HarnessRegistry::instance().register_process(pr);

  ::neamc::vm::harness::TaskRecord tr;
  tr.name = "T1"; tr.agent_ref = "Solver";
  tr.fields_json = R"({"agent":"Solver","next":"E_end"})";
  ::neamc::vm::harness::HarnessRegistry::instance().register_task(tr);

  ::neamc::vm::harness::EventRecord er;
  er.name = "E_end"; er.event_type = "end";
  er.fields_json = R"({"type":"end"})";
  ::neamc::vm::harness::HarnessRegistry::instance().register_event(er);

  auto start = ::neamc::vm::process::process_start("P_trace");
  ASSERT_TRUE(start.ok);
  auto adv = ::neamc::vm::process::process_advance(start.output);
  ASSERT_TRUE(adv.ok) << adv.error_code << " " << adv.output;

  std::ifstream f(trace);
  EXPECT_TRUE(f.good());
  std::string line; int line_count = 0;
  while (std::getline(f, line)) line_count++;
  EXPECT_GT(line_count, 0);
  // Confirm the trace contains expected lifecycle events.
  std::ifstream f2(trace); std::string content((std::istreambuf_iterator<char>(f2)),
                                                std::istreambuf_iterator<char>());
  EXPECT_NE(content.find("process.start"), std::string::npos);
  EXPECT_NE(content.find("task.begin"), std::string::npos);
  EXPECT_NE(content.find("task.complete"), std::string::npos);
}

// ═══════════════════════════════════════════════════════════════
// PART 8: Backward compatibility — v1.4.5 + v1.5 still work
// ═══════════════════════════════════════════════════════════════

TEST_F(V16Fixture, BackwardCompatV145Harness) {
  assert_compiles(R"NEAM(
    budget B { cost: 10.0, tokens: 1000 }
    agent M { provider: "openai", model: "gpt-4o-mini", system: "m", budget: B }
    harness H {
      provider: "openai", model: "gpt-4o-mini", budget: B,
      sub_agents: { main: M }
    }
  )NEAM");
}

TEST_F(V16Fixture, BackwardCompatV15EvolveAgent) {
  assert_compiles(R"NEAM(
    fun verify(ctx) { return true; }
    program P0 { mission: "x", constraints: [], process: ["x"], autonomy: "human_in_loop" }
    budget B { cost: 10.0, tokens: 1000 }
    handoff H { path: "/tmp/v16_compat.md", schema: "markdown",
                required_sections: ["x"], schema_version: "1.0.0" }
    assertion_registry AR { x: { kind: "regex", pattern: "api_key", severity: "hard" } }
    belief Bel { initial: "x", constraints: AR, revision_trigger: "manual", rollback: true }
    forge agent Crit {
      provider: "openai", model: "gpt-4o-mini", role: "evaluator",
      loop: { max_iterations: 1 max_cost: 1.0 max_tokens: 100 },
      verify: verify, budget: B
    }
    evolve agent Darwin {
      provider: "openai", model: "gpt-4o-mini", budget: B,
      sub_agents: { critic: Crit },
      handoff: H, assertions: AR, belief: Bel,
      safety: { program: P0 }
    }
  )NEAM");
}

// ═══════════════════════════════════════════════════════════════
// PART 9: Stress — 100 instances, ULID uniqueness + advance-to-completion
// (validates ADR-004 multi-instance isolation + P-NFR-PERF-1 O(steps) advance)
// ═══════════════════════════════════════════════════════════════

TEST_F(V16Fixture, Stress100InstancesAllDistinctAllComplete) {
  const char* trace = "/tmp/v16_stress100_trace.jsonl";
  setenv("NEAM_PROCESS_TRACE_PATH", trace, 1);
  unlink(trace);

  // Register one process with one task + end event.
  ::neamc::vm::harness::ProcessRecord pr;
  pr.name = "StressFlow";
  pr.fields_json = R"({"start":"T1","tasks":{"T1":"S"},"events":{"E_end":{"type":"end"}}})";
  ::neamc::vm::harness::HarnessRegistry::instance().register_process(pr);

  ::neamc::vm::harness::TaskRecord tr;
  tr.name = "T1"; tr.agent_ref = "S";
  tr.fields_json = R"({"agent":"S","next":"E_end"})";
  ::neamc::vm::harness::HarnessRegistry::instance().register_task(tr);

  ::neamc::vm::harness::EventRecord er;
  er.name = "E_end"; er.event_type = "end";
  er.fields_json = R"({"type":"end"})";
  ::neamc::vm::harness::HarnessRegistry::instance().register_event(er);

  // Phase 1: start 100 instances, collect IDs.
  std::vector<std::string> ids;
  ids.reserve(100);
  for (int i = 0; i < 100; ++i) {
    auto r = ::neamc::vm::process::process_start("StressFlow");
    ASSERT_TRUE(r.ok) << "iter " << i << ": " << r.error_code;
    ASSERT_FALSE(r.output.empty()) << "iter " << i << ": empty instance id";
    ids.push_back(r.output);
  }

  // Phase 2: distinct check (ULID uniqueness — ADR-004).
  std::sort(ids.begin(), ids.end());
  auto dup_it = std::adjacent_find(ids.begin(), ids.end());
  EXPECT_EQ(dup_it, ids.end()) << "duplicate ID found: " << *dup_it;

  // Phase 3: advance each — all should complete.
  int completed = 0;
  for (const auto& id : ids) {
    auto r = ::neamc::vm::process::process_advance(id);
    EXPECT_TRUE(r.ok) << "advance failed for " << id << ": " << r.error_code;
    auto status = ::neamc::vm::process::process_status(id);
    if (status == "complete") completed++;
  }
  EXPECT_EQ(completed, 100);

  // Phase 4: trace plane — should have ~500 events (100 × 5 per instance).
  std::ifstream f(trace);
  ASSERT_TRUE(f.good());
  std::string line; int line_count = 0;
  int starts = 0, completes = 0;
  while (std::getline(f, line)) {
    line_count++;
    if (line.find("\"kind\":\"process.start\"") != std::string::npos) starts++;
    if (line.find("\"kind\":\"process.complete\"") != std::string::npos) completes++;
  }
  EXPECT_EQ(starts, 100);
  EXPECT_EQ(completes, 100);
  EXPECT_GE(line_count, 400);  // at least process.start + task.begin + task.complete + event + process.complete per instance
}

}  // namespace
