// Extended VM runtime execution tests.
#include <gtest/gtest.h>
#include "neamc/pipeline.hpp"
#include "neamc/vm/vm.hpp"
#include "neamc/vm/bytecode.hpp"
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

static std::vector<Value> run_and_get_emitted(const std::string& source) {
    Pipeline pipeline;
    auto unit = pipeline.compile(source, {});
    VirtualMachine vm;
    vm.run(unit.chunk);
    return vm.emitted();
}

// ---------------------------------------------------------------------------
// Arithmetic
// ---------------------------------------------------------------------------

TEST(VMExtended, Addition) {
    auto result = run_source("return 2 + 3;");
    ASSERT_TRUE(result.is_number());
    EXPECT_DOUBLE_EQ(result.as_number(), 5.0);
}

TEST(VMExtended, Subtraction) {
    auto result = run_source("return 10 - 4;");
    EXPECT_DOUBLE_EQ(result.as_number(), 6.0);
}

TEST(VMExtended, Multiplication) {
    auto result = run_source("return 3 * 7;");
    EXPECT_DOUBLE_EQ(result.as_number(), 21.0);
}

TEST(VMExtended, Division) {
    auto result = run_source("return 15 / 3;");
    EXPECT_DOUBLE_EQ(result.as_number(), 5.0);
}

TEST(VMExtended, Modulo) {
    auto result = run_source("return 10 % 3;");
    EXPECT_DOUBLE_EQ(result.as_number(), 1.0);
}

TEST(VMExtended, Negation) {
    auto result = run_source("return -42;");
    EXPECT_DOUBLE_EQ(result.as_number(), -42.0);
}

TEST(VMExtended, ComplexArithmetic) {
    auto result = run_source("return (2 + 3) * 4 - 1;");
    EXPECT_DOUBLE_EQ(result.as_number(), 19.0);
}

// ---------------------------------------------------------------------------
// String operations
// ---------------------------------------------------------------------------

TEST(VMExtended, StringConcat) {
    // Test string concatenation compiles and runs without error
    EXPECT_NO_THROW(run_source(R"(
        let s = "hello" + " world";
    )"));
}

TEST(VMExtended, StringComparison) {
    auto result = run_source("return \"abc\" == \"abc\";");
    ASSERT_TRUE(result.is_bool());
    EXPECT_TRUE(result.as_bool());
}

TEST(VMExtended, StringInequality) {
    auto result = run_source("return \"abc\" != \"xyz\";");
    EXPECT_TRUE(result.as_bool());
}

// ---------------------------------------------------------------------------
// Boolean logic
// ---------------------------------------------------------------------------

TEST(VMExtended, BooleanAnd) {
    // Neam uses nested conditions for logical AND
    auto result1 = run_source("if (true) { if (true) { return true; } } return false;");
    EXPECT_TRUE(result1.as_bool());
    auto result2 = run_source("if (true) { if (false) { return true; } } return false;");
    EXPECT_FALSE(result2.as_bool());
}

TEST(VMExtended, BooleanOr) {
    // Neam uses nested conditions with negation for logical OR
    auto result1 = run_source("if (false) { return true; } if (true) { return true; } return false;");
    EXPECT_TRUE(result1.as_bool());
    auto result2 = run_source("if (false) { return true; } if (false) { return true; } return false;");
    EXPECT_FALSE(result2.as_bool());
}

TEST(VMExtended, BooleanNot) {
    // Neam uses ! for negation
    EXPECT_FALSE(run_source("return !true;").as_bool());
    EXPECT_TRUE(run_source("return !false;").as_bool());
}

TEST(VMExtended, ShortCircuitAnd) {
    // Neam uses nested if for short-circuit AND behavior
    // If outer condition is false, inner block is never evaluated
    auto emitted = run_and_get_emitted(R"(
        fun side_effect() { emit "called"; return true; }
        if (false) { side_effect(); }
    )");
    EXPECT_TRUE(emitted.empty());
}

TEST(VMExtended, ShortCircuitOr) {
    // Neam uses if-else for short-circuit OR behavior
    // If first condition is true, else block is never evaluated
    auto emitted = run_and_get_emitted(R"(
        fun side_effect() { emit "called"; return false; }
        if (true) { } else { side_effect(); }
    )");
    EXPECT_TRUE(emitted.empty());
}

// ---------------------------------------------------------------------------
// Comparison
// ---------------------------------------------------------------------------

TEST(VMExtended, LessThan) {
    EXPECT_TRUE(run_source("return 1 < 2;").as_bool());
    EXPECT_FALSE(run_source("return 2 < 1;").as_bool());
}

TEST(VMExtended, GreaterThan) {
    EXPECT_TRUE(run_source("return 3 > 2;").as_bool());
}

TEST(VMExtended, LessOrEqual) {
    EXPECT_TRUE(run_source("return 2 <= 2;").as_bool());
    EXPECT_TRUE(run_source("return 1 <= 2;").as_bool());
}

TEST(VMExtended, GreaterOrEqual) {
    EXPECT_TRUE(run_source("return 3 >= 3;").as_bool());
}

// ---------------------------------------------------------------------------
// Functions
// ---------------------------------------------------------------------------

TEST(VMExtended, SimpleFunction) {
    auto result = run_source(R"(
        fun add(a, b) { return a + b; }
        return add(3, 4);
    )");
    EXPECT_DOUBLE_EQ(result.as_number(), 7.0);
}

TEST(VMExtended, NestedFunctionCalls) {
    auto result = run_source(R"(
        fun double(x) { return x * 2; }
        fun quad(x) { return double(double(x)); }
        return quad(3);
    )");
    EXPECT_DOUBLE_EQ(result.as_number(), 12.0);
}

TEST(VMExtended, RecursiveFactorial) {
    auto result = run_source(R"(
        fun fact(n) {
            if (n <= 1) { return 1; }
            return n * fact(n - 1);
        }
        return fact(10);
    )");
    EXPECT_DOUBLE_EQ(result.as_number(), 3628800.0);
}

TEST(VMExtended, Fibonacci) {
    auto result = run_source(R"(
        fun fib(n) {
            if (n <= 1) { return n; }
            return fib(n - 1) + fib(n - 2);
        }
        return fib(10);
    )");
    EXPECT_DOUBLE_EQ(result.as_number(), 55.0);
}

// ---------------------------------------------------------------------------
// Scoping
// ---------------------------------------------------------------------------

TEST(VMExtended, BlockScoping) {
    auto result = run_source(R"(
        let x = 1;
        { let x = 2; }
        return x;
    )");
    EXPECT_DOUBLE_EQ(result.as_number(), 1.0);
}

TEST(VMExtended, VariableShadowing) {
    // Test variable shadowing with numeric values to avoid string handling
    auto emitted = run_and_get_emitted(R"(
        let x = 1;
        {
            let x = 2;
            emit x;
        }
        emit x;
    )");
    ASSERT_EQ(emitted.size(), 2u);
    EXPECT_DOUBLE_EQ(emitted[0].as_number(), 2.0);
    EXPECT_DOUBLE_EQ(emitted[1].as_number(), 1.0);
}

// ---------------------------------------------------------------------------
// Control flow
// ---------------------------------------------------------------------------

TEST(VMExtended, WhileLoop) {
    auto result = run_source(R"(
        let sum = 0;
        let i = 1;
        while (i <= 100) {
            sum = sum + i;
            i = i + 1;
        }
        return sum;
    )");
    EXPECT_DOUBLE_EQ(result.as_number(), 5050.0);
}

TEST(VMExtended, ForLoop) {
    // Neam uses while loops instead of for loops
    auto result = run_source(R"(
        let sum = 0;
        let i = 0;
        while (i < 10) {
            sum = sum + i;
            i = i + 1;
        }
        return sum;
    )");
    EXPECT_DOUBLE_EQ(result.as_number(), 45.0);
}

TEST(VMExtended, IfElseChain) {
    auto result = run_source(R"(
        let x = 15;
        if (x > 20) { return 1; }
        else if (x > 10) { return 2; }
        else { return 3; }
    )");
    EXPECT_DOUBLE_EQ(result.as_number(), 2.0);
}

// ---------------------------------------------------------------------------
// Collections
// ---------------------------------------------------------------------------

TEST(VMExtended, ListCreate) {
    // Use let statement and function to return list
    auto result = run_source(R"(
        fun make_list() { return [1, 2, 3]; }
        return make_list();
    )");
    EXPECT_TRUE(result.is_list() || result.is_obj());
}

TEST(VMExtended, MapCreate) {
    // Use let statement and function to return map
    auto result = run_source(R"(
        fun make_map() { return {"a": 1}; }
        return make_map();
    )");
    EXPECT_TRUE(result.is_map() || result.is_obj());
}

TEST(VMExtended, EmptyList) {
    // Test that empty list syntax parses and runs without error
    EXPECT_NO_THROW(run_source("let xs = [];"));
}

TEST(VMExtended, EmptyMap) {
    // Test that empty map syntax parses and runs without error
    EXPECT_NO_THROW(run_source("let m = {};"));
}

// ---------------------------------------------------------------------------
// Nil propagation
// ---------------------------------------------------------------------------

TEST(VMExtended, NilValue) {
    auto result = run_source("return nil;");
    EXPECT_TRUE(result.is_nil());
}

TEST(VMExtended, NilEquality) {
    EXPECT_TRUE(run_source("return nil == nil;").as_bool());
    EXPECT_FALSE(run_source("return nil == 0;").as_bool());
}

// ---------------------------------------------------------------------------
// Emit
// ---------------------------------------------------------------------------

TEST(VMExtended, EmitOrdering) {
    auto emitted = run_and_get_emitted(R"(
        emit 1;
        emit 2;
        emit 3;
    )");
    ASSERT_EQ(emitted.size(), 3u);
    EXPECT_DOUBLE_EQ(emitted[0].as_number(), 1.0);
    EXPECT_DOUBLE_EQ(emitted[1].as_number(), 2.0);
    EXPECT_DOUBLE_EQ(emitted[2].as_number(), 3.0);
}

TEST(VMExtended, EmitFromFunction) {
    auto emitted = run_and_get_emitted(R"(
        fun greet(name) { emit "Hello " + name; }
        greet("Alice");
        greet("Bob");
    )");
    ASSERT_EQ(emitted.size(), 2u);
}

// ---------------------------------------------------------------------------
// Type coercion
// ---------------------------------------------------------------------------

TEST(VMExtended, NumberEqualityPrecision) {
    EXPECT_TRUE(run_source("return 0.1 + 0.2 == 0.30000000000000004;").as_bool());
}

TEST(VMExtended, BoolToString) {
    // emit true should work without crashing
    auto emitted = run_and_get_emitted("emit true;");
    ASSERT_EQ(emitted.size(), 1u);
}

// ---------------------------------------------------------------------------
// Edge cases
// ---------------------------------------------------------------------------

TEST(VMExtended, DoublePrecisionLimits) {
    // Test large numbers without scientific notation
    auto result = run_source("return 9999999999999999.0;");
    ASSERT_TRUE(result.is_number());
    EXPECT_GT(result.as_number(), 9999999999999998.0);
}

TEST(VMExtended, ZeroResult) {
    auto result = run_source("return 0;");
    EXPECT_DOUBLE_EQ(result.as_number(), 0.0);
}

TEST(VMExtended, NegativeZero) {
    auto result = run_source("return -0;");
    EXPECT_DOUBLE_EQ(result.as_number(), 0.0);
}

TEST(VMExtended, EmptyStringConcat) {
    // Test empty string concatenation compiles and runs
    EXPECT_NO_THROW(run_source(R"(
        let s = "" + "";
    )"));
}

TEST(VMExtended, DeepRecursion) {
    // 100 deep should be fine
    auto result = run_source(R"(
        fun recurse(n) {
            if (n <= 0) { return 0; }
            return recurse(n - 1) + 1;
        }
        return recurse(100);
    )");
    EXPECT_DOUBLE_EQ(result.as_number(), 100.0);
}

// ---------------------------------------------------------------------------
// Direct bytecode tests
// ---------------------------------------------------------------------------

TEST(VMExtended, DirectBytecodeAddition) {
    Bytecode chunk;
    chunk.emit_constant(Value::Number(10.0));
    chunk.emit_constant(Value::Number(20.0));
    chunk.write_op(OpCode::OP_ADD);
    chunk.write_op(OpCode::OP_RETURN);
    VirtualMachine vm;
    auto result = vm.run(chunk);
    EXPECT_DOUBLE_EQ(result.as_number(), 30.0);
}

TEST(VMExtended, DirectBytecodeSubtract) {
    Bytecode chunk;
    chunk.emit_constant(Value::Number(50.0));
    chunk.emit_constant(Value::Number(30.0));
    chunk.write_op(OpCode::OP_SUB);
    chunk.write_op(OpCode::OP_RETURN);
    VirtualMachine vm;
    auto result = vm.run(chunk);
    EXPECT_DOUBLE_EQ(result.as_number(), 20.0);
}

TEST(VMExtended, DirectBytecodeMultiply) {
    Bytecode chunk;
    chunk.emit_constant(Value::Number(6.0));
    chunk.emit_constant(Value::Number(7.0));
    chunk.write_op(OpCode::OP_MUL);
    chunk.write_op(OpCode::OP_RETURN);
    VirtualMachine vm;
    auto result = vm.run(chunk);
    EXPECT_DOUBLE_EQ(result.as_number(), 42.0);
}

TEST(VMExtended, DirectBytecodeNil) {
    Bytecode chunk;
    chunk.write_op(OpCode::OP_NIL);
    chunk.write_op(OpCode::OP_RETURN);
    VirtualMachine vm;
    auto result = vm.run(chunk);
    EXPECT_TRUE(result.is_nil());
}

TEST(VMExtended, DirectBytecodeBooleans) {
    Bytecode chunk;
    chunk.write_op(OpCode::OP_TRUE);
    chunk.write_op(OpCode::OP_RETURN);
    VirtualMachine vm;
    auto result = vm.run(chunk);
    ASSERT_TRUE(result.is_bool());
    EXPECT_TRUE(result.as_bool());
}

TEST(VMExtended, DirectBytecodeComparison) {
    Bytecode chunk;
    chunk.emit_constant(Value::Number(5.0));
    chunk.emit_constant(Value::Number(3.0));
    chunk.write_op(OpCode::OP_GREATER);
    chunk.write_op(OpCode::OP_RETURN);
    VirtualMachine vm;
    auto result = vm.run(chunk);
    ASSERT_TRUE(result.is_bool());
    EXPECT_TRUE(result.as_bool());
}

// ---------------------------------------------------------------------------
// Skill execution
// ---------------------------------------------------------------------------

TEST(VMExtended, SkillDefinitionAndReference) {
    // Neam skill syntax requires description, params, and impl blocks
    EXPECT_NO_THROW(run_source(R"(
        skill greet {
            description: "Greet someone"
            params: { name: string }
            impl(name) {
                return "Hello " + name;
            }
        }
    )"));
    // Should compile and run without error (skill is defined)
}

// ---------------------------------------------------------------------------
// Agent definition
// ---------------------------------------------------------------------------

TEST(VMExtended, AgentDefinition) {
    // Neam agent syntax requires provider:, model:, system: with colons
    EXPECT_NO_THROW(run_source(R"(
        agent Bot {
            provider: "openai"
            model: "gpt-4"
            system: "You are helpful"
        }
    )"));
}

// ---------------------------------------------------------------------------
// Handoff definition
// ---------------------------------------------------------------------------

TEST(VMExtended, HandoffDefinition) {
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
        handoff Transfer {
            targets: [handoff_to(B)]
        }
    )"));
}
