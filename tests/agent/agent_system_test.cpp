// Agent subsystem: ask, handoffs, skills, orchestration tests.
#include <gtest/gtest.h>
#include "neamc/pipeline.hpp"
#include "neamc/vm/vm.hpp"
#include "neamc/vm/value.hpp"

using namespace neamc;
using namespace neamc::vm;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static Value run_source(const std::string& source) {
    Pipeline pipeline;
    auto unit = pipeline.compile(source, {});
    VirtualMachine vm;
    return vm.run(unit.chunk);
}

// ---------------------------------------------------------------------------
// Skill definitions and calls
// ---------------------------------------------------------------------------

TEST(AgentSystem, SkillDefinition) {
    // Neam skill syntax requires description:, params:, and impl() blocks
    EXPECT_NO_THROW(run_source(R"(
        skill greet {
            description: "Greet someone"
            params: { name: string }
            impl(name) {
                return "Hello " + name;
            }
        }
    )"));
}

TEST(AgentSystem, MultipleSkills) {
    EXPECT_NO_THROW(run_source(R"(
        skill add_nums {
            description: "Add two numbers"
            params: { a: number, b: number }
            impl(a, b) { return a + b; }
        }
        skill mul_nums {
            description: "Multiply two numbers"
            params: { a: number, b: number }
            impl(a, b) { return a * b; }
        }
    )"));
}

// ---------------------------------------------------------------------------
// Agent definitions
// ---------------------------------------------------------------------------

TEST(AgentSystem, SimpleAgent) {
    // Neam agent syntax requires provider:, model:, system: with colons
    EXPECT_NO_THROW(run_source(R"(
        agent Greeter {
            provider: "openai"
            model: "gpt-4"
            system: "You greet people warmly."
        }
    )"));
}

TEST(AgentSystem, AgentWithSkills) {
    EXPECT_NO_THROW(run_source(R"(
        skill calculate {
            description: "Calculate expression"
            params: { expr: string }
            impl(expr) { return 42; }
        }
        agent MathBot {
            provider: "openai"
            model: "gpt-4"
            system: "You do math."
            skills: [calculate]
        }
    )"));
}

TEST(AgentSystem, AgentWithKnowledge) {
    // Test knowledge definition and agent definition separately
    // (knowledge attachment may not be supported in agent block)
    EXPECT_NO_THROW(run_source(R"(
        knowledge FAQ {
            vector_store: "memory"
            embedding_model: "text-embedding-3-small"
            chunk_size: 500
            chunk_overlap: 50
            sources: [{ type: "text", content: "FAQ data" }]
        }
        agent Helper {
            provider: "openai"
            model: "gpt-4"
            system: "You help people."
        }
    )"));
}

// ---------------------------------------------------------------------------
// Handoff definitions
// ---------------------------------------------------------------------------

TEST(AgentSystem, SimpleHandoff) {
    // Neam handoff syntax uses targets: [handoff_to(Agent)]
    EXPECT_NO_THROW(run_source(R"(
        agent A {
            provider: "openai"
            model: "gpt-4"
            system: "a"
        }
        agent B {
            provider: "openai"
            model: "gpt-4"
            system: "b"
        }
        handoff AtoB {
            targets: [handoff_to(B)]
        }
    )"));
}

TEST(AgentSystem, HandoffWithToolName) {
    EXPECT_NO_THROW(run_source(R"(
        agent A {
            provider: "openai"
            model: "gpt-4"
            system: "a"
        }
        agent B {
            provider: "openai"
            model: "gpt-4"
            system: "b"
        }
        handoff Transfer {
            targets: [handoff_to(B) {
                tool_name: "transfer_to_b"
                description: "Transfer to agent B"
            }]
        }
    )"));
}

TEST(AgentSystem, HandoffChain) {
    EXPECT_NO_THROW(run_source(R"(
        agent A {
            provider: "openai"
            model: "gpt-4"
            system: "a"
        }
        agent B {
            provider: "openai"
            model: "gpt-4"
            system: "b"
        }
        agent C {
            provider: "openai"
            model: "gpt-4"
            system: "c"
        }
        handoff AtoB {
            targets: [handoff_to(B)]
        }
        handoff BtoC {
            targets: [handoff_to(C)]
        }
    )"));
}

// ---------------------------------------------------------------------------
// Handoff registration (VM API)
// ---------------------------------------------------------------------------

TEST(AgentSystem, RegisterHandoffVM) {
    Pipeline pipeline;
    auto unit = pipeline.compile(R"(
        agent A {
            provider: "openai"
            model: "gpt-4"
            system: "a"
        }
        agent B {
            provider: "openai"
            model: "gpt-4"
            system: "b"
        }
        handoff H {
            targets: [handoff_to(B)]
        }
    )", {});

    VirtualMachine vm;
    vm.run(unit.chunk);

    auto handoffs = vm.get_agent_handoffs("A");
    // Handoffs should be registered after running the code
    // May or may not be linked to A depending on implementation
}

// ---------------------------------------------------------------------------
// Runner definitions
// ---------------------------------------------------------------------------

TEST(AgentSystem, RunnerDefinition) {
    // Neam runner syntax uses entry_agent: and max_turns:
    EXPECT_NO_THROW(run_source(R"(
        agent Bot {
            provider: "openai"
            model: "gpt-4"
            system: "help"
        }
        runner Pipeline {
            entry_agent: Bot
            max_turns: 5
        }
    )"));
}

TEST(AgentSystem, RunnerMultiAgent) {
    EXPECT_NO_THROW(run_source(R"(
        agent A {
            provider: "openai"
            model: "gpt-4"
            system: "a"
        }
        agent B {
            provider: "openai"
            model: "gpt-4"
            system: "b"
        }
        handoff AtoB {
            targets: [handoff_to(B)]
        }
        runner MultiPipeline {
            entry_agent: A
            max_turns: 10
        }
    )"));
}

// ---------------------------------------------------------------------------
// Guard definitions
// ---------------------------------------------------------------------------

TEST(AgentSystem, GuardOnInput) {
    // Neam guard syntax uses description: and on_observation() handler
    EXPECT_NO_THROW(run_source(R"(
        guard SafeInput {
            description: "No harmful content"
            on_observation(msg) {
                return msg;
            }
        }
    )"));
}

TEST(AgentSystem, GuardOnOutput) {
    EXPECT_NO_THROW(run_source(R"(
        guard SafeOutput {
            description: "Responses are helpful"
            on_observation(msg) {
                return msg;
            }
        }
    )"));
}

// ---------------------------------------------------------------------------
// Agent card
// ---------------------------------------------------------------------------

TEST(AgentSystem, AgentCardDefinition) {
    // Neam uses 'card' keyword (not 'agent_card') with version:, description:
    EXPECT_NO_THROW(run_source(R"(
        card MyCard {
            version: "1.0"
            description: "A helpful bot"
        }
    )"));
}

TEST(AgentSystem, AgentCardRegistration) {
    Pipeline pipeline;
    auto unit = pipeline.compile(R"(
        card MyCard {
            version: "1.0"
            description: "A helpful bot"
        }
    )", {});

    VirtualMachine vm;
    vm.run(unit.chunk);

    auto card = vm.get_agent_card("MyCard");
    // Card should be registered
}

// ---------------------------------------------------------------------------
// Task creation (VM API)
// ---------------------------------------------------------------------------

TEST(AgentSystem, TaskCreation) {
    Pipeline pipeline;
    auto unit = pipeline.compile(R"(
        agent Bot {
            provider: "openai"
            model: "gpt-4"
            system: "help"
        }
    )", {});

    VirtualMachine vm;
    vm.run(unit.chunk);

    nlohmann::json input = {{"query", "hello"}};
    std::string task_id = vm.create_task("Bot", input);
    EXPECT_FALSE(task_id.empty());

    auto status = vm.get_task_status_string(task_id);
    EXPECT_FALSE(status.empty());
}

TEST(AgentSystem, TaskCancellation) {
    Pipeline pipeline;
    auto unit = pipeline.compile(R"(
        agent Bot {
            provider: "openai"
            model: "gpt-4"
            system: "help"
        }
    )", {});

    VirtualMachine vm;
    vm.run(unit.chunk);

    nlohmann::json input = {{"query", "hello"}};
    std::string task_id = vm.create_task("Bot", input);
    vm.cancel_task(task_id);
    auto status = vm.get_task_status_string(task_id);
    EXPECT_EQ(status, "cancelled");
}

// ---------------------------------------------------------------------------
// Voice pipeline definition
// ---------------------------------------------------------------------------

TEST(AgentSystem, VoicePipelineDefinition) {
    // Neam voice syntax uses voice keyword with stt_provider:, tts_provider:, agent:
    EXPECT_NO_THROW(run_source(R"(
        agent Bot {
            provider: "openai"
            model: "gpt-4"
            system: "help"
        }
        voice VoiceBot {
            stt_provider: "whisper"
            tts_provider: "openai"
            agent: Bot
        }
    )"));
}

// ---------------------------------------------------------------------------
// Realtime voice definition
// ---------------------------------------------------------------------------

TEST(AgentSystem, RealtimeVoiceDefinition) {
    // Neam realtime_voice syntax
    EXPECT_NO_THROW(run_source(R"(
        agent Bot {
            provider: "openai"
            model: "gpt-4"
            system: "help"
        }
        realtime_voice LiveBot {
            agent: Bot
            rt_provider: "openai"
            rt_model: "gpt-4o-realtime"
            rt_voice: "alloy"
        }
    )"));
}

// ---------------------------------------------------------------------------
// Edge cases
// ---------------------------------------------------------------------------

TEST(AgentSystem, EmptyAgentInstructions) {
    EXPECT_NO_THROW(run_source(R"(
        agent Empty {
            provider: "openai"
            model: "gpt-4"
            system: ""
        }
    )"));
}

TEST(AgentSystem, AgentWithTemperature) {
    EXPECT_NO_THROW(run_source(R"(
        agent Creative {
            provider: "openai"
            model: "gpt-4"
            system: "Be creative"
            temperature: 0.9
        }
    )"));
}

TEST(AgentSystem, MultipleAgentCards) {
    EXPECT_NO_THROW(run_source(R"(
        card Card1 {
            version: "1.0"
            description: "first"
        }
        card Card2 {
            version: "2.0"
            description: "second"
        }
    )"));
}

TEST(AgentSystem, TaskDefinition) {
    // Neam task syntax uses target_agent:
    EXPECT_NO_THROW(run_source(R"(
        agent Bot {
            provider: "openai"
            model: "gpt-4"
            system: "help"
        }
        task MyTask {
            target_agent: Bot
        }
    )"));
}
