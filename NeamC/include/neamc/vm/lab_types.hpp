//
// Neam v1.3 — NeamLab Runtime Types
// Auto Research Agent: program, research agent, experiment loop, metric extractor
//

#pragma once
#include "neamc/vm/object.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>

namespace neamc::vm {

struct ObjProgram : Obj {
    std::string name;
    std::string mission;
    std::string success_criterion;
    std::string constraints_json;       // serialized array
    std::string process_json;           // serialized array
    std::string autonomy;
    std::string halt_conditions_json;
    std::string style_preferences_json;
    std::string version;
    std::string metadata_json;
};

struct ObjResearchAgent : Obj {
    std::string name;
    std::string provider;
    std::string llm_model;
    double temperature = 0.2;
    std::string budget_ref;
    std::string program_ref;
    std::string mutable_artifacts_json;
    std::string immutable_artifacts_json;
    std::string metric_ref;
    std::string iteration_budget_json;
    std::string experiment_log_ref;
    std::string on_improvement;
    std::string on_failure;
    bool hypothesis_required = false;
    bool until_interrupted = false;
    double target_metric = 0.0;
    std::string eval_set_ref;
    std::string plugin_throttle;
    // Runtime state
    mutable std::string status = "initialized";
    mutable int current_iteration = 0;
    mutable double best_metric = 0.0;
    mutable double current_metric = 0.0;
    mutable std::unordered_map<std::string, std::string> immutable_checksums;
    mutable std::mutex state_mutex;
};

struct ObjExperimentLoop : Obj {
    std::string name;
    std::string research_agent_ref;
    std::string until_json;
    std::string on_iteration_json;
    std::string on_improvement_json;
    std::string on_failure_json;
    std::string notify_ref;
    mutable std::string status = "stopped";  // "stopped"|"running"|"paused"|"complete"
    mutable int total_iterations = 0;
    mutable int kept_iterations = 0;
    mutable int reverted_iterations = 0;
};

struct ObjMetricExtractor : Obj {
    std::string name;
    std::string direction;
    std::string method;
    std::string pattern;
    std::string path;
    std::string function_ref;
    int regex_group = 0;
    double tolerance = 0.0;
    std::string from_eval_ref;
    mutable int extractions_performed = 0;
};

// ═══ v1.4: NeamWiki Runtime Types ═══

struct ObjWiki : Obj {
    std::string name;
    std::string topic;
    std::string raw_path;
    std::string wiki_path;
    std::string description;
    std::string hub;
    std::string inbox_path;
    std::string output_path;
    std::string page_types_json;
    std::string naming_convention;
    std::string frontmatter_format;
    bool auto_compile = true;
    bool contradiction_detection = true;
    bool auto_link = true;
    bool auto_extract_entities = true;
    bool obsidian_compat = false;
    std::string vector_store;
    std::string embedding_model;
    std::string chunk_strategy;
    std::string sibling_wikis_json;
    std::string fields_json;
    // Runtime state
    mutable int page_count = 0;
    mutable int source_count = 0;
    mutable std::string status = "initialized";
};

struct ObjWikiAgent : Obj {
    std::string name;
    std::string provider;
    std::string llm_model;
    double temperature = 0.2;
    std::string budget_ref;
    std::string wikis_json;
    std::string operations_json;
    std::string research_config_json;
    std::string lint_policies_json;
    std::string graph_config_json;
    std::string output_formats_json;
    std::string governance_json;
    std::string plugin_hooks_json;
    std::string knowledge_cards_json;
    // Runtime state
    mutable std::string status = "initialized";
    mutable int operations_performed = 0;
    mutable int ingests_done = 0;
    mutable int queries_done = 0;
};

} // namespace neamc::vm
