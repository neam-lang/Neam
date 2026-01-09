//
// Neam Virtual Machine - Interpreter
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include "neamc/vm/bytecode.hpp"
#include "neamc/vm/memory.hpp"
#include "neamc/vm/native.hpp"
#include "neamc/vm/object.hpp"
#include "neamc/vm/table.hpp"
#include "neamc/vm/trace.hpp"
#include "neamc/vm/value.hpp"

namespace neamc::vm
{
class VirtualMachine
{
public:
  enum class DebugEventType
  {
    BeforeAgentAsk,
    BeforeToolExecution
  };

  struct DebugEvent
  {
    DebugEventType type;
    std::string label;
    std::size_t ip = 0;
    std::string payload;
  };

  using DebugHook = std::function<void(const DebugEvent&)>;

  struct CallFrame
  {
    const Bytecode* chunk = nullptr;
    ObjFunction* function = nullptr;
    std::size_t ip = 0;
    std::size_t stack_start = 0;
    bool is_tool = false;
    std::string tool_name;
  };
  VirtualMachine();
  ~VirtualMachine();
  Value run(const Bytecode& chunk);
  void define_native(const std::string& name, int arity, NativeFn function);
  void set_debug_hook(DebugHook hook) { debug_hook_ = std::move(hook); }
  void push_root(Value value);
  void pop_root();
  const std::vector<Value>& stack() const { return stack_; }
  const std::vector<CallFrame>& frames() const { return frames_; }
  const std::vector<Value>& roots() const { return gc_roots_; }
  const std::vector<Value>& emitted() const { return emitted_; }
  std::unordered_set<std::string>& allowed_skills() { return allowed_skills_; }
  Table& globals() { return globals_; }
  Table& strings() { return interned_strings_; }
  ObjEnv* env() const { return env_; }
  std::unordered_map<std::string, ObjKnowledge*>& knowledge_bases() { return knowledge_bases_; }

private:
  void emit_debug_event(DebugEventType type, std::string label, std::size_t ip,
                        std::string payload);
  Value pop();
  Value& peek();
  Value& peek_offset(std::size_t distance);
  static bool is_truthy(const Value& value);
  Value binary_numeric_op(const Value& lhs, const Value& rhs, OpCode op);
  uint16_t read_short(const std::vector<uint8_t>& code, std::size_t& ip);
  bool values_equal(const Value& lhs, const Value& rhs);
  Value concatenate(const Value& lhs, const Value& rhs);

  std::vector<Value> stack_{};
  std::vector<CallFrame> frames_{};
  std::vector<Value> gc_roots_{};
  std::vector<Value> emitted_{};
  std::unordered_set<std::string> allowed_skills_{};
  std::unordered_map<std::string, ObjKnowledge*> knowledge_bases_{};
  ObjEnv* env_{nullptr};
  Table globals_{};
  Table interned_strings_{};
  DebugHook debug_hook_{};
  TraceLogger trace_logger_{};
};
}  // namespace neamc::vm
