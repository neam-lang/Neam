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

  // v0.6.7: External skill adoption
  OP_DEFINE_EXTERN_SKILL,  // Stack: [name, desc, param_names, params, binding_type, binding_config]
  OP_DEFINE_MCP_SERVER,    // Stack: [name, config_map]
  OP_ADOPT_MCP_TOOLS,      // Stack: [server_name, filter_list, alias_or_nil]

  // v0.7.0: Data types & data processing
  OP_SET_INDEX,       // Stack: [base, index, value] -> [value]
  OP_GET_ITER,        // Stack: [collection] -> [iter_state]
  OP_FOR_ITER,        // Operand: uint16 exit_offset. Stack: [iter_state] -> [iter_state, next_val] or jump
  OP_BUILD_SET,       // Operand: uint16 count
  OP_BUILD_TUPLE,     // Operand: uint16 count
  OP_CONTAINS,        // Stack: [element, collection] -> [bool]
  OP_FORMAT_STRING,   // Operand: uint16 part_count
  OP_UNPACK,          // Operand: uint16 count
  OP_UNPACK_REST,     // Operands: uint16 before_count, uint16 after_count
  OP_SLICE            // Operand: uint8 flags (bit0=has_start, bit1=has_end, bit2=has_step)
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
