//
// Neam v0.9 — Data Agent runtime types
//

#pragma once

#include <string>
#include <unordered_map>
#include <vector>

#include "neamc/vm/object.hpp"

namespace neamc::vm
{

struct ObjSchema : Obj
{
  ObjString* name{nullptr};
  ObjMap* fields{nullptr};    // field definitions as map
  int version{1};
};

struct ObjSource : Obj
{
  ObjString* name{nullptr};
  ObjString* source_type{nullptr};  // "postgres", "s3", etc.
  ObjString* connection{nullptr};
  ObjString* format{nullptr};
  ObjString* refresh{nullptr};
  ObjString* schema_ref{nullptr};
  ObjString* classification{nullptr};
  ObjString* mode{nullptr};
  ObjList* partition_by{nullptr};
};

struct ObjSink : Obj
{
  ObjString* name{nullptr};
  ObjString* sink_type{nullptr};
  ObjString* connection{nullptr};
  ObjString* format{nullptr};
  ObjString* write_mode{nullptr};
  int batch_size{0};
  ObjString* schema_ref{nullptr};
  ObjString* compute_ref{nullptr};
};

struct ObjQualityGate : Obj
{
  ObjString* name{nullptr};
  ObjString* freshness{nullptr};
  double completeness{-1.0};  // -1 means unset
  ObjList* uniqueness{nullptr};
  bool drift_detection{false};
  double anomaly_threshold{0.0};
  ObjString* on_violation{nullptr};
};

struct ObjComputeEngine : Obj
{
  ObjString* name{nullptr};
  ObjString* engine{nullptr};  // "spark", "snowflake", etc.
  ObjMap* config{nullptr};
};

struct ObjGovernancePolicy : Obj
{
  ObjString* name{nullptr};
  ObjMap* body{nullptr};  // full governance config as map
};

struct ObjCatalog : Obj
{
  ObjString* name{nullptr};
  ObjString* engine{nullptr};
  ObjMap* register_opts{nullptr};
  bool discovery{false};
};

struct ObjDataAgent : Obj
{
  // Base agent fields
  ObjString* name{nullptr};
  ObjString* provider{nullptr};
  ObjString* model{nullptr};
  ObjString* endpoint{nullptr};
  ObjString* api_key_env{nullptr};
  ObjString* system{nullptr};
  double temperature{0.0};
  ObjList* skills{nullptr};
  ObjList* guardchains{nullptr};
  ObjContext* context{nullptr};
  // Data agent specific
  ObjList* sources{nullptr};
  ObjList* sinks{nullptr};
  ObjString* schema_ref{nullptr};
  ObjString* quality_ref{nullptr};
  ObjMap* compute_config{nullptr};
  ObjString* governance_ref{nullptr};
  ObjString* catalog_ref{nullptr};
  bool lineage{false};
  ObjString* role{nullptr};
  ObjString* purpose{nullptr};
  ObjString* autonomy{nullptr};
  ObjMap* pipeline_config{nullptr};
  ObjString* agent_md_path{nullptr};
  // Agent.MD runtime state
  std::string system_prompt;
  std::vector<std::vector<std::string>> data_dictionary;
  std::vector<std::string> quality_rules;
  std::string access_patterns;
};

}  // namespace neamc::vm
