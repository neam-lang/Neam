// Parser/AST generation tests for all Neam language constructs.
#include <gtest/gtest.h>
#include "neamc/parser.hpp"
#include "neamc/ast.hpp"

using namespace neamc;

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static Program parse(const std::string& source) {
    Parser parser(source);
    return parser.parse();
}

// ---------------------------------------------------------------------------
// Literals
// ---------------------------------------------------------------------------

TEST(ParserLiterals, NumberLiteral) {
    auto prog = parse("emit 42;");
    ASSERT_FALSE(prog.statements.empty());
}

TEST(ParserLiterals, FloatingPointLiteral) {
    auto prog = parse("emit 3.14;");
    ASSERT_FALSE(prog.statements.empty());
}

TEST(ParserLiterals, StringLiteral) {
    auto prog = parse("emit \"hello world\";");
    ASSERT_FALSE(prog.statements.empty());
}

TEST(ParserLiterals, BooleanTrue) {
    auto prog = parse("emit true;");
    ASSERT_FALSE(prog.statements.empty());
}

TEST(ParserLiterals, BooleanFalse) {
    auto prog = parse("emit false;");
    ASSERT_FALSE(prog.statements.empty());
}

TEST(ParserLiterals, NilLiteral) {
    auto prog = parse("emit nil;");
    ASSERT_FALSE(prog.statements.empty());
}

// ---------------------------------------------------------------------------
// Variable declarations
// ---------------------------------------------------------------------------

TEST(ParserVariables, LetDeclaration) {
    auto prog = parse("let x = 10;");
    ASSERT_EQ(prog.statements.size(), 1u);
}

TEST(ParserVariables, LetWithStringInit) {
    auto prog = parse("let name = \"neam\";");
    ASSERT_EQ(prog.statements.size(), 1u);
}

TEST(ParserVariables, Reassignment) {
    auto prog = parse("let x = 1; x = 2;");
    ASSERT_EQ(prog.statements.size(), 2u);
}

TEST(ParserVariables, MultipleDeclarations) {
    auto prog = parse("let a = 1; let b = 2; let c = a;");
    ASSERT_EQ(prog.statements.size(), 3u);
}

// ---------------------------------------------------------------------------
// Binary / unary operators
// ---------------------------------------------------------------------------

TEST(ParserOperators, Arithmetic) {
    auto prog = parse("emit 1 + 2 * 3 - 4 / 2;");
    ASSERT_FALSE(prog.statements.empty());
}

TEST(ParserOperators, Modulo) {
    auto prog = parse("emit 10 % 3;");
    ASSERT_FALSE(prog.statements.empty());
}

TEST(ParserOperators, Comparison) {
    auto prog = parse("emit 1 < 2; emit 3 > 2; emit 1 <= 1; emit 2 >= 2;");
    ASSERT_EQ(prog.statements.size(), 4u);
}

TEST(ParserOperators, Equality) {
    auto prog = parse("emit 1 == 1; emit 1 != 2;");
    ASSERT_EQ(prog.statements.size(), 2u);
}

TEST(ParserOperators, LogicalAndOr) {
    // Neam uses ! for negation, nested conditions for and/or
    auto prog = parse("if (true) { if (false) { emit 1; } }");
    ASSERT_FALSE(prog.statements.empty());
}

TEST(ParserOperators, UnaryNegate) {
    auto prog = parse("emit -5;");
    ASSERT_FALSE(prog.statements.empty());
}

TEST(ParserOperators, UnaryNot) {
    auto prog = parse("emit !true;");
    ASSERT_FALSE(prog.statements.empty());
}

// ---------------------------------------------------------------------------
// Control flow
// ---------------------------------------------------------------------------

TEST(ParserControlFlow, IfStatement) {
    auto prog = parse("if (true) { emit 1; }");
    ASSERT_EQ(prog.statements.size(), 1u);
}

TEST(ParserControlFlow, IfElse) {
    auto prog = parse("if (false) { emit 1; } else { emit 2; }");
    ASSERT_EQ(prog.statements.size(), 1u);
}

TEST(ParserControlFlow, WhileLoop) {
    auto prog = parse("let i = 0; while (i < 5) { i = i + 1; }");
    ASSERT_EQ(prog.statements.size(), 2u);
}

TEST(ParserControlFlow, ForLoop) {
    // Neam uses while loops instead of for loops
    auto prog = parse("let i = 0; while (i < 3) { emit i; i = i + 1; }");
    ASSERT_EQ(prog.statements.size(), 2u);
}

TEST(ParserControlFlow, NestedIf) {
    auto prog = parse("if (true) { if (false) { emit 1; } else { emit 2; } }");
    ASSERT_FALSE(prog.statements.empty());
}

// ---------------------------------------------------------------------------
// Functions
// ---------------------------------------------------------------------------

TEST(ParserFunctions, Declaration) {
    auto prog = parse("fun add(a, b) { return a + b; }");
    ASSERT_EQ(prog.statements.size(), 1u);
}

TEST(ParserFunctions, Call) {
    auto prog = parse("fun f() { return 1; } emit f();");
    ASSERT_EQ(prog.statements.size(), 2u);
}

TEST(ParserFunctions, Recursive) {
    auto prog = parse(R"(
        fun fib(n) {
            if (n <= 1) { return n; }
            return fib(n - 1) + fib(n - 2);
        }
    )");
    ASSERT_EQ(prog.statements.size(), 1u);
}

TEST(ParserFunctions, NoParams) {
    auto prog = parse("fun greet() { emit \"hi\"; }");
    ASSERT_EQ(prog.statements.size(), 1u);
}

TEST(ParserFunctions, MultipleParams) {
    auto prog = parse("fun add3(a, b, c) { return a + b + c; }");
    ASSERT_EQ(prog.statements.size(), 1u);
}

// ---------------------------------------------------------------------------
// Collections
// ---------------------------------------------------------------------------

TEST(ParserCollections, ListLiteral) {
    auto prog = parse("let xs = [1, 2, 3];");
    ASSERT_EQ(prog.statements.size(), 1u);
}

TEST(ParserCollections, EmptyList) {
    auto prog = parse("let xs = [];");
    ASSERT_EQ(prog.statements.size(), 1u);
}

TEST(ParserCollections, MapLiteral) {
    auto prog = parse("let m = {\"a\": 1, \"b\": 2};");
    ASSERT_EQ(prog.statements.size(), 1u);
}

TEST(ParserCollections, EmptyMap) {
    auto prog = parse("let m = {};");
    ASSERT_EQ(prog.statements.size(), 1u);
}

TEST(ParserCollections, IndexAccess) {
    auto prog = parse("let xs = [1,2,3]; emit xs[0];");
    ASSERT_EQ(prog.statements.size(), 2u);
}

// ---------------------------------------------------------------------------
// Agent / Skill / Knowledge / Runner / Guard declarations
// ---------------------------------------------------------------------------

TEST(ParserAgentic, AgentDeclaration) {
    auto prog = parse(R"(
        agent Greeter {
            provider: "openai"
            model: "gpt-4"
            system: "You greet people."
        }
    )");
    ASSERT_EQ(prog.statements.size(), 1u);
}

TEST(ParserAgentic, SkillDeclaration) {
    auto prog = parse(R"(
        skill add_numbers {
            description: "Adds two numbers"
            params: { a: number, b: number }
            impl(a, b) {
                return a + b;
            }
        }
    )");
    ASSERT_EQ(prog.statements.size(), 1u);
}

TEST(ParserAgentic, KnowledgeDeclaration) {
    auto prog = parse(R"(
        knowledge FAQ {
            vector_store: "memory"
            embedding_model: "text-embedding-3-small"
            chunk_size: 500
            chunk_overlap: 50
            sources: [{ type: "text", content: "FAQ content" }]
        }
    )");
    ASSERT_EQ(prog.statements.size(), 1u);
}

TEST(ParserAgentic, RunnerDeclaration) {
    auto prog = parse(R"(
        runner Pipeline {
            entry_agent: Greeter
            max_turns: 5
        }
    )");
    ASSERT_EQ(prog.statements.size(), 1u);
}

TEST(ParserAgentic, GuardDeclaration) {
    auto prog = parse(R"(
        guard ContentFilter {
            description: "Filter harmful content"
            on_observation(msg) {
                return msg;
            }
        }
    )");
    ASSERT_EQ(prog.statements.size(), 1u);
}

// ---------------------------------------------------------------------------
// Handoff and context_from syntax
// ---------------------------------------------------------------------------

TEST(ParserHandoff, BasicHandoff) {
    auto prog = parse(R"(
        handoff ToHelper {
            targets: [handoff_to(Helper)]
        }
    )");
    ASSERT_EQ(prog.statements.size(), 1u);
}

TEST(ParserHandoff, HandoffWithToolName) {
    auto prog = parse(R"(
        handoff Transfer {
            targets: [handoff_to(B) {
                tool_name: "transfer_to_b"
                description: "Transfer conversation to agent B"
            }]
        }
    )");
    ASSERT_EQ(prog.statements.size(), 1u);
}

// ---------------------------------------------------------------------------
// Edge cases
// ---------------------------------------------------------------------------

TEST(ParserEdgeCases, EmptyProgram) {
    auto prog = parse("");
    EXPECT_TRUE(prog.statements.empty());
}

TEST(ParserEdgeCases, WhitespaceOnly) {
    auto prog = parse("   \n\t\n  ");
    EXPECT_TRUE(prog.statements.empty());
}

TEST(ParserEdgeCases, DeepNesting) {
    std::string src;
    for (int i = 0; i < 20; ++i) src += "if (true) { ";
    src += "emit 1;";
    for (int i = 0; i < 20; ++i) src += " }";
    auto prog = parse(src);
    ASSERT_FALSE(prog.statements.empty());
}

TEST(ParserEdgeCases, ManyStatements) {
    std::string src;
    for (int i = 0; i < 100; ++i) src += "emit " + std::to_string(i) + "; ";
    auto prog = parse(src);
    EXPECT_EQ(prog.statements.size(), 100u);
}

TEST(ParserEdgeCases, LongIdentifier) {
    std::string name(200, 'x');
    auto prog = parse("let " + name + " = 1;");
    ASSERT_EQ(prog.statements.size(), 1u);
}

TEST(ParserEdgeCases, NestedCollections) {
    auto prog = parse("let m = {\"a\": [1, 2, {\"b\": 3}]};");
    ASSERT_EQ(prog.statements.size(), 1u);
}

// ---------------------------------------------------------------------------
// Try / Catch
// ---------------------------------------------------------------------------

TEST(ParserErrorHandling, TryCatch) {
    auto prog = parse(R"(
        try {
            emit 1 / 0;
        } catch (e) {
            emit "caught";
        }
    )");
    ASSERT_FALSE(prog.statements.empty());
}

TEST(ParserErrorHandling, Throw) {
    auto prog = parse("throw \"error\";");
    ASSERT_EQ(prog.statements.size(), 1u);
}

// ---------------------------------------------------------------------------
// Voice declarations
// ---------------------------------------------------------------------------

TEST(ParserVoice, VoicePipeline) {
    auto prog = parse(R"(
        voice MyVoice {
            stt_provider: "whisper"
            tts_provider: "openai"
            agent: Greeter
        }
    )");
    ASSERT_EQ(prog.statements.size(), 1u);
}

TEST(ParserVoice, RealtimeVoice) {
    auto prog = parse(R"(
        realtime_voice LiveVoice {
            agent: SomeAgent
            rt_provider: "openai"
            rt_model: "gpt-4o-realtime"
            rt_voice: "alloy"
        }
    )");
    ASSERT_EQ(prog.statements.size(), 1u);
}

// ---------------------------------------------------------------------------
// A2A declarations
// ---------------------------------------------------------------------------

TEST(ParserA2A, AgentCard) {
    auto prog = parse(R"(
        card MyCard {
            version: "1.0"
            description: "A test agent"
        }
    )");
    ASSERT_EQ(prog.statements.size(), 1u);
}

TEST(ParserA2A, TaskDecl) {
    auto prog = parse(R"(
        task MyTask {
            target_agent: Greeter
        }
    )");
    ASSERT_EQ(prog.statements.size(), 1u);
}
