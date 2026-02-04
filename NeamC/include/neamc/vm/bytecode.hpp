// SPDX-License-Identifier: Apache-2.0
//
// Neam Virtual Machine - Bytecode definition
//

#pragma once

#include <cstddef>
#include <cstdint>
#include <iosfwd>
#include <limits>
#include <string>
#include <vector>

#include "neamc/vm/value.hpp"

namespace neamc::vm
{
enum class OpCode : uint8_t
{
  OP_CONST = 0,
  OP_NIL,
  OP_TRUE,
  OP_FALSE,
  OP_POP,
  OP_DUP,
  OP_GET_LOCAL,
  OP_SET_LOCAL,
  OP_DEFINE_GLOBAL,
  OP_GET_GLOBAL,
  OP_SET_GLOBAL,

  OP_NEGATE,
  OP_NOT,

  OP_ADD,
  OP_SUB,
  OP_MUL,
  OP_DIV,
  OP_MOD,
  OP_EQUAL,
  OP_GREATER,
  OP_LESS,

  OP_JUMP,
  OP_JUMP_IF_FALSE,
  OP_LOOP,

  OP_CALL,
  OP_CALL_NATIVE,
  OP_GET_PROPERTY,
  OP_GET_INDEX,
  OP_INVOKE,
  OP_AWAIT,
  OP_RETURN,

  OP_EMIT,
  OP_TRACE,

  OP_BUILD_LIST,
  OP_BUILD_MAP,
  OP_DEFINE_SKILL,
  OP_DEFINE_KNOWLEDGE,
  OP_DEFINE_AGENT,
  OP_GRANT,
  OP_CHECKPOINT,
  OP_REWIND,

  // NEW: Agentic Orchestration opcodes
  OP_DEFINE_HANDOFF,
  OP_DEFINE_AGENT_CARD,
  OP_DEFINE_TASK,
  OP_DEFINE_RUNNER,
  OP_EXECUTE_HANDOFF,
  OP_CREATE_TASK,
  OP_SUBMIT_TASK,
  OP_GET_TASK_STATUS,
  OP_RUN_AGENT_LOOP,

  // Error handling opcodes
  OP_TRY_BEGIN,   // Push exception handler; operand: uint16_t catch_offset
  OP_TRY_END,     // Pop exception handler (normal exit from try block)
  OP_THROW,       // Throw top-of-stack value as exception

  // Async opcodes
  OP_AWAIT_ALL,    // Pop list of futures, await all, push list of results

  // Voice pipeline opcodes
  OP_DEFINE_VOICE_PIPELINE,
  OP_VOICE_TRANSCRIBE,
  OP_VOICE_SYNTHESIZE,
  OP_VOICE_PIPELINE_RUN,

  // Realtime voice opcodes
  OP_DEFINE_REALTIME_VOICE,
  OP_REALTIME_CONNECT,
  OP_REALTIME_SEND_AUDIO,
  OP_REALTIME_SEND_TEXT,
  OP_REALTIME_ON_EVENT,
  OP_REALTIME_TOOL_RESULT,
  OP_REALTIME_CLOSE,

  // Cognitive opcodes (v0.5.0)
  OP_DEFINE_REASONING,
  OP_DEFINE_REFLECTION,
  OP_DEFINE_LEARNING,
  OP_DEFINE_GOALS,
  OP_DEFINE_EVOLUTION,
  OP_DEFINE_INNER_MODEL,
  OP_AGENT_RATE,
  OP_AGENT_REFLECT,
  OP_AGENT_EVOLVE,
  OP_AGENT_STATUS
};

class Bytecode
{
public:
  struct SourceMapEntry
  {
    std::size_t offset = 0;
    std::size_t line = 0;
  };

  std::size_t add_constant(Value value);
  void write_op(OpCode op);
  void write_byte(uint8_t value);
  void write_short(uint16_t value);
  void emit_constant(Value value);
  void patch_short(std::size_t index, uint16_t value);

  void set_manifest(std::string manifest) { manifest_ = std::move(manifest); }
  const std::string& manifest() const { return manifest_; }
  void set_current_line(std::size_t line) { current_line_ = line; }
  void clear_source_map() { source_map_.clear(); }
  const std::vector<SourceMapEntry>& source_map() const { return source_map_; }

  void serialize(std::ostream& out) const;
  static Bytecode deserialize(std::istream& in);

  const std::vector<uint8_t>& code() const { return code_; }
  const std::vector<Value>& constants() const { return constants_; }

private:
  std::vector<uint8_t> code_{};
  std::vector<Value> constants_{};
  std::string manifest_{};
  std::vector<SourceMapEntry> source_map_{};
  std::size_t current_line_ = std::numeric_limits<std::size_t>::max();
};

using Chunk = Bytecode;
}  // namespace neamc::vm
