// SPDX-License-Identifier: Apache-2.0
//
// Neam Virtual Machine - Interpreter
//

#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <iosfwd>
#include <memory>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <nlohmann/json.hpp>

#include "neamc/llm/provider.hpp"
#include "neamc/vm/bytecode.hpp"
#include "neamc/vm/memory.hpp"
#include "neamc/vm/native.hpp"
#include "neamc/vm/object.hpp"
#include "neamc/vm/table.hpp"
#include "neamc/vm/trace.hpp"
#include "neamc/vm/memory_backend.hpp"
#include "neamc/vm/autonomous.hpp"
#include "neamc/vm/state_backend.hpp"
#include "neamc/vm/value.hpp"
#include "neamc/mcp/registry.hpp"

namespace neamc::voice
{
class RealtimeVoiceSession;
}  // namespace neamc::voice

namespace neamc::vm
{
class HealthManager;
class TelemetryExporter;
class LLMGateway;

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

  // Task status for A2A task lifecycle (public for A2A handler access)
  enum class TaskStatusVM
  {
    kPending,
    kRunning,
    kCompleted,
    kFailed,
    kCancelled
  };

  // Task instance for runtime task tracking (public for A2A handler access)
  struct TaskInstance
  {
    std::string id;
    TaskStatusVM status{TaskStatusVM::kPending};
    std::string target_agent;
    nlohmann::json input;
    nlohmann::json output;
    int64_t created_at{0};
    int64_t started_at{0};
    int64_t completed_at{0};
    std::string error_message;
    ObjFunction* on_status_change{nullptr};
  };

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

  // Cognitive enums (v0.5.0) — public for native function access
  enum class ReasoningMode { kNone=0, kChainOfThought, kPlanAndExecute, kTreeOfThought, kSelfConsistency };
  enum class ReflectionMode { kNone=0, kEachResponse, kEveryN, kOnDemand };
  enum class LowQualityStrategy { kRevise=0, kRetry, kEscalate, kAcknowledge };
  enum class LearningStrategy { kNone=0, kExperienceReplay, kPatternExtraction, kPromptEvolution, kPreferenceLearning };

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

  // Cognitive config structs (v0.5.0)
  struct ReasoningConfig { size_t max_steps{5}; bool show_thinking{false}; size_t tree_branches{3}; size_t consistency_samples{3}; };
  struct ReflectionConfig { ReflectionMode mode{ReflectionMode::kNone}; std::vector<std::string> dimensions; double min_confidence{0.7}; LowQualityStrategy strategy{LowQualityStrategy::kRevise}; size_t max_revisions{2}; std::string escalate_to; };
  struct LearningConfig { LearningStrategy strategy{LearningStrategy::kNone}; size_t review_interval{10}; size_t max_adaptations{50}; bool rollback_on_decline{true}; };
  struct GoalConfig { std::vector<std::string> goals; std::string schedule; bool initiative{false}; size_t max_daily_calls{100}; double max_daily_cost{5.0}; size_t max_daily_tokens{0}; };
  struct EvolutionConfig { std::vector<std::string> mutable_fields; size_t review_after{50}; std::string core_identity; bool allow_rollback{true}; };

  struct AgentExtension
  {
    std::vector<std::string> required_capabilities;
    std::vector<std::string> guardchains;
    std::string budget;
    std::string env;
    std::string memory;
    std::string world_model;
    std::string plan;
    std::string connector;
    std::string output_type;
    std::vector<std::string> mcp_servers;

    // Cognitive features (v0.5.0)
    ReasoningMode reasoning_mode{ReasoningMode::kNone};
    ReasoningConfig reasoning_config;
    ReflectionConfig reflection_config;
    LearningConfig learning_config;
    GoalConfig goal_config;
    EvolutionConfig evolution_config;
    std::string model_path;
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

  // ============================================================================
  // Agentic Orchestration Runtime Structures
  // ============================================================================

  // Handoff definition for agent-to-agent transfers (OpenAI SDK style)
  struct HandoffDef
  {
    std::string target_agent;
    std::string tool_name;
    std::string description;
    ObjFunction* input_filter{nullptr};
    ObjFunction* on_handoff{nullptr};
    ObjFunction* is_enabled{nullptr};
    std::string input_type;  // Structured input type name (empty if unstructured)
  };

  // Agent Card definition for A2A Protocol
  struct AgentCardDef
  {
    std::string version;
    std::string description;
    std::vector<std::string> capabilities;
    nlohmann::json input_schema;
    nlohmann::json output_schema;
    std::string endpoint_url;
    std::string authentication;
  };

  // Runner definition for agent loop execution
  struct RunnerDef
  {
    std::string entry_agent;
    std::size_t max_turns{10};
    bool tracing_enabled{false};
    std::vector<std::string> guardrails;        // General guardrails (backward compat)
    std::vector<std::string> input_guardrails;  // Run before first agent call
    std::vector<std::string> output_guardrails; // Run before returning final output
    ObjFunction* on_turn{nullptr};
    ObjFunction* on_complete{nullptr};
  };

  // Tool call trace entry
  struct ToolCallTrace
  {
    std::string tool_name;
    std::string input;
    std::string output;
    int64_t duration_ms{0};
  };

  // Agent loop execution trace entry
  struct AgentLoopTraceEntry
  {
    std::size_t turn{0};
    std::string agent_name;
    std::string input;
    std::string output;
    std::string action;  // "response", "handoff", "tool_call"
    int64_t timestamp{0};
    int64_t duration_ms{0};
    bool was_handoff{false};
    std::string handoff_to;
    std::vector<ToolCallTrace> tool_calls;
    std::size_t input_tokens{0};
    std::size_t output_tokens{0};
  };

  // Result of running an agent loop
  struct AgentLoopResult
  {
    Value final_output;
    std::string final_agent;
    std::vector<AgentLoopTraceEntry> trace;
    std::size_t total_turns{0};
    bool completed{false};
    std::string error_message;
    int64_t start_time{0};
    int64_t end_time{0};
    int64_t total_duration_ms{0};
    std::size_t total_input_tokens{0};
    std::size_t total_output_tokens{0};
  };

  void emit_debug_event(DebugEventType type, std::string label, std::size_t ip,
                        std::string payload);

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

  struct ExceptionHandler
  {
    std::size_t catch_ip;       // IP to jump to in the catch block
    std::size_t stack_depth;    // Stack depth when handler was pushed
    std::size_t frame_depth;    // Frame depth when handler was pushed
    const Bytecode* chunk;      // Chunk the handler belongs to
  };

  std::vector<Value> stack_{};
  std::vector<CallFrame> frames_{};
  std::vector<ExceptionHandler> exception_handlers_{};
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
  std::unordered_map<std::string, MemoryStore> memory_stores_{};
  std::unique_ptr<MemoryBackend> memory_backend_;
  std::shared_ptr<StateBackend> state_backend_;
  std::unique_ptr<HealthManager> health_manager_;
  std::unique_ptr<TelemetryExporter> telemetry_;
  std::unique_ptr<LLMGateway> llm_gateway_;
  std::unordered_map<std::string, std::unordered_set<std::string>> entity_capabilities_{};
  ObjEnv* env_{nullptr};
  Table globals_{};
  Table interned_strings_{};
  DebugHook debug_hook_{};
  TraceLogger trace_logger_{};
  std::istream* input_{nullptr};
  std::ostream* output_{nullptr};

  // ============================================================================
  // Agentic Orchestration Runtime State
  // ============================================================================

  // Handoff support (OpenAI SDK style)
  std::unordered_map<std::string, std::vector<HandoffDef>> agent_handoffs_{};

  // Agent cards registry (A2A Protocol)
  std::unordered_map<std::string, AgentCardDef> agent_cards_{};

  // Task tracking (A2A Protocol)
  std::unordered_map<std::string, TaskInstance> tasks_{};
  std::atomic<uint64_t> task_counter_{0};

  // Runner definitions
  std::unordered_map<std::string, RunnerDef> runners_{};

  // MCP server registry (Phase 3)
  std::unique_ptr<mcp::McpRegistry> mcp_registry_;

  // Autonomous executor (v0.5.0)
  std::unique_ptr<AutonomousExecutor> autonomous_executor_;

  // Cognitive state maps (v0.5.0)
  std::unordered_map<std::string, nlohmann::json> agent_reflection_results_;
  std::unordered_map<std::string, size_t> learning_interaction_counts_;
  std::unordered_map<std::string, std::string> agent_evolved_prompts_;
  std::unordered_map<std::string, size_t> agent_evolution_versions_;

  // Realtime voice definitions
  struct RealtimeVoiceDef
  {
    std::string agent_name;
    std::string provider{"openai"};
    std::string model;
    std::string voice;
    std::string vad{"server"};
    double vad_threshold = 0.5;
    int silence_duration_ms = 500;
    std::string input_format{"pcm16"};
    std::string output_format{"pcm16"};
    int sample_rate = 24000;
    double speed = 1.0;
    std::string stt_endpoint;
    std::string tts_endpoint;
    std::string llm_endpoint;
  };
  std::unordered_map<std::string, RealtimeVoiceDef> realtime_voices_{};
  std::unordered_map<std::string, std::shared_ptr<voice::RealtimeVoiceSession>> realtime_sessions_{};
  int next_session_id_ = 0;

  // Voice pipeline definitions
  struct VoicePipelineDef
  {
    std::string agent_name;
    std::string stt_provider{"whisper"};
    std::string stt_model{"whisper-1"};
    std::string tts_provider{"openai"};
    std::string tts_model{"tts-1"};
    std::string tts_voice{"alloy"};
    std::string stt_endpoint;
    std::string tts_endpoint;
    std::string tts_format;
    std::string tts_speed;
    std::string tts_instructions;
    std::string stt_language;
    std::string stt_format;
  };
  std::unordered_map<std::string, VoicePipelineDef> voice_pipelines_{};

public:
  // ============================================================================
  // Agentic Orchestration Public API
  // ============================================================================

  // Handoff execution (OpenAI SDK style)
  Value execute_handoff(const std::string& from_agent, const std::string& to_agent,
                        const Value& context);
  void register_handoff(const std::string& agent_name, const HandoffDef& handoff);
  const std::vector<HandoffDef>* get_agent_handoffs(const std::string& agent_name) const;

  // Agent card operations (A2A Protocol)
  void register_agent_card(const std::string& agent_name, const AgentCardDef& card);
  const AgentCardDef* get_agent_card(const std::string& agent_name) const;
  nlohmann::json get_agent_card_json(const std::string& agent_name) const;

  // Task management (A2A Protocol)
  std::string create_task(const std::string& agent_name, const nlohmann::json& input);
  void submit_task(const std::string& task_id);
  TaskStatusVM get_task_status(const std::string& task_id) const;
  nlohmann::json get_task_result(const std::string& task_id) const;
  void cancel_task(const std::string& task_id);
  TaskInstance* get_task(const std::string& task_id);
  std::string get_task_status_string(const std::string& task_id) const;

  // Agent loop execution (Runner)
  void register_runner(const std::string& name, const RunnerDef& runner);
  AgentLoopResult run_agent_loop(const std::string& runner_name, const Value& input);
  AgentLoopResult run_agent_loop(const std::string& agent_name, const Value& input,
                                 std::size_t max_turns);

  // Guardrails execution
  // Returns true if allowed, false if blocked. Modifies value in place if guardrail transforms it.
  bool run_guardrails(const std::vector<std::string>& guardrail_names,
                      const std::string& handler_type, std::string& value);

  // Accessors for new state
  const std::unordered_map<std::string, std::vector<HandoffDef>>& agent_handoffs() const
  {
    return agent_handoffs_;
  }
  const std::unordered_map<std::string, AgentCardDef>& agent_cards() const { return agent_cards_; }
  const std::unordered_map<std::string, TaskInstance>& tasks() const { return tasks_; }
  const std::unordered_map<std::string, RunnerDef>& runners() const { return runners_; }

  void set_memory_backend(std::unique_ptr<MemoryBackend> backend);

  // Cognitive feature methods (v0.5.0)
  std::string apply_reasoning_strategy(ObjAgent* agent, llm::LLMProvider* provider,
                                        const std::string& system_prompt, const std::string& query,
                                        ReasoningMode mode, const ReasoningConfig& config,
                                        const std::vector<HandoffDef>* handoffs);
  std::string apply_reflection(ObjAgent* agent, llm::LLMProvider* provider,
                                const std::string& agent_name, const std::string& query,
                                const std::string& response, const ReflectionConfig& config);
  void trigger_learning_review(const std::string& agent_name, llm::LLMProvider* provider,
                                const LearningConfig& config);
  void apply_evolution(const std::string& agent_name, llm::LLMProvider* provider,
                        const EvolutionConfig& config);

  // Cognitive state accessors (v0.5.0) — used by native functions
  std::unordered_map<std::string, nlohmann::json>& reflection_results() { return agent_reflection_results_; }
  std::unordered_map<std::string, size_t>& learning_counts() { return learning_interaction_counts_; }
  std::unordered_map<std::string, std::string>& evolved_prompts() { return agent_evolved_prompts_; }
  std::unordered_map<std::string, size_t>& evolution_versions() { return agent_evolution_versions_; }
  std::unordered_map<std::string, AgentExtension>& extensions() { return agent_extensions_; }
  MemoryBackend* memory_backend_ptr() { return memory_backend_.get(); }
  StateBackend* state_backend_ptr() { return state_backend_.get(); }
  HealthManager* health_manager_ptr() { return health_manager_.get(); }
  TelemetryExporter* telemetry_ptr() { return telemetry_.get(); }
  LLMGateway* llm_gateway_ptr() { return llm_gateway_.get(); }
  AutonomousExecutor* autonomous_executor_ptr() { return autonomous_executor_.get(); }

  // Trace logger accessor for evaluation framework
  const TraceLogger& trace() const { return trace_logger_; }
  TraceLogger& trace() { return trace_logger_; }

  // Agent calling API (used by runner loop, A2A handler, and OP_INVOKE)
  std::string call_agent_internal(const std::string& agent_name, const std::string& query,
                                  const std::vector<HandoffDef>* handoffs = nullptr);

  // Vision overload: call agent with multi-part content including images
  std::string call_agent_internal(const std::string& agent_name, const std::string& query,
                                  const std::vector<llm::MessageContent>& images,
                                  const std::vector<HandoffDef>* handoffs = nullptr);

  // Voice pipeline operations
  // Realtime voice operations
  void register_realtime_voice(const std::string& name, const RealtimeVoiceDef& def);
  const RealtimeVoiceDef* get_realtime_voice(const std::string& name) const;
  std::string realtime_connect(const std::string& config_name);
  std::shared_ptr<voice::RealtimeVoiceSession> get_realtime_session(const std::string& session_id);

  void register_voice_pipeline(const std::string& name, const VoicePipelineDef& def);
  std::string transcribe_audio(const std::string& pipeline_name, const std::string& audio_path);
  std::string synthesize_speech(const std::string& pipeline_name, const std::string& text,
                                const std::string& output_path);
  const VoicePipelineDef* get_voice_pipeline(const std::string& name) const;
};
}  // namespace neamc::vm
