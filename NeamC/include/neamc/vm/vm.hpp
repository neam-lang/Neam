//
// Neam Virtual Machine - Interpreter
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <memory>

#include <mutex>

#include "neamc/security/tool_policy.hpp"
#include "neamc/vm/bytecode.hpp"
#include "neamc/vm/channel_adapter.hpp"
#include "neamc/vm/lane_queue.hpp"
#include "neamc/vm/mcp_client.hpp"
#include "neamc/vm/memory.hpp"
#include "neamc/vm/native.hpp"
#include "neamc/vm/object.hpp"
#include "neamc/vm/table.hpp"
#include "neamc/vm/trace.hpp"
#include "neamc/vm/value.hpp"

namespace neamc::vm
{

// v0.7.2: Fine-grained VM reset control for different use cases
enum class ResetPolicy
{
  Minimal,   // stack + frames + emitted only. No GC. ~0.01ms
  Standard,  // + gc_roots, skills, budgets, memory, knowledge. Run GC. ~0.05ms
  Full,      // + guards, guardchains, tools, policies, extensions, capabilities. ~0.1ms
  Complete   // + globals, interned_strings, re-register natives, OOP tables, mcp_clients. ~5ms
};

class VirtualMachine
{
public:
  // v0.7.2: Per-VM GC state (accessed by memory.hpp allocate_object template)
  Obj* objects_{nullptr};
  std::size_t bytes_allocated_{0};
  std::size_t next_gc_{1024 * 1024};

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
  void reset_for_reuse(ResetPolicy policy = ResetPolicy::Standard);
  Value run(const Bytecode& chunk);
  Value run(const Bytecode& chunk, std::istream* input, std::ostream* output);
  void set_io(std::istream* input, std::ostream* output);
  std::istream& input_stream() const;
  std::ostream& output_stream() const;
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

  // v0.8 Phase 6: Channel + Lane + Trait public API
  bool has_agent_trait_impl(const std::string& type, const std::string& trait) const;
  Value invoke_trait_method(const std::string& type, const std::string& method, Value self,
                            const std::vector<Value>& args);
  ObjChannel* get_channel(const std::string& name) const;
  const std::unordered_map<std::string, ObjClawAgent*>& claw_agents() const { return claw_agents_; }
  LaneQueueEngine* lane_engine() { return lane_engine_.get(); }
  std::mutex& execution_mutex() { return execution_mutex_; }

private:
  struct BudgetDefinition
  {
    double max_tokens{0.0};
    double max_api_calls{0.0};
    double max_wall_time_ms{0.0};
    double max_cost{0.0};
  };

  struct BudgetTracker
  {
    BudgetDefinition limits{};
    double used_tokens{0.0};
    double used_api_calls{0.0};
    double used_cost{0.0};
    int64_t start_time_ms{0};

    bool is_exhausted() const;
    int64_t elapsed_ms(int64_t now_ms) const;
  };

  struct GuardHandlerDef
  {
    std::string type;
    std::vector<std::string> parameters;
    ObjFunction* impl{nullptr};
  };

  struct GuardDef
  {
    std::string description;
    std::vector<GuardHandlerDef> handlers;
  };

  struct GuardChainDef
  {
    std::vector<std::string> guards;
  };

  struct ToolDef
  {
    std::string description;
    std::vector<std::string> capabilities;
    std::unordered_map<std::string, double> budget_costs;
    std::vector<std::string> guards;
    ObjFunction* impl{nullptr};
  };

  struct AgentExtension
  {
    std::string agent_type;  // v0.8: "claw", "forge", or "" (legacy)
    std::vector<std::string> required_capabilities;
    std::vector<std::string> guardchains;
    std::string policy;  // v0.6.9: security policy reference
    std::string budget;
    std::string env;
    std::string memory;
    std::string world_model;
    std::string plan;
    std::string connector;
  };

  struct MemoryEvent
  {
    int64_t timestamp{0};
    std::string type;
    std::string data;
    std::string agent;
  };

  struct MemoryStore
  {
    std::vector<MemoryEvent> events;
    std::vector<std::size_t> checkpoints;
    std::unordered_map<std::string, std::size_t> labeled_checkpoints;
  };

  void emit_debug_event(DebugEventType type, std::string label, std::size_t ip,
                        std::string payload);
  void register_builtin_traits();
  Value run_internal(const Bytecode& chunk);
  Value run_frames(std::size_t target_frame_count);
  Value call_function(ObjFunction* fn, const std::vector<Value>& args, bool is_tool,
                      const std::string& tool_name);
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
  std::unordered_map<std::string, BudgetDefinition> budgets_{};
  std::unordered_map<std::string, BudgetTracker> budget_trackers_{};
  std::unordered_map<std::string, AgentExtension> agent_extensions_{};
  std::unordered_map<std::string, GuardDef> guards_{};
  std::unordered_map<std::string, GuardChainDef> guardchains_{};
  std::unordered_map<std::string, ToolDef> tools_{};
  std::unordered_map<std::string, security::PolicyDef> policies_{};  // v0.6.9
  std::unordered_map<std::string, MemoryStore> memory_stores_{};
  std::unordered_map<std::string, std::unordered_set<std::string>> entity_capabilities_{};
  std::unordered_map<std::string, std::unique_ptr<McpClient>> mcp_clients_{};
  // v0.7.1: OOP runtime tables
  std::unordered_map<std::string, ObjStructDef*> struct_defs_{};
  std::unordered_map<std::string, ObjImplTable*> impl_tables_{};
  // v0.7.1 Phase 2: trait + sealed runtime tables
  std::unordered_map<std::string, ObjTraitDef*> trait_defs_{};
  std::unordered_map<std::string, ObjSealedDef*> sealed_defs_{};
  // v0.8: Agent type runtime registries
  std::unordered_map<std::string, ObjClawAgent*> claw_agents_{};
  std::unordered_map<std::string, ObjForgeAgent*> forge_agents_{};
  // v0.8 Phase 6: Channels, lanes, execution mutex
  std::unordered_map<std::string, ObjChannel*> channel_registry_{};
  std::unordered_map<std::string, std::unique_ptr<ChannelAdapter>> channel_adapters_{};
  std::unique_ptr<LaneQueueEngine> lane_engine_;
  std::mutex execution_mutex_;
  ObjEnv* env_{nullptr};
  Table globals_{};
  Table interned_strings_{};
  DebugHook debug_hook_{};
  TraceLogger trace_logger_{};
  std::istream* input_{nullptr};
  std::ostream* output_{nullptr};
};
}  // namespace neamc::vm
