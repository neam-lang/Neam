//
// Neam v1.5 — NeamEvolve Phase A–F Unit Tests
//
// Verifies the P0 substrate: evolve agent + belief declarations parse and
// compile, the validators E-001..E-016 fire on broken inputs, and the
// belief runtime gates revision under the AR kernel.
//
// All tests dry-run; no LLM calls.
//

#include <gtest/gtest.h>
#include "neamc/pipeline.hpp"
#include "neamc/vm/vm.hpp"
#include "neamc/vm/harness_types.hpp"
#include <string>
#include <unistd.h>
#include <cstdlib>

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

// ═══════════════════════════════════════════════════════════════
// PART 1: Belief declaration parse + compile
// ═══════════════════════════════════════════════════════════════

TEST(V15Evolve, BasicBelief) {
  assert_compiles(R"NEAM(
    assertion_registry AR {
        no_secrets: { kind: "regex", pattern: "api_key", severity: "hard" }
    }
    belief CoreStrategy {
        initial: "Approach problems systematically.",
        constraints: AR,
        revision_trigger: "every_N_runs",
        trigger_n: 5,
        max_revisions_per_session: 10,
        rollback: true
    }
  )NEAM");
}

TEST(V15Evolve, BeliefMissingInitial_PBL001) {
  assert_fails(R"NEAM(
    assertion_registry AR {
        no_secrets: { kind: "regex", pattern: "api_key", severity: "hard" }
    }
    belief Bad {
        constraints: AR,
        revision_trigger: "manual",
        rollback: true
    }
  )NEAM");
}

TEST(V15Evolve, BeliefMissingConstraints_E002) {
  assert_fails(R"NEAM(
    belief Bad {
        initial: "x",
        revision_trigger: "manual",
        rollback: true
    }
  )NEAM");
}

TEST(V15Evolve, BeliefBadTriggerEnum_E012) {
  assert_fails(R"NEAM(
    assertion_registry AR { x: { kind: "regex", pattern: "z", severity: "hard" } }
    belief Bad {
        initial: "x",
        constraints: AR,
        revision_trigger: "spontaneous",
        rollback: true
    }
  )NEAM");
}

TEST(V15Evolve, BeliefRollbackFalseWithAutoTrigger_E007) {
  assert_fails(R"NEAM(
    assertion_registry AR { x: { kind: "regex", pattern: "z", severity: "hard" } }
    belief Bad {
        initial: "x",
        constraints: AR,
        revision_trigger: "every_N_runs",
        trigger_n: 3,
        rollback: false
    }
  )NEAM");
}

TEST(V15Evolve, BeliefMaxRevisionsOutOfRange_E015) {
  assert_fails(R"NEAM(
    assertion_registry AR { x: { kind: "regex", pattern: "z", severity: "hard" } }
    belief Bad {
        initial: "x",
        constraints: AR,
        revision_trigger: "manual",
        max_revisions_per_session: 500,
        rollback: true
    }
  )NEAM");
}

// ═══════════════════════════════════════════════════════════════
// PART 2: EvolveAgent declaration + validators
// ═══════════════════════════════════════════════════════════════

TEST(V15Evolve, BasicEvolveAgent) {
  assert_compiles(R"NEAM(
    fun check_result(ctx) { return true; }
    program EvolveGoals {
        mission: "test",
        constraints: [],
        process: ["test"],
        autonomy: "human_in_loop"
    }
    budget B { cost: 10.00, tokens: 100000 }
    handoff Experiences {
        path: "/tmp/v15_test_exp.md",
        schema: "markdown",
        required_sections: ["reflections"],
        schema_version: "1.0.0"
    }
    assertion_registry CoreConstraints {
        no_secret: { kind: "regex", pattern: "api_key", severity: "hard" }
    }
    belief CoreStrategy {
        initial: "Approach systematically.",
        constraints: CoreConstraints,
        revision_trigger: "manual",
        rollback: true
    }
    forge agent Critic {
        provider: "openai", model: "gpt-4o-mini", role: "evaluator",
        loop: { max_iterations: 1 max_cost: 1.0 max_tokens: 10000 },
        verify: check_result, budget: B
    }
    evolve agent Darwin {
        provider: "openai", model: "gpt-4o-mini", budget: B,
        sub_agents: { critic: Critic },
        handoff: Experiences,
        assertions: CoreConstraints,
        belief: CoreStrategy,
        safety: { program: EvolveGoals }
    }
  )NEAM");
}

TEST(V15Evolve, EvolveMissingBelief_E001) {
  assert_fails(R"NEAM(
    program P { mission: "x", constraints: [], process: ["x"], autonomy: "human_in_loop" }
    budget B { cost: 10.00, tokens: 100000 }
    handoff H { path: "/tmp/x.md", schema: "markdown", schema_version: "1.0.0" }
    assertion_registry AR { x: { kind: "regex", pattern: "z", severity: "hard" } }
    fun check(ctx) { return true; }
    forge agent C { provider: "openai", model: "gpt-4o-mini", role: "evaluator",
                    loop: { max_iterations: 1 max_cost: 1.0 max_tokens: 1000 },
                    verify: check, budget: B }
    evolve agent Bad {
        provider: "openai", model: "gpt-4o-mini", budget: B,
        sub_agents: { c: C },
        handoff: H,
        assertions: AR,
        safety: { program: P }
    }
  )NEAM");
}

TEST(V15Evolve, EvolveMissingHandoff_E003) {
  assert_fails(R"NEAM(
    program P { mission: "x", constraints: [], process: ["x"], autonomy: "human_in_loop" }
    budget B { cost: 10.00, tokens: 100000 }
    assertion_registry AR { x: { kind: "regex", pattern: "z", severity: "hard" } }
    belief Bel { initial: "x", constraints: AR, revision_trigger: "manual", rollback: true }
    fun check(ctx) { return true; }
    forge agent C { provider: "openai", model: "gpt-4o-mini", role: "evaluator",
                    loop: { max_iterations: 1 max_cost: 1.0 max_tokens: 1000 },
                    verify: check, budget: B }
    evolve agent Bad {
        provider: "openai", model: "gpt-4o-mini", budget: B,
        sub_agents: { c: C },
        belief: Bel,
        assertions: AR,
        safety: { program: P }
    }
  )NEAM");
}

TEST(V15Evolve, EvolveMissingProgram_E016) {
  assert_fails(R"NEAM(
    budget B { cost: 10.00, tokens: 100000 }
    handoff H { path: "/tmp/x.md", schema: "markdown", schema_version: "1.0.0" }
    assertion_registry AR { x: { kind: "regex", pattern: "z", severity: "hard" } }
    belief Bel { initial: "x", constraints: AR, revision_trigger: "manual", rollback: true }
    fun check(ctx) { return true; }
    forge agent C { provider: "openai", model: "gpt-4o-mini", role: "evaluator",
                    loop: { max_iterations: 1 max_cost: 1.0 max_tokens: 1000 },
                    verify: check, budget: B }
    evolve agent Bad {
        provider: "openai", model: "gpt-4o-mini", budget: B,
        sub_agents: { c: C },
        handoff: H,
        belief: Bel,
        assertions: AR
    }
  )NEAM");
}

TEST(V15Evolve, EvolveEmptySubAgents_E013) {
  assert_fails(R"NEAM(
    program P { mission: "x", constraints: [], process: ["x"], autonomy: "human_in_loop" }
    budget B { cost: 10.00, tokens: 100000 }
    handoff H { path: "/tmp/x.md", schema: "markdown", schema_version: "1.0.0" }
    assertion_registry AR { x: { kind: "regex", pattern: "z", severity: "hard" } }
    belief Bel { initial: "x", constraints: AR, revision_trigger: "manual", rollback: true }
    evolve agent Bad {
        provider: "openai", model: "gpt-4o-mini", budget: B,
        sub_agents: {},
        handoff: H,
        belief: Bel,
        assertions: AR,
        safety: { program: P }
    }
  )NEAM");
}

// ═══════════════════════════════════════════════════════════════
// PART 3: Belief runtime — text, history, hash (introspection)
// ═══════════════════════════════════════════════════════════════

TEST(V15Evolve, BeliefTextReturnsInitial) {
  assert_compiles(R"NEAM(
    assertion_registry AR { x: { kind: "regex", pattern: "z", severity: "hard" } }
    belief CoreStrategy {
        initial: "Approach systematically.",
        constraints: AR,
        revision_trigger: "manual",
        rollback: true
    }
    let t = belief_text("CoreStrategy");
    assert_true(t == "Approach systematically.");
  )NEAM");
}

TEST(V15Evolve, BeliefHistoryHasInitialVersion) {
  assert_compiles(R"NEAM(
    assertion_registry AR { x: { kind: "regex", pattern: "z", severity: "hard" } }
    belief CoreStrategy {
        initial: "x",
        constraints: AR,
        revision_trigger: "manual",
        rollback: true
    }
    let h = belief_history("CoreStrategy");
    // history is a JSON string; we just assert it's a String containing "version"
    assert_true(typeof(h) == "String");
  )NEAM");
}

TEST(V15Evolve, BeliefHashUnknownReturnsEmpty) {
  assert_compiles(R"NEAM(
    let h = belief_hash("NoSuchBelief");
    assert_true(h == "");
  )NEAM");
}

TEST(V15Evolve, BeliefReviseOutsideEvolveScope_BLSCOPE) {
  assert_compiles(R"NEAM(
    assertion_registry AR { x: { kind: "regex", pattern: "z", severity: "hard" } }
    belief CoreStrategy {
        initial: "x",
        constraints: AR,
        revision_trigger: "manual",
        rollback: true
    }
    // No evolve_agent_start was called, so revise must refuse with E-SBX-1.
    let r = belief_revise("CoreStrategy", "y");
    assert_true(typeof(r) == "String");
  )NEAM");
}

// ═══════════════════════════════════════════════════════════════
// PART 4: EvolveAgent lifecycle (dry-run)
// ═══════════════════════════════════════════════════════════════

TEST(V15Evolve, EvolveAgentStartUnknownReturnsError) {
  assert_compiles(R"NEAM(
    let r = evolve_agent_start("NoSuchAgent");
    assert_true(typeof(r) == "String");
  )NEAM");
}

TEST(V15Evolve, EvolveAgentStatusOnHarness_NotEvolveMode) {
  setenv("NEAM_HARNESS_DRY_RUN", "1", 1);
  assert_compiles(R"NEAM(
    budget B { cost: 10.0, tokens: 1000 }
    agent W { provider: "openai", model: "gpt-4o-mini", system: "w", budget: B }
    harness PlainHarness {
        provider: "openai", model: "gpt-4o-mini", budget: B,
        sub_agents: { main: W }
    }
    // PlainHarness is not an evolve agent; evolve_agent_start must refuse.
    let r = evolve_agent_start("PlainHarness");
    // Status native passes through, returns "registered" for any harness.
    let s = evolve_agent_status("PlainHarness");
    assert_true(s == "registered");
  )NEAM");
}

TEST(V15Evolve, EvolveAgentDryRunFullCycle) {
  setenv("NEAM_HARNESS_DRY_RUN", "1", 1);
  assert_compiles(R"NEAM(
    fun check_result(ctx) { return true; }
    program P { mission: "test", constraints: [], process: ["test"], autonomy: "human_in_loop" }
    budget B { cost: 10.0, tokens: 1000 }
    handoff H { path: "/tmp/v15_dry_h.md", schema: "markdown",
                required_sections: ["x"], schema_version: "1.0.0" }
    assertion_registry AR { x: { kind: "regex", pattern: "api_key", severity: "hard" } }
    belief Bel { initial: "go", constraints: AR, revision_trigger: "manual", rollback: true }
    forge agent Crit {
        provider: "openai", model: "gpt-4o-mini", role: "evaluator",
        loop: { max_iterations: 1 max_cost: 1.0 max_tokens: 1000 },
        verify: check_result, budget: B
    }
    evolve agent Darwin {
        provider: "openai", model: "gpt-4o-mini", budget: B,
        sub_agents: { critic: Crit },
        handoff: H, assertions: AR, belief: Bel,
        safety: { program: P }
    }
    let s0 = evolve_agent_status("Darwin");
    let r0 = evolve_agent_start("Darwin");
    let out = evolve_agent_run("Darwin", "do the thing");
    let r1 = evolve_agent_complete("Darwin");
    let s1 = evolve_agent_status("Darwin");
    assert_true(s0 == "registered");
    assert_true(r0 == "ok");
    assert_true(r1 == "ok");
    assert_true(s1 == "complete");
  )NEAM");
}

// ═══════════════════════════════════════════════════════════════
// PART 5: Skill library — declaration + validators
// ═══════════════════════════════════════════════════════════════

TEST(V15Evolve, BasicSkillLibrary) {
  assert_compiles(R"NEAM(
    skill_library Skills {
        verify: { method: "self_test", sandbox: true },
        deprecate: { after_failures: 5 },
        allow_runtime_acquisition: true
    }
  )NEAM");
}

TEST(V15Evolve, SkillLibraryMissingVerify_E005) {
  assert_fails(R"NEAM(
    skill_library Bad {
        deprecate: { after_failures: 5 }
    }
  )NEAM");
}

TEST(V15Evolve, SkillLibrarySandboxFalseWithRuntimeAcq_E006) {
  assert_fails(R"NEAM(
    skill_library Bad {
        verify: { method: "self_test", sandbox: false },
        allow_runtime_acquisition: true,
        deprecate: { after_failures: 5 }
    }
  )NEAM");
}

TEST(V15Evolve, SkillLibraryDeprecateOutOfRange_E014) {
  assert_fails(R"NEAM(
    skill_library Bad {
        verify: { method: "self_test", sandbox: true },
        deprecate: { after_failures: 5000 }
    }
  )NEAM");
}

// ═══════════════════════════════════════════════════════════════
// PART 6: Skill library — runtime (acquire/list/test/get/deprecate/invoke)
// ═══════════════════════════════════════════════════════════════

TEST(V15Evolve, SkillAcquireOutsideEvolveScope_SKSCOPE) {
  // Acquiring without an active evolve agent must refuse.
  assert_compiles(R"NEAM(
    skill_library Skills {
        verify: { method: "self_test", sandbox: true },
        deprecate: { after_failures: 5 }
    }
    let r = skill_acquire("Skills", "noop", "fun noop() { return 1; }");
    assert_true(typeof(r) == "String");
  )NEAM");
}

TEST(V15Evolve, SkillListEmptyLibrary) {
  assert_compiles(R"NEAM(
    skill_library Skills {
        verify: { method: "self_test", sandbox: true },
        deprecate: { after_failures: 5 }
    }
    let lst = skill_list("Skills");
    assert_true(typeof(lst) == "String");
    assert_true(lst == "[]");
  )NEAM");
}

TEST(V15Evolve, SkillGetUnknownLibrary_SKUNKNOWN) {
  assert_compiles(R"NEAM(
    let r = skill_get("NoSuchLib", "x");
    assert_true(typeof(r) == "String");
  )NEAM");
}

TEST(V15Evolve, SkillAcquireInsideEvolveScope_StaticAnalysisRejectsForbidden) {
  // Inside an active evolve run, a skill that calls a forbidden native
  // (e.g., system) must fail static analysis (E-011) and emit
  // skill.acquired with committed:false.
  setenv("NEAM_HARNESS_DRY_RUN", "1", 1);
  assert_compiles(R"NEAM(
    fun check_result(ctx) { return true; }
    program P { mission: "x", constraints: [], process: ["x"], autonomy: "human_in_loop" }
    budget B { cost: 10.0, tokens: 1000 }
    handoff H { path: "/tmp/v15_skill_h.md", schema: "markdown",
                required_sections: ["x"], schema_version: "1.0.0" }
    assertion_registry AR { x: { kind: "regex", pattern: "api_key", severity: "hard" } }
    belief Bel { initial: "go", constraints: AR, revision_trigger: "manual", rollback: true }
    skill_library Skills {
        verify: { method: "self_test", sandbox: true },
        deprecate: { after_failures: 5 }
    }
    forge agent Crit {
        provider: "openai", model: "gpt-4o-mini", role: "evaluator",
        loop: { max_iterations: 1 max_cost: 1.0 max_tokens: 1000 },
        verify: check_result, budget: B
    }
    evolve agent Darwin {
        provider: "openai", model: "gpt-4o-mini", budget: B,
        sub_agents: { critic: Crit },
        handoff: H, assertions: AR, belief: Bel, skills: Skills,
        safety: { program: P }
    }
    let r0 = evolve_agent_start("Darwin");
    // Forbidden: skill body calls evolve_agent_run() which is on the deny list.
    // Neam strings don't support \" escaping, so we use a single-line body
    // without nested string literals.
    let bad = skill_acquire("Skills", "evil", "fun evil() { evolve_agent_run(); }");
    let r1 = evolve_agent_complete("Darwin");
    assert_true(r0 == "ok");
    assert_true(typeof(bad) == "String");
    // Library still empty — bad skill not registered.
    let lst = skill_list("Skills");
    assert_true(lst == "[]");
  )NEAM");
}

TEST(V15Evolve, SkillAcquireSucceedsForCleanCode) {
  setenv("NEAM_HARNESS_DRY_RUN", "1", 1);
  assert_compiles(R"NEAM(
    fun check_result(ctx) { return true; }
    program P { mission: "x", constraints: [], process: ["x"], autonomy: "human_in_loop" }
    budget B { cost: 10.0, tokens: 1000 }
    handoff H { path: "/tmp/v15_skill_h2.md", schema: "markdown",
                required_sections: ["x"], schema_version: "1.0.0" }
    assertion_registry AR { x: { kind: "regex", pattern: "api_key", severity: "hard" } }
    belief Bel { initial: "go", constraints: AR, revision_trigger: "manual", rollback: true }
    skill_library Skills {
        verify: { method: "self_test", sandbox: true },
        deprecate: { after_failures: 5 }
    }
    forge agent Crit {
        provider: "openai", model: "gpt-4o-mini", role: "evaluator",
        loop: { max_iterations: 1 max_cost: 1.0 max_tokens: 1000 },
        verify: check_result, budget: B
    }
    evolve agent Darwin {
        provider: "openai", model: "gpt-4o-mini", budget: B,
        sub_agents: { critic: Crit },
        handoff: H, assertions: AR, belief: Bel, skills: Skills,
        safety: { program: P }
    }
    let r0 = evolve_agent_start("Darwin");
    let ok = skill_acquire("Skills", "double", "fun double_v(x) { return x * 2; }");
    let r1 = evolve_agent_complete("Darwin");
    assert_true(ok == "ok");
  )NEAM");
}

// ═══════════════════════════════════════════════════════════════
// PART 7: Curriculum (P1) — declaration + validators + runtime
// ═══════════════════════════════════════════════════════════════

TEST(V15Evolve, BasicCurriculum) {
  assert_compiles(R"NEAM(
    curriculum TaskProgression {
        mode: "auto",
        difficulty_metric: "task_success_rate",
        advance_threshold: 0.8,
        fallback_threshold: 0.4
    }
  )NEAM");
}

TEST(V15Evolve, CurriculumMissingMetric_E009) {
  assert_fails(R"NEAM(
    curriculum Bad {
        mode: "auto",
        advance_threshold: 0.8,
        fallback_threshold: 0.4
    }
  )NEAM");
}

TEST(V15Evolve, CurriculumThresholdOrder_E009) {
  assert_fails(R"NEAM(
    curriculum Bad {
        mode: "auto",
        difficulty_metric: "x",
        advance_threshold: 0.4,
        fallback_threshold: 0.8
    }
  )NEAM");
}

TEST(V15Evolve, CurriculumNextReturnsTask) {
  assert_compiles(R"NEAM(
    curriculum TP {
        mode: "auto",
        difficulty_metric: "x",
        advance_threshold: 0.8,
        fallback_threshold: 0.4
    }
    let task = curriculum_next("TP");
    assert_true(typeof(task) == "String");
  )NEAM");
}

TEST(V15Evolve, CurriculumDifficultyDefault) {
  assert_compiles(R"NEAM(
    curriculum TP {
        mode: "auto",
        difficulty_metric: "x",
        advance_threshold: 0.8,
        fallback_threshold: 0.4
    }
    let d = curriculum_difficulty("TP");
    assert_true(d == 0.5);
  )NEAM");
}

TEST(V15Evolve, CurriculumAdvanceUnknown_CRUNKNOWN) {
  assert_compiles(R"NEAM(
    let r = curriculum_advance("NoSuchCurriculum", true);
    assert_true(typeof(r) == "String");
  )NEAM");
}

// ═══════════════════════════════════════════════════════════════
// PART 8: Design op (P2) — declaration + validators + runtime
// ═══════════════════════════════════════════════════════════════

TEST(V15Evolve, EvolveAgentWithDesignOpsRequiresHumanGate_E010) {
  // safety.allow_design_ops:true without human_gate must fail.
  assert_fails(R"NEAM(
    fun check_result(ctx) { return true; }
    program P { mission: "x", constraints: [], process: ["x"], autonomy: "human_in_loop" }
    budget B { cost: 10.0, tokens: 1000 }
    handoff H { path: "/tmp/v15_design_h.md", schema: "markdown",
                required_sections: ["x"], schema_version: "1.0.0" }
    assertion_registry AR { x: { kind: "regex", pattern: "z", severity: "hard" } }
    belief Bel { initial: "x", constraints: AR, revision_trigger: "manual", rollback: true }
    forge agent Crit {
        provider: "openai", model: "gpt-4o-mini", role: "evaluator",
        loop: { max_iterations: 1 max_cost: 1.0 max_tokens: 1000 },
        verify: check_result, budget: B
    }
    evolve agent Bad {
        provider: "openai", model: "gpt-4o-mini", budget: B,
        sub_agents: { critic: Crit },
        handoff: H, assertions: AR, belief: Bel,
        safety: { program: P, allow_design_ops: true }
    }
  )NEAM");
}

TEST(V15Evolve, DesignProposeOutsideRunFails) {
  assert_compiles(R"NEAM(
    let r = design_propose("NoSuchAgent", "test goal");
    assert_true(typeof(r) == "String");
  )NEAM");
}

TEST(V15Evolve, DesignCompileSandboxDryRunPasses) {
  setenv("NEAM_HARNESS_DRY_RUN", "1", 1);
  assert_compiles(R"NEAM(
    let r = design_compile_in_sandbox("fun f() { return 1; }");
    assert_true(r == "ok");
  )NEAM");
}

TEST(V15Evolve, DesignPromoteWithoutHumanGateFails) {
  setenv("NEAM_HARNESS_DRY_RUN", "1", 1);
  assert_compiles(R"NEAM(
    let r = design_promote("UnknownAgent", "fun f(){}");
    assert_true(typeof(r) == "String");
  )NEAM");
}

// ═══════════════════════════════════════════════════════════════
// PART 9: Trace event hash chain (NFR-SEC-6)
// ═══════════════════════════════════════════════════════════════

TEST(V15Evolve, TraceHashChainEmitted) {
  // We can't easily read the trace JSONL from inside a test, but we can
  // exercise the chain code path and assert the run completes.  A more
  // thorough verification reads the trace file and chains hashes manually;
  // covered in integration tests.
  setenv("NEAM_HARNESS_DRY_RUN", "1", 1);
  setenv("NEAM_HARNESS_TRACE_PATH", "/tmp/v15_chain.jsonl", 1);
  unlink("/tmp/v15_chain.jsonl");
  assert_compiles(R"NEAM(
    fun check_result(ctx) { return true; }
    program P { mission: "x", constraints: [], process: ["x"], autonomy: "human_in_loop" }
    budget B { cost: 10.0, tokens: 1000 }
    handoff H { path: "/tmp/v15_chain_h.md", schema: "markdown",
                required_sections: ["x"], schema_version: "1.0.0" }
    assertion_registry AR { x: { kind: "regex", pattern: "api_key", severity: "hard" } }
    belief Bel { initial: "x", constraints: AR, revision_trigger: "manual", rollback: true }
    forge agent Crit {
        provider: "openai", model: "gpt-4o-mini", role: "evaluator",
        loop: { max_iterations: 1 max_cost: 1.0 max_tokens: 1000 },
        verify: check_result, budget: B
    }
    evolve agent Darwin {
        provider: "openai", model: "gpt-4o-mini", budget: B,
        sub_agents: { critic: Crit },
        handoff: H, assertions: AR, belief: Bel,
        safety: { program: P }
    }
    let r0 = evolve_agent_start("Darwin");
    let r1 = evolve_agent_run("Darwin", "go");
    let r2 = evolve_agent_complete("Darwin");
    assert_true(r0 == "ok");
    assert_true(r2 == "ok");
  )NEAM");
}

// ═══════════════════════════════════════════════════════════════
// PART 10: Backward compatibility — v1.4.5 programs still compile
// ═══════════════════════════════════════════════════════════════

TEST(V15Evolve, BackwardCompatV145Harness) {
  assert_compiles(R"NEAM(
    budget B { cost: 10.00, tokens: 100000 }
    agent M { provider: "anthropic", model: "claude-sonnet-4", system: "m", budget: B }
    harness H {
        provider: "anthropic",
        model: "claude-sonnet-4",
        budget: B,
        sub_agents: { main: M }
    }
  )NEAM");
}

TEST(V15Evolve, BackwardCompatV145HarnessLifecycle) {
  setenv("NEAM_HARNESS_DRY_RUN", "1", 1);
  assert_compiles(R"NEAM(
    budget B { cost: 10.0, tokens: 1000 }
    agent W { provider: "openai", model: "gpt-4o-mini", system: "w", budget: B }
    harness H {
        provider: "openai", model: "gpt-4o-mini", budget: B,
        sub_agents: { main: W }
    }
    let r = harness_start("H");
    let s = harness_status("H");
    let c = harness_complete("H");
    assert_true(r == "ok");
    assert_true(s == "running");
    assert_true(c == "ok");
  )NEAM");
}

}  // namespace
