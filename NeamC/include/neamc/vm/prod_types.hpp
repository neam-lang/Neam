//
// Neam v1.2 — NeamProd Enterprise Production Runtime Types
// Plugin, Session, Eval, Artifact, Stream, A2A
//

#pragma once

#include "neamc/vm/object.hpp"
#include <string>
#include <vector>
#include <unordered_map>
#include <mutex>

namespace neamc::vm {

struct ObjPlugin : Obj {
    std::string name;
    std::string hooks_json;
    int priority = 100;
    bool enabled = true;
    std::string description;
    std::string scope;
    std::string config_json;
    mutable int invocations = 0;
};

struct ObjSessionService : Obj {
    std::string name;
    std::string backend;
    std::string connection_json;
    std::string scoping_json;
    std::string compression_json;
    std::string ttl;
    mutable std::string status = "initialized";
    mutable int sessions_active = 0;
};

struct ObjEvalTest : Obj {
    std::string name;
    std::string agent_ref;
    std::string input;
    std::string expected_tools_json;
    std::string expected_response;
    std::string criteria_json;
    std::string trajectory_mode;
    double threshold = 0.8;
    mutable std::string last_result = "not_run";
    mutable double last_score = 0.0;
};

struct ObjEvalSet : Obj {
    std::string name;
    std::string test_refs_json;
    std::string judge_json;
    std::string thresholds_json;
    std::string output_format;
    std::string output_path;
    mutable int total_runs = 0;
    mutable int passed_runs = 0;
    mutable double avg_score = 0.0;
};

struct ObjArtifactStore : Obj {
    std::string name;
    std::string backend;
    std::string path;
    std::string scoping;
    std::string metadata_json;
    mutable int artifacts_stored = 0;
    mutable int total_versions = 0;
};

struct ObjStreamConfig : Obj {
    std::string name;
    std::string mode;
    std::string voice_json;
    bool interrupt_enabled = true;
    std::string event_format;
    mutable bool streaming_active = false;
};

struct ObjA2AConfig : Obj {
    std::string name;
    std::string agent_card_json;
    std::string auth_json;
    std::string protocols_json;
    bool expose_as_server = false;
    bool discover_peers = false;
    mutable std::string connection_status = "disconnected";
};

} // namespace neamc::vm
