// Extended compiler bytecode verification tests.
#include <gtest/gtest.h>
#include "neamc/parser.hpp"
#include "neamc/compiler.hpp"
#include "neamc/vm/bytecode.hpp"
#include "neamc/vm/value.hpp"
#include "neamc/pipeline.hpp"

using namespace neamc;
using namespace neamc::vm;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static Chunk compile_source(const std::string& source) {
    Parser parser(source);
    auto program = parser.parse();
    Compiler compiler;
    return compiler.compile(program);
}

static bool has_opcode(const Chunk& chunk, OpCode op) {
    for (auto byte : chunk.code()) {
        if (byte == static_cast<uint8_t>(op)) return true;
    }
    return false;
}

// ---------------------------------------------------------------------------
// String operations
// ---------------------------------------------------------------------------

TEST(CompilerExtended, StringConcatenation) {
    auto chunk = compile_source("emit \"hello\" + \" world\";");
    EXPECT_TRUE(has_opcode(chunk, OpCode::OP_ADD));
    EXPECT_TRUE(has_opcode(chunk, OpCode::OP_EMIT));
}

TEST(CompilerExtended, StringComparison) {
    auto chunk = compile_source("emit \"a\" == \"b\";");
    EXPECT_TRUE(has_opcode(chunk, OpCode::OP_EQUAL));
}

// ---------------------------------------------------------------------------
// Comparison operators
// ---------------------------------------------------------------------------

TEST(CompilerExtended, LessThan) {
    auto chunk = compile_source("emit 1 < 2;");
    EXPECT_TRUE(has_opcode(chunk, OpCode::OP_LESS));
}

TEST(CompilerExtended, GreaterThan) {
    auto chunk = compile_source("emit 1 > 2;");
    EXPECT_TRUE(has_opcode(chunk, OpCode::OP_GREATER));
}

// ---------------------------------------------------------------------------
// Logical short-circuit
// ---------------------------------------------------------------------------

TEST(CompilerExtended, LogicalAnd) {
    // Neam uses nested conditions for logical AND
    auto chunk = compile_source("if (true) { if (false) { emit 1; } }");
    EXPECT_TRUE(has_opcode(chunk, OpCode::OP_JUMP_IF_FALSE));
}

TEST(CompilerExtended, LogicalOr) {
    // Neam uses nested conditions for logical OR
    auto chunk = compile_source("if (!true) { emit 0; } if (!false) { emit 1; }");
    EXPECT_TRUE(has_opcode(chunk, OpCode::OP_JUMP_IF_FALSE));
}

// ---------------------------------------------------------------------------
// Control flow jump offsets
// ---------------------------------------------------------------------------

TEST(CompilerExtended, NestedIfElseJumps) {
    auto chunk = compile_source(R"(
        if (true) {
            if (false) { emit 1; }
            else { emit 2; }
        } else {
            emit 3;
        }
    )");
    EXPECT_TRUE(has_opcode(chunk, OpCode::OP_JUMP_IF_FALSE));
    EXPECT_TRUE(has_opcode(chunk, OpCode::OP_JUMP));
}

TEST(CompilerExtended, WhileLoopJumps) {
    auto chunk = compile_source("let i = 0; while (i < 10) { i = i + 1; }");
    EXPECT_TRUE(has_opcode(chunk, OpCode::OP_LOOP));
    EXPECT_TRUE(has_opcode(chunk, OpCode::OP_JUMP_IF_FALSE));
}

TEST(CompilerExtended, ForLoopCompilation) {
    // Neam uses while loops instead of for loops
    auto chunk = compile_source("let i = 0; while (i < 5) { emit i; i = i + 1; }");
    EXPECT_TRUE(has_opcode(chunk, OpCode::OP_LOOP));
}

// ---------------------------------------------------------------------------
// Local variable slot allocation
// ---------------------------------------------------------------------------

TEST(CompilerExtended, MultipleLocals) {
    auto chunk = compile_source(R"(
        {
            let a = 1;
            let b = 2;
            let c = 3;
            let d = 4;
            let e = 5;
            emit a + b + c + d + e;
        }
    )");
    EXPECT_TRUE(has_opcode(chunk, OpCode::OP_GET_LOCAL));
}

// ---------------------------------------------------------------------------
// Functions
// ---------------------------------------------------------------------------

TEST(CompilerExtended, FunctionCompilation) {
    auto chunk = compile_source(R"(
        fun square(x) { return x * x; }
        emit square(5);
    )");
    EXPECT_TRUE(has_opcode(chunk, OpCode::OP_CALL));
}

TEST(CompilerExtended, RecursiveFunctionCompiles) {
    auto chunk = compile_source(R"(
        fun fact(n) {
            if (n <= 1) { return 1; }
            return n * fact(n - 1);
        }
    )");
    EXPECT_TRUE(has_opcode(chunk, OpCode::OP_RETURN));
}

TEST(CompilerExtended, EmptyFunctionBody) {
    auto chunk = compile_source("fun noop() {} noop();");
    EXPECT_TRUE(has_opcode(chunk, OpCode::OP_CALL));
}

// ---------------------------------------------------------------------------
// Collections
// ---------------------------------------------------------------------------

TEST(CompilerExtended, ListLiteralBytecode) {
    auto chunk = compile_source("let xs = [1, 2, 3];");
    EXPECT_TRUE(has_opcode(chunk, OpCode::OP_BUILD_LIST));
}

TEST(CompilerExtended, MapLiteralBytecode) {
    auto chunk = compile_source("let m = {\"a\": 1, \"b\": 2};");
    EXPECT_TRUE(has_opcode(chunk, OpCode::OP_BUILD_MAP));
}

TEST(CompilerExtended, EmptyListBytecode) {
    auto chunk = compile_source("let xs = [];");
    EXPECT_TRUE(has_opcode(chunk, OpCode::OP_BUILD_LIST));
}

TEST(CompilerExtended, EmptyMapBytecode) {
    auto chunk = compile_source("let m = {};");
    EXPECT_TRUE(has_opcode(chunk, OpCode::OP_BUILD_MAP));
}

// ---------------------------------------------------------------------------
// Agent / Skill compilation
// ---------------------------------------------------------------------------

TEST(CompilerExtended, SkillCompilation) {
    auto chunk = compile_source(R"(
        skill greet {
            description: "Greet someone"
            params: { name: string }
            impl(name) {
                return "Hello " + name;
            }
        }
    )");
    EXPECT_TRUE(has_opcode(chunk, OpCode::OP_DEFINE_SKILL));
}

TEST(CompilerExtended, AgentCompilation) {
    auto chunk = compile_source(R"(
        agent Bot {
            provider: "openai"
            model: "gpt-4"
            system: "helpful"
        }
    )");
    EXPECT_TRUE(has_opcode(chunk, OpCode::OP_DEFINE_AGENT));
}

TEST(CompilerExtended, KnowledgeCompilation) {
    auto chunk = compile_source(R"(
        knowledge FAQ {
            vector_store: "memory"
            embedding_model: "text-embedding-3-small"
            chunk_size: 500
            chunk_overlap: 50
            sources: [{ type: "text", content: "FAQ data" }]
        }
    )");
    EXPECT_TRUE(has_opcode(chunk, OpCode::OP_DEFINE_KNOWLEDGE));
}

// ---------------------------------------------------------------------------
// Bytecode serialization round-trip
// ---------------------------------------------------------------------------

TEST(CompilerExtended, SerializeDeserializeRoundTrip) {
    auto chunk = compile_source("emit 1 + 2;");
    std::stringstream ss;
    chunk.serialize(ss);
    auto restored = Bytecode::deserialize(ss);
    EXPECT_EQ(chunk.code().size(), restored.code().size());
    EXPECT_EQ(chunk.constants().size(), restored.constants().size());
}

TEST(CompilerExtended, ManifestRoundTrip) {
    Pipeline pipeline;
    auto unit = pipeline.compile("emit 1;", "manifest_data");
    std::stringstream ss;
    unit.chunk.serialize(ss);
    auto restored = Bytecode::deserialize(ss);
    EXPECT_EQ(restored.manifest(), "manifest_data");
}

// ---------------------------------------------------------------------------
// Edge cases
// ---------------------------------------------------------------------------

TEST(CompilerExtended, ManyConstants) {
    std::string src;
    for (int i = 0; i < 300; ++i) {
        src += "emit " + std::to_string(i) + "; ";
    }
    auto chunk = compile_source(src);
    EXPECT_GE(chunk.constants().size(), 300u);
}

TEST(CompilerExtended, DeepNesting) {
    std::string src;
    for (int i = 0; i < 50; ++i) src += "{ ";
    src += "emit 1;";
    for (int i = 0; i < 50; ++i) src += " }";
    EXPECT_NO_THROW(compile_source(src));
}

TEST(CompilerExtended, HandoffCompilation) {
    // Verify handoff declaration compiles without error
    EXPECT_NO_THROW(compile_source(R"(
        handoff Transfer {
            targets: [handoff_to(B)]
        }
    )"));
}

TEST(CompilerExtended, RunnerCompilation) {
    // Verify runner declaration compiles without error
    EXPECT_NO_THROW(compile_source(R"(
        runner Pipeline {
            entry_agent: Bot
            max_turns: 10
        }
    )"));
}

TEST(CompilerExtended, TaskCompilation) {
    // Verify task declaration compiles without error
    EXPECT_NO_THROW(compile_source(R"(
        task MyTask {
            target_agent: Bot
        }
    )"));
}

TEST(CompilerExtended, AgentCardCompilation) {
    // Verify card declaration compiles without error
    EXPECT_NO_THROW(compile_source(R"(
        card Card {
            version: "1.0"
            description: "test"
        }
    )"));
}
