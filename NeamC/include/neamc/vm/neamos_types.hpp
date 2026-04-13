//
// Neam v1.1 — NeamOS Foundation Runtime Types
// Knowledge Cards, Context Assembly, Personas, Locale, Governance, Adapters, Blueprints
//

#pragma once

#include "neamc/vm/object.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>

namespace neamc::vm {

// ═══ Knowledge Card ═══
struct ObjKnowledgeCard : Obj {
    std::string name;
    std::string card_type;              // "concept"|"policy"|"decision"|"skill"
    std::string fields_json;            // type-specific fields as JSON
    std::string owner_agent_ref;
    std::string version;                // semver
    std::string ttl;                    // duration or empty
    std::string domain;                 // dot-path
    std::string provenance_json;
    // Runtime state
    mutable std::string status = "active";
    mutable int access_count = 0;
};

// ═══ Context Assembly ═══
struct ObjContextAssembly : Obj {
    std::string name;
    std::string target_agent_ref;
    std::string phase;
    std::string card_refs_json;         // serialized card reference list
    int max_context_tokens = 4000;
    std::string assembly_strategy;      // "relevance_ranked"|"chronological"|"priority"
    bool include_metadata = false;
    std::string fallback;               // "truncate"|"prioritize"|"error"
    // Runtime state
    mutable int assemblies_performed = 0;
    mutable int tokens_used_last = 0;
};

// ═══ Agent Persona ═══
struct ObjAgentPersona : Obj {
    std::string name;
    std::string target_agent_ref;
    std::string display_name;
    std::string avatar_path;
    std::string voice_json;             // {engine, voice_id}
    std::string personality_json;       // {trait: float}
    std::string catchphrase;
    std::string locales_json;           // {lang: {voice_id, catchphrases}}
    std::string animation_set;
};

// ═══ Locale Configuration ═══
struct ObjLocaleConfig : Obj {
    std::string name;
    std::string supported_json;         // ["en", "zh", "ms", ...]
    std::string rtl_json;               // ["ar", "he", ...]
    std::string fallback_locale;
    std::string string_table;           // path template
    // Runtime state
    mutable std::string active_locale = "en";
};

// ═══ Governance Rule ═══
struct ObjGovernanceRule : Obj {
    std::string name;
    std::string trigger;                // "data_write"|"data_read"|"tool_call"|"agent_spawn"|"config_change"|"budget_exceed"
    std::string condition_json;         // condition expression AST as JSON
    std::string action_json;            // {enforce_ttl, require_consent, notify, audit, block}
    std::string regulatory_basis;
    std::string effective_date;
    std::string review_date;
    // Runtime state
    mutable int evaluations = 0;
    mutable int triggers_fired = 0;
    mutable int actions_blocked = 0;
};

// ═══ Agent Adapter ═══
struct ObjAgentAdapter : Obj {
    std::string name;
    std::string adapter_type;           // "local_coding_agent"|"gateway_agent"|"cloud_agent"|"custom"
    std::string binary;
    std::string protocol;               // "stdin_stdout"|"rpc"|"sse"|"websocket"
    std::string endpoint;
    std::string capabilities_json;
    std::string communication;
    bool skill_injection = false;
    std::string cost_tracking;          // "token_based"|"api_based"|"time_based"|"none"
    bool device_key_pairing = false;
    // Runtime state
    mutable std::string connection_status = "disconnected";
    mutable double tracked_cost = 0.0;
};

// ═══ Blueprint ═══
struct ObjBlueprint : Obj {
    std::string name;
    std::string version;
    std::string description;
    std::string author;
    std::string agent_refs_json;        // serialized agent reference list
    std::string skill_refs_json;        // serialized skill reference list
    std::string card_refs_json;         // serialized card reference list
    std::string dio_spec_ref;
    std::string parameters_json;        // {param: {type, default, range}}
    // Runtime state
    mutable std::string install_status = "not_installed";
};

// ═══ KnowledgeWeaver Agent ═══
struct ObjKnowledgeWeaverAgent : Obj {
    std::string name;
    std::string provider;
    std::string llm_model;
    double temperature = 0.2;
    std::string budget_ref;
    std::string fabric_json;            // {card_store, embedding_model, search, certification}
    std::string monitors_json;          // {card_freshness, coverage_gaps, conflict_detection}
    mutable std::string status = "initialized";
    mutable int cards_indexed = 0;
    mutable int contexts_assembled = 0;
};

// ═══ AdaptAgent ═══
struct ObjAdaptAgent : Obj {
    std::string name;
    std::string provider;
    std::string llm_model;
    double temperature = 0.2;
    std::string budget_ref;
    std::string monitors_json;          // {model_releases, protocol_updates, security_advisories}
    std::string proposals_json;         // {channel, requires_approval}
    mutable std::string status = "initialized";
    mutable int proposals_generated = 0;
};

// ═══ Storyteller Agent ═══
struct ObjStorytellerAgent : Obj {
    std::string name;
    std::string provider;
    std::string llm_model;
    double temperature = 0.7;
    std::string budget_ref;
    std::string sub_agents_json;        // {scene_artist, voice_caster, mood_composer}
    std::string safety_json;            // {content_filter, age_groups, image_scan, parent_preview}
    mutable std::string status = "initialized";
    mutable int stories_generated = 0;
};

} // namespace neamc::vm
