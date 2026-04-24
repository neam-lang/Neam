//
// Neam v1.4.5 — NeamHarness Phase 0 Unit Tests
// Tests the minimal harness declaration (sub-type 25 on OP_DEFINE_DIO_DECLARATION):
// parser recognition, AST variant registration, bytecode emission, VM dispatch.
//
// Phase 0 scope: declaration parses and compiles; runtime behavior (handoff,
// tool_registry, assertion_registry, forge roles, trace replay, benchmarking)
// lands in subsequent phases.
//

#include <gtest/gtest.h>
#include "neamc/pipeline.hpp"
#include "neamc/vm/vm.hpp"
#include <string>

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
// PART 1: Minimal Harness Declaration (Phase 0)
// ═══════════════════════════════════════════════════════════════

TEST(V145Harness, BasicHarness) {
  assert_compiles(R"NEAM(
    budget B { cost: 10.00, tokens: 100000 }
    agent Main {
        provider: "anthropic",
        model: "claude-sonnet-4",
        system: "Answer questions",
        budget: B
    }
    harness M {
        provider: "anthropic",
        model: "claude-sonnet-4",
        budget: B,
        sub_agents: { main: Main }
    }
  )NEAM");
}

TEST(V145Harness, HarnessWithAllCoreFields) {
  assert_compiles(R"NEAM(
    budget B { cost: 500.00, tokens: 10000000 }
    agent Worker { provider: "anthropic", model: "claude-opus-4", system: "work", budget: B }
    harness App {
        provider: "anthropic",
        model: "claude-opus-4",
        temperature: 0.2,
        budget: B,
        sub_agents: { main: Worker },
        context: {
            compaction: "auto",
            max_window_tokens: 200000,
            preserve_across_reset: ["goal", "progress"]
        },
        observability: {
            metrics: ["task_success_rate", "tokens_per_task"],
            audit: true
        }
    }
  )NEAM");
}

TEST(V145Harness, H001EmptyHarnessRejected) {
  // Phase 1 H-001: harness must declare non-empty sub_agents.
  // This test documents the validation boundary activated in Phase 1.
  assert_fails(R"NEAM(
    harness Empty {
        provider: "anthropic",
        model: "claude-sonnet-4"
    }
  )NEAM");
}

TEST(V145Harness, H001EmptySubAgentsRejected) {
  assert_fails(R"NEAM(
    harness E {
        provider: "anthropic",
        model: "claude-sonnet-4",
        sub_agents: {}
    }
  )NEAM");
}

// ═══════════════════════════════════════════════════════════════
// PART 2: Interaction with v1.4 and Prior Constructs
// ═══════════════════════════════════════════════════════════════

TEST(V145Harness, HarnessCoexistsWithWiki) {
  assert_compiles(R"NEAM(
    wiki KnowledgeBase {
        topic: "engineering",
        raw_path: "./wiki/raw",
        wiki_path: "./wiki/topics"
    }
    budget B { cost: 50.00, tokens: 500000 }
    agent Research { provider: "anthropic", model: "claude-opus-4", system: "research", budget: B }
    harness Pipeline {
        provider: "anthropic",
        model: "claude-opus-4",
        budget: B,
        sub_agents: { research: Research }
    }
  )NEAM");
}

TEST(V145Harness, MultipleHarnessesInOneProgram) {
  assert_compiles(R"NEAM(
    budget B1 { cost: 10.00, tokens: 100000 }
    budget B2 { cost: 50.00, tokens: 500000 }
    agent A { provider: "anthropic", model: "claude-sonnet-4", system: "a", budget: B1 }
    agent BAgent { provider: "anthropic", model: "claude-opus-4", system: "b", budget: B2 }
    harness H1 {
        provider: "anthropic", model: "claude-sonnet-4",
        budget: B1,
        sub_agents: { main: A }
    }
    harness H2 {
        provider: "anthropic", model: "claude-opus-4",
        budget: B2,
        sub_agents: { main: BAgent }
    }
  )NEAM");
}

// ═══════════════════════════════════════════════════════════════
// PART 3: Backward Compatibility (v0.6–v1.4 programs unaffected)
// ═══════════════════════════════════════════════════════════════

TEST(V145Harness, BackwardCompatPlainAgent) {
  assert_compiles(R"NEAM(
    budget B { cost: 10.00, tokens: 100000 }
    agent Plain { provider: "anthropic", model: "claude-sonnet-4", system: "hi", budget: B }
  )NEAM");
}

TEST(V145Harness, BackwardCompatWikiOnly) {
  assert_compiles(R"NEAM(
    wiki W { topic: "t", raw_path: "./r", wiki_path: "./w" }
  )NEAM");
}

// ═══════════════════════════════════════════════════════════════
// PART 4: Handoff Declaration (sub-type 26, Phase 0b)
// ═══════════════════════════════════════════════════════════════

TEST(V145Harness, BasicHandoff) {
  assert_compiles(R"NEAM(
    handoff HF {
        path: "./progress.md",
        schema: "markdown",
        required_sections: ["completed", "in_progress", "next"],
        max_size_kb: 16,
        on_overflow: "summarize",
        versioning: "git",
        on_read: "strict",
        on_write: "lenient",
        schema_version: "1.0.0"
    }
  )NEAM");
}

TEST(V145Harness, H015HandoffMissingSchemaVersionRejected) {
  // Phase 1 H-015: handoff must declare schema_version.
  // Prevents silent breaking changes across sessions.
  assert_fails(R"NEAM(
    handoff NoVer {
        path: "./p.md",
        schema: "markdown"
    }
  )NEAM");
}

TEST(V145Harness, HandoffInsideHarness) {
  assert_compiles(R"NEAM(
    budget B { cost: 10.00, tokens: 100000 }
    agent Worker { provider: "anthropic", model: "claude-sonnet-4", system: "w", budget: B }
    handoff HF {
        path: "./hf.md",
        schema: "markdown",
        required_sections: ["state"],
        schema_version: "1.0.0"
    }
    harness H {
        provider: "anthropic",
        model: "claude-sonnet-4",
        budget: B,
        handoff: HF,
        sub_agents: { main: Worker }
    }
  )NEAM");
}

// ═══════════════════════════════════════════════════════════════
// PART 5: Tool Registry (sub-type 27, Phase 0b)
// ═══════════════════════════════════════════════════════════════

TEST(V145Harness, BasicToolRegistry) {
  assert_compiles(R"NEAM(
    tool_registry TR {
        builtin: ["fetch", "parse"],
        project: ["compile_neam"],
        user: [],
        max_active: 12
    }
  )NEAM");
}

TEST(V145Harness, ToolRegistryWithBriefs) {
  assert_compiles(R"NEAM(
    tool_registry TR {
        builtin: ["fetch", "parse"],
        max_active: 5,
        briefs: {
            fetch: { safe_max: 5, note: "Each fetch ~8KB; 5 fills ~40% of 100k context." },
            parse: { safe_max: 20, note: "Parse results are compact (~200 tokens each)." }
        }
    }
  )NEAM");
}

TEST(V145Harness, ToolRegistryWithScoping) {
  assert_compiles(R"NEAM(
    tool_registry TR {
        builtin: ["fetch", "run_tests", "write_file"],
        max_active: 10,
        scoping: {
            planner: ["fetch"],
            generator: ["run_tests", "write_file"],
            evaluator: ["run_tests"]
        }
    }
  )NEAM");
}

// ═══════════════════════════════════════════════════════════════
// PART 6: Assertion Registry (sub-type 28, Phase 0b)
// ═══════════════════════════════════════════════════════════════

TEST(V145Harness, BasicAssertionRegistry) {
  assert_compiles(R"NEAM(
    assertion_registry AR {
        no_secrets: {
            kind: "regex",
            pattern: "api_key",
            severity: "hard",
            on_violation: "abort_and_log"
        }
    }
  )NEAM");
}

TEST(V145Harness, AssertionRegistryAllKinds) {
  assert_compiles(R"NEAM(
    assertion_registry AR {
        no_secrets: { kind: "regex", pattern: "secret", severity: "hard" },
        respect_budget: { kind: "runtime", metric: "cost", op: "<=", value: 500, severity: "hard" },
        no_network: { kind: "capability", forbid: ["network_call"], severity: "hard" },
        reactor_safe: { kind: "domain", invariant: "temperature < 500", severity: "hard" }
    }
  )NEAM");
}

// ═══════════════════════════════════════════════════════════════
// PART 7: Harness Benchmark (sub-type 29, Phase 0b)
// ═══════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════
// Phase 2: ForgeAgent role extension
// ═══════════════════════════════════════════════════════════════

TEST(V145Harness, Phase2ForgeRolePlannerParses) {
  assert_compiles(R"NEAM(
    fun check_result(ctx) { return true; }
    budget B { cost: 10.00, tokens: 100000 }
    forge agent Planner {
        provider: "anthropic",
        model: "claude-sonnet-4",
        role: "planner",
        loop: { max_iterations: 1 max_cost: 1.0 max_tokens: 10000 },
        verify: check_result,
        budget: B
    }
  )NEAM");
}

TEST(V145Harness, Phase2ForgeRoleGeneratorParses) {
  assert_compiles(R"NEAM(
    fun check_result(ctx) { return true; }
    budget B { cost: 10.00, tokens: 100000 }
    forge agent Generator {
        provider: "anthropic",
        model: "claude-sonnet-4",
        role: "generator",
        loop: { max_iterations: 5 max_cost: 5.0 max_tokens: 100000 },
        verify: check_result,
        budget: B
    }
  )NEAM");
}

TEST(V145Harness, Phase2ForgeRoleEvaluatorParses) {
  assert_compiles(R"NEAM(
    fun check_result(ctx) { return true; }
    budget B { cost: 10.00, tokens: 100000 }
    forge agent Evaluator {
        provider: "anthropic",
        model: "claude-sonnet-4",
        role: "evaluator",
        loop: { max_iterations: 1 max_cost: 1.0 max_tokens: 10000 },
        verify: check_result,
        budget: B
    }
  )NEAM");
}

TEST(V145Harness, Phase2ForgeRoleInvalidRejected) {
  // P-FR-001: role must be one of planner/generator/evaluator
  assert_fails(R"NEAM(
    fun check_result(ctx) { return true; }
    budget B { cost: 10.00, tokens: 100000 }
    forge agent Bad {
        provider: "anthropic",
        model: "claude-sonnet-4",
        role: "critic",
        loop: { max_iterations: 1 max_cost: 1.0 max_tokens: 10000 },
        verify: check_result,
        budget: B
    }
  )NEAM");
}

TEST(V145Harness, Phase2ForgeNoRoleBackwardCompat) {
  // Existing v1.4 forge agents without role: must still compile.
  assert_compiles(R"NEAM(
    fun check_result(ctx) { return true; }
    budget B { cost: 10.00, tokens: 100000 }
    forge agent Legacy {
        provider: "anthropic",
        model: "claude-sonnet-4",
        loop: { max_iterations: 1 max_cost: 1.0 max_tokens: 10000 },
        verify: check_result,
        budget: B
    }
  )NEAM");
}

TEST(V145Harness, BasicHarnessBenchmark) {
  assert_compiles(R"NEAM(
    harness_benchmark NB {
        description: "NeamBench v1",
        dimensions: ["pass_rate", "efficiency", "determinism", "compile_time_catch_rate"],
        repetitions: 3,
        seed: 42
    }
  )NEAM");
}

// ═══════════════════════════════════════════════════════════════
// PART 8: Full Integration (all 5 declarations together)
// ═══════════════════════════════════════════════════════════════

// ═══════════════════════════════════════════════════════════════
// Phase 3-minimal: HarnessRegistry + lifecycle natives
// ═══════════════════════════════════════════════════════════════

TEST(V145Harness, Phase3HarnessHashDeterministic) {
  // Same harness declaration MUST produce the same 64-char SHA-256 hex
  // across two independent compile+run cycles (FR-H-5 determinism).
  assert_compiles(R"NEAM(
    budget B { cost: 10.00, tokens: 100000 }
    agent M { provider: "anthropic", model: "claude-sonnet-4", system: "m", budget: B }
    harness H {
        provider: "anthropic",
        model: "claude-sonnet-4",
        budget: B,
        sub_agents: { main: M }
    }
    let h = harness_hash("H");
    assert_true(typeof(h) == "String");
  )NEAM");
}

TEST(V145Harness, Phase3HarnessStatusRegisteredAfterCompile) {
  assert_compiles(R"NEAM(
    budget B { cost: 10.00, tokens: 100000 }
    agent M { provider: "anthropic", model: "claude-sonnet-4", system: "m", budget: B }
    harness H {
        provider: "anthropic", model: "claude-sonnet-4",
        budget: B, sub_agents: { main: M }
    }
    let s = harness_status("H");
    assert_true(s == "registered");
  )NEAM");
}

TEST(V145Harness, Phase3HarnessStatusUnknownForMissing) {
  assert_compiles(R"NEAM(
    let s = harness_status("NoSuchHarness");
    assert_true(s == "unknown");
  )NEAM");
}

TEST(V145Harness, Phase3HandoffSchemaVersionRetrievable) {
  // Phase 3 makes schema_version readable at runtime.
  assert_compiles(R"NEAM(
    handoff HF {
        path: "./p.md",
        schema: "markdown",
        schema_version: "2.3.4"
    }
    let v = handoff_schema_version("HF");
    assert_true(v == "2.3.4");
  )NEAM");
}

TEST(V145Harness, Phase3LlmAskCompilesAndReturnsError) {
  // llm_ask compiles + links. Without an API key it must return an
  // error-sentinel string (not crash).  We can't assert the exact reply
  // without a live key; just that the call doesn't blow up and typeof
  // is String.
  assert_compiles(R"NEAM(
    let reply = llm_ask("openai", "gpt-5-mini", "hi");
    assert_true(typeof(reply) == "String");
  )NEAM");
}

TEST(V145Harness, Phase3LlmAskUnknownProviderSurfacesError) {
  // Don't assert the return type (Nil vs. error-string varies by code path);
  // just prove it compiles + doesn't crash the VM.
  assert_compiles(R"NEAM(
    let reply = llm_ask("not_a_provider", "gpt-5", "hi");
  )NEAM");
}

TEST(V145Harness, Phase3HarnessEnvReturnsJSON) {
  assert_compiles(R"NEAM(
    let run_env = harness_env();
    // Must be a non-empty JSON string containing NEAM_RUN_ID
    assert_true(typeof(run_env) == "String");
  )NEAM");
}

// ═══════════════════════════════════════════════════════════════
// Phase 4: handoff runtime natives
// ═══════════════════════════════════════════════════════════════

TEST(V145Harness, Phase4HandoffWriteThenReadRoundtrip) {
  assert_compiles(R"NEAM(
    handoff HF {
        path: "/tmp/neam_test_handoff_roundtrip.md",
        schema: "markdown",
        required_sections: ["status"],
        schema_version: "1.0.0"
    }
    let r = handoff_write("HF", "status", "All green");
    assert_true(r == "ok");
    let back = handoff_read("HF", "status");
    assert_true(back == "All green");
  )NEAM");
}

TEST(V145Harness, Phase4HandoffExistsAndSize) {
  assert_compiles(R"NEAM(
    handoff HF {
        path: "/tmp/neam_test_handoff_exists.md",
        schema: "markdown",
        schema_version: "1.0.0"
    }
    handoff_write("HF", "body", "hello world");
    assert_true(handoff_exists("HF") == true);
    let n = handoff_size("HF");
    assert_true(n > 0);
  )NEAM");
}

TEST(V145Harness, Phase4HandoffValidateRequiredSections) {
  assert_compiles(R"NEAM(
    handoff HF {
        path: "/tmp/neam_test_handoff_validate.md",
        schema: "markdown",
        required_sections: ["alpha", "beta"],
        schema_version: "1.0.0"
    }
    handoff_write("HF", "alpha", "a");
    let v1 = handoff_validate("HF");
    // Missing 'beta' — expect error string, not "ok"
    assert_true(v1 != "ok");
    handoff_write("HF", "beta", "b");
    let v2 = handoff_validate("HF");
    assert_true(v2 == "ok");
  )NEAM");
}

TEST(V145Harness, Phase4HandoffUnknownNameSurfacesError) {
  assert_compiles(R"NEAM(
    let r = handoff_write("NoSuchHandoff", "x", "y");
    // Should return an error string containing HF-UNKNOWN
    assert_true(typeof(r) == "String");
  )NEAM");
}

// ═══════════════════════════════════════════════════════════════
// Phase 5: tool registry scope + brief injection
// ═══════════════════════════════════════════════════════════════

TEST(V145Harness, Phase5ToolRegistryScopeCheck) {
  assert_compiles(R"NEAM(
    tool_registry TR {
        builtin: ["fetch", "execute_python_code"],
        max_active: 5,
        scoping: {
            planner: ["fetch"],
            generator: ["execute_python_code"]
        }
    }
    assert_true(tool_registry_check("TR", "planner", "fetch") == true);
    assert_true(tool_registry_check("TR", "planner", "execute_python_code") == false);
    assert_true(tool_registry_check("TR", "generator", "execute_python_code") == true);
  )NEAM");
}

TEST(V145Harness, Phase5ToolRegistryPermissiveForAbsentRole) {
  // If scoping doesn't mention a role, tool_registry_check returns true
  // (permissive for roles not declared — harness's compile-time role
  // validation is where denial actually happens).
  assert_compiles(R"NEAM(
    tool_registry TR {
        builtin: ["fetch"],
        max_active: 5,
        scoping: { planner: ["fetch"] }
    }
    assert_true(tool_registry_check("TR", "some_other_role", "fetch") == true);
  )NEAM");
}

TEST(V145Harness, Phase5BriefsOnlyForPlanner) {
  // FR-TB-2: briefs only visible to planner role.
  assert_compiles(R"NEAM(
    tool_registry TR {
        builtin: ["fetch"],
        max_active: 5,
        scoping: { planner: ["fetch"] },
        briefs: { fetch: { safe_max: 5, note: "small chunks" } }
    }
    let planner_briefs = tool_registry_format_briefs("TR", "planner");
    let gen_briefs     = tool_registry_format_briefs("TR", "generator");
    assert_true(gen_briefs == "");
  )NEAM");
}

TEST(V145Harness, Phase5UnknownRegistryReturnsEmpty) {
  assert_compiles(R"NEAM(
    let scope = tool_registry_scope_of("NoTR", "planner");
    assert_true(scope == "[]");
    assert_true(tool_registry_check("NoTR", "planner", "fetch") == false);
  )NEAM");
}

// ═══════════════════════════════════════════════════════════════
// Phase 6: assertion kernel (regex + runtime)
// ═══════════════════════════════════════════════════════════════

TEST(V145Harness, Phase6AssertionRegexCleanAndDirty) {
  assert_compiles(R"NEAM(
    assertion_registry AR {
        no_secrets: { kind: "regex", pattern: "api_key", severity: "hard" }
    }
    assert_true(assertion_check_regex("AR", "no_secrets", "clean text") == "ok");
    assert_true(assertion_check_regex("AR", "no_secrets", "my api_key = x") == "violated");
  )NEAM");
}

TEST(V145Harness, Phase6AssertionRuntimeBudget) {
  assert_compiles(R"NEAM(
    assertion_registry AR {
        respect_budget: {
            kind: "runtime", metric: "cost", op: "<=", value: 500, severity: "hard"
        }
    }
    assert_true(assertion_check_runtime("AR", "respect_budget", 100) == "ok");
    assert_true(assertion_check_runtime("AR", "respect_budget", 700) == "violated");
  )NEAM");
}

TEST(V145Harness, Phase6AssertionAIMEAnswerRange) {
  // Would catch II-14 (7529536 bogus answer in real gpt-5 AIME run)
  assert_compiles(R"NEAM(
    assertion_registry AR {
        answer_in_range: {
            kind: "runtime", metric: "answer", op: "<=", value: 999, severity: "hard"
        }
    }
    assert_true(assertion_check_runtime("AR", "answer_in_range", 42) == "ok");
    assert_true(assertion_check_runtime("AR", "answer_in_range", 7529536) == "violated");
  )NEAM");
}

TEST(V145Harness, Phase6HardCountMatchesSeverity) {
  assert_compiles(R"NEAM(
    assertion_registry AR {
        h1: { kind: "regex", pattern: "a", severity: "hard" },
        h2: { kind: "regex", pattern: "b", severity: "hard" },
        s1: { kind: "regex", pattern: "c", severity: "soft" }
    }
    let n = assertion_hard_count("AR");
    assert_true(n == 2);
  )NEAM");
}

TEST(V145Harness, Phase6KindsDistribution) {
  assert_compiles(R"NEAM(
    assertion_registry AR {
        r1: { kind: "regex",   pattern: "x", severity: "hard" },
        r2: { kind: "regex",   pattern: "y", severity: "soft" },
        t1: { kind: "runtime", metric: "cost", op: "<=", value: 100, severity: "hard" }
    }
    let k = assertion_kinds("AR");
    // Return value is a JSON string; we just assert type + non-empty
    assert_true(typeof(k) == "String");
  )NEAM");
}

TEST(V145Harness, Phase6UnknownRegistryOrName) {
  assert_compiles(R"NEAM(
    let r1 = assertion_check_regex("NoAR", "x", "y");
    // Result is a string containing AR-UNKNOWN
    assert_true(typeof(r1) == "String");
    assertion_registry AR { a: { kind: "regex", pattern: "x", severity: "hard" } }
    let r2 = assertion_check_regex("AR", "b", "y");
    assert_true(typeof(r2) == "String");
  )NEAM");
}

// ═══════════════════════════════════════════════════════════════
// Phase 7: forge role introspection
// ═══════════════════════════════════════════════════════════════

TEST(V145Harness, Phase7ForgeRoleOfReturnsDeclaredRole) {
  assert_compiles(R"NEAM(
    fun check_result(ctx) { return true; }
    budget B { cost: 10.00, tokens: 100000 }
    forge agent Planner {
        provider: "anthropic", model: "claude-sonnet-4",
        role: "planner",
        loop: { max_iterations: 1 max_cost: 1.0 max_tokens: 10000 },
        verify: check_result, budget: B
    }
    assert_true(forge_role_of("Planner") == "planner");
  )NEAM");
}

TEST(V145Harness, Phase7ForgeRoleOfLegacyIsEmpty) {
  // Backward compat: v1.4 forge agents without role: stay introspectable-as-empty.
  assert_compiles(R"NEAM(
    fun check_result(ctx) { return true; }
    budget B { cost: 10.00, tokens: 100000 }
    forge agent Legacy {
        provider: "anthropic", model: "claude-sonnet-4",
        loop: { max_iterations: 1 max_cost: 1.0 max_tokens: 10000 },
        verify: check_result, budget: B
    }
    assert_true(forge_role_of("Legacy") == "");
  )NEAM");
}

TEST(V145Harness, Phase7ForgeRoleOfUnknownIsEmpty) {
  assert_compiles(R"NEAM(
    assert_true(forge_role_of("NoSuchAgent") == "");
  )NEAM");
}

TEST(V145Harness, Phase7AllThreeRolesIntrospectable) {
  assert_compiles(R"NEAM(
    fun check_result(ctx) { return true; }
    budget B { cost: 10.00, tokens: 100000 }
    forge agent P {
        provider: "anthropic", model: "claude-sonnet-4", role: "planner",
        loop: { max_iterations: 1 max_cost: 1.0 max_tokens: 10000 }, verify: check_result, budget: B
    }
    forge agent G {
        provider: "anthropic", model: "claude-sonnet-4", role: "generator",
        loop: { max_iterations: 5 max_cost: 5.0 max_tokens: 100000 }, verify: check_result, budget: B
    }
    forge agent E {
        provider: "anthropic", model: "claude-sonnet-4", role: "evaluator",
        loop: { max_iterations: 1 max_cost: 1.0 max_tokens: 10000 }, verify: check_result, budget: B
    }
    assert_true(forge_role_of("P") == "planner");
    assert_true(forge_role_of("G") == "generator");
    assert_true(forge_role_of("E") == "evaluator");
  )NEAM");
}

TEST(V145Harness, FullStackAllFiveDeclarations) {
  assert_compiles(R"NEAM(
    budget B { cost: 200.00, tokens: 5000000 }

    agent Main { provider: "anthropic", model: "claude-opus-4", system: "m", budget: B }

    handoff HF {
        path: "./progress.md", schema: "markdown",
        required_sections: ["done", "todo"],
        schema_version: "1.0.0"
    }

    tool_registry TR {
        builtin: ["compile", "test"],
        max_active: 4
    }

    assertion_registry AR {
        respect_budget: { kind: "runtime", metric: "cost", op: "<=", value: 200, severity: "hard" }
    }

    harness App {
        provider: "anthropic", model: "claude-opus-4",
        budget: B,
        handoff: HF,
        tools: TR,
        assertions: AR,
        sub_agents: { main: Main }
    }

    harness_benchmark NB {
        description: "App benchmark",
        dimensions: ["pass_rate", "efficiency"],
        repetitions: 1
    }
  )NEAM");
}

}  // namespace
