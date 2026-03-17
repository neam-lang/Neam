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
  OP_SLICE,           // Operand: uint8 flags (bit0=has_start, bit1=has_end, bit2=has_step)

  // v0.7.1: OOP — struct + impl + method dispatch
  OP_DEFINE_STRUCT,   // Operand: uint16 name_const, uint8 field_count, uint8 is_mutable
  OP_IMPL_METHOD,     // Operand: uint16 type_name_const, uint16 method_name_const, uint8 is_static
  OP_CONSTRUCT_NAMED, // Operand: uint16 type_name_const, uint8 field_count
  OP_COPY_WITH,       // Operand: uint8 override_count
  OP_SET_PROPERTY,    // Operand: uint16 name_const

  // v0.7.1 Phase 2: trait + sealed + match
  OP_DEFINE_TRAIT,    // Operand: uint16 name_const, uint8 required_count, uint8 default_count
  OP_IMPL_TRAIT,      // Operand: uint16 trait_name_const, uint16 type_name_const, uint8 method_count
  OP_DEFINE_SEALED,   // Operand: uint16 name_const, uint8 variant_count
  OP_CONSTRUCT_VARIANT, // Operand: uint16 sealed_name_const, uint16 variant_name_const, uint8 arg_count
  OP_MATCH_START,     // Operand: uint8 arm_count (sets up match context)
  OP_MATCH_VARIANT,   // Operand: uint16 variant_name_const, uint8 bind_count, uint16 skip_offset
  OP_MATCH_WILDCARD,  // No operands (always matches)
  OP_MATCH_END,       // Cleanup match context

  // v0.7.1 Phase 5: Property observers
  OP_SET_FIELD_OBSERVER,  // Operand: uint16 type_name, uint16 field_name, uint8 kind (0=willSet, 1=didSet, 2=guard)

  // v0.8: Agent type system
  OP_DEFINE_CLAW_AGENT,   // Stack: [name, provider, model, endpoint|nil, api_key_env|nil, temperature|nil, system|nil, skills_list, knowledge_list, guardchains_list, policy|nil, budget|nil, env|nil, workspace|nil, session_config_map, channels_list, lanes_list, semantic_memory|nil]
  OP_DEFINE_FORGE_AGENT,  // Stack: [name, provider, model, endpoint|nil, api_key_env|nil, temperature|nil, system|nil, skills_list, guardchains_list, policy|nil, budget|nil, env|nil, workspace|nil, loop_config_map, verify_fn, checkpoint|nil]

  // v0.8 Phase 1: Runtime operation opcodes
  OP_DEFINE_CHANNEL,    // Stack: [name, config_map] -> []
  OP_WORKSPACE_READ,    // Stack: [path] -> [content]
  OP_WORKSPACE_WRITE,   // Stack: [path, content] -> [bool]
  OP_MEMORY_SEARCH,     // Stack: [query, top_k] -> [results_list]
  OP_SPAWN_AGENT,       // Stack: [agent_name, config_map] -> [result]
  OP_FORGE_ITERATE,     // Start next forge loop iteration -> []
  OP_VERIFY,            // Run forge verify callback -> [VerifyResult]
  OP_COMPACT,           // Trigger claw session compaction -> []
  OP_FLUSH,             // Flush claw memory to workspace -> []
  OP_SESSION_HISTORY,   // Stack: [key, limit] -> [history_list]
  OP_FORGE_RUN,         // Stack: [agent_name, config_map] -> [LoopOutcome]

  // v0.9: Data agent opcodes
  OP_DEFINE_DATA_AGENT,   // Stack: [field_count × (key, value)] -> register in globals
  OP_DEFINE_SOURCE,       // Stack: [field_count × (key, value)] -> register in globals
  OP_DEFINE_SINK,         // Stack: [field_count × (key, value)] -> register in globals
  OP_DEFINE_SCHEMA,       // Stack: [field_count × (key, value)] -> register in globals
  OP_DEFINE_COMPUTE,      // Stack: [field_count × (key, value)] -> register in globals
  OP_DEFINE_QUALITY,      // Stack: [field_count × (key, value)] -> register in globals
  OP_DEFINE_GOVERNANCE,   // Stack: [field_count × (key, value)] -> register in globals
  OP_DEFINE_CATALOG,      // Stack: [field_count × (key, value)] -> register in globals

  // v0.9.1: NeamETL
  OP_DEFINE_ETL_AGENT,
  OP_DEFINE_MART,
  OP_DEFINE_SEMANTIC,
  OP_SQL_TRANSPILE,
  OP_SQL_PUSHDOWN,
  OP_NL2SQL,
  OP_AUTO_MODEL,
  OP_SELF_HEAL,

  // v0.9.2: Migration Agent opcodes
  OP_DEFINE_MIGRATION_AGENT,
  OP_MIGRATION_ASSESS,
  OP_MIGRATION_PLAN_WAVES,
  OP_MIGRATION_TRANSLATE_SCHEMA,
  OP_MIGRATION_MOVE_DATA,
  OP_MIGRATION_RECONCILE,
  OP_MIGRATION_CUTOVER,
  OP_MIGRATION_ROLLBACK,
  OP_MIGRATION_SELF_HEAL,

  // v0.9.3: DataOps Agent opcodes
  OP_DEFINE_DATAOPS_AGENT,
  OP_DEFINE_SCHEDULER,
  OP_DEFINE_AUDIT_TABLE,
  OP_DEFINE_LOG_SOURCE,
  OP_DEFINE_PLATFORM,
  OP_DEFINE_INCIDENT_POLICY,
  OP_DEFINE_CORRELATION,
  OP_DATAOPS_START_MONITOR,
  OP_DATAOPS_STOP_MONITOR,
  OP_DATAOPS_TRIAGE,
  OP_DATAOPS_INVESTIGATE,
  OP_DATAOPS_REMEDIATE,

  // v0.9.4: Governance Agent opcodes
  OP_DEFINE_GOVERNANCE_AGENT,
  OP_DEFINE_CATALOG_SOURCE,
  OP_DEFINE_GOV_CATALOG,
  OP_DEFINE_GLOSSARY,
  OP_DEFINE_CLASSIFICATION_POLICY,
  OP_DEFINE_ACCESS_POLICY,
  OP_DEFINE_QUALITY_POLICY,
  OP_DEFINE_LINEAGE_POLICY,
  OP_DEFINE_COMPLIANCE_POLICY,
  OP_DEFINE_LIFECYCLE_POLICY,
  OP_DEFINE_DATA_PRODUCT,
  OP_DEFINE_CONTRACT_POLICY,
  OP_DEFINE_MASTER_DATA,
  OP_DEFINE_EXTERNAL_TOOL,
  OP_GOVERNANCE_CLASSIFY,
  OP_GOVERNANCE_TRACE_LINEAGE,

  // v0.9.5: Modeling Agent opcodes
  OP_DEFINE_SCHEMA_SOURCE,
  OP_DEFINE_ER_MODEL,
  OP_DEFINE_ENTITY_MODEL,
  OP_DEFINE_DIMENSIONAL_MODEL,
  OP_DEFINE_DATAMART_V095,
  OP_DEFINE_NORM_ANALYSIS,
  OP_DEFINE_AMENDMENT_CONFIG,
  OP_DEFINE_AMENDMENT,
  OP_DEFINE_DATA_PROFILE,
  OP_DEFINE_MODELING_TOOL,
  OP_DEFINE_MODELING_AGENT,

  // v0.9.6: Analyst Agent opcodes
  OP_DEFINE_SQL_CONNECTION,
  OP_DEFINE_DOMAIN_CONTEXT,
  OP_DEFINE_QUERY_TEMPLATE,
  OP_DEFINE_QUERY_OPTIMIZER,
  OP_DEFINE_EXECUTION_POLICY,
  OP_DEFINE_OUTPUT_FORMAT,
  OP_DEFINE_QUERY_LIBRARY,
  OP_DEFINE_ANALYSIS_SCHEDULE,
  OP_DEFINE_ANALYST_AGENT,
  OP_ANALYST_QUERY,
  OP_ANALYST_EXECUTE,
  OP_ANALYST_FORMAT,
  OP_ANALYST_OPTIMIZE,

  // v0.9.7: Data Pipeline Deployment opcodes
  OP_DEFINE_DEPLOY_TARGET,
  OP_DEFINE_PROMOTION_RULE,
  OP_DEFINE_ROLLBACK_POLICY,
  OP_DEFINE_ARTIFACT_REGISTRY,
  OP_DEFINE_DEPLOY_CONFIG,
  OP_DEPLOY_EXECUTE,
  OP_DEPLOY_ROLLBACK,
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
