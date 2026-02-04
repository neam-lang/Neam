// SPDX-License-Identifier: Apache-2.0
//
// Neam Virtual Machine - DynamoDB State Backend Implementation (v0.6.0)
//
// Single-table design with PK/SK patterns for all entity types.
// Uses libcurl REST API with placeholder AWS Signature V4 signing.
//

#ifdef NEAM_BACKEND_AWS

#include "neamc/vm/state_dynamodb.hpp"
#include "neamc/vm/autonomous.hpp"

#include <nlohmann/json.hpp>
#include <curl/curl.h>

#include <chrono>
#include <ctime>
#include <random>
#include <sstream>
#include <stdexcept>

using json = nlohmann::json;

namespace neamc::vm
{

// ---- helpers ----------------------------------------------------------------

namespace
{

std::string generate_uuid()
{
  static std::mt19937_64 rng(std::random_device{}());
  std::uniform_int_distribution<uint64_t> dist;
  uint64_t a = dist(rng);
  uint64_t b = dist(rng);
  char buf[40];
  std::snprintf(buf, sizeof(buf), "%08x-%04x-%04x-%04x-%012llx",
                static_cast<uint32_t>(a >> 32),
                static_cast<uint16_t>(a >> 16),
                static_cast<uint16_t>((a & 0xFFFF) | 0x4000),
                static_cast<uint16_t>((b >> 48) | 0x8000),
                static_cast<unsigned long long>(b & 0xFFFFFFFFFFFF));
  return buf;
}

std::string now_iso8601()
{
  auto tp = std::chrono::system_clock::now();
  std::time_t tt = std::chrono::system_clock::to_time_t(tp);
  struct tm tm_buf;
#ifdef _WIN32
  localtime_s(&tm_buf, &tt);
#else
  localtime_r(&tt, &tm_buf);
#endif
  char buf[32];
  std::strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tm_buf);
  return buf;
}

std::string today_date()
{
  auto tp = std::chrono::system_clock::now();
  std::time_t tt = std::chrono::system_clock::to_time_t(tp);
  struct tm tm_buf;
#ifdef _WIN32
  localtime_s(&tm_buf, &tt);
#else
  localtime_r(&tt, &tm_buf);
#endif
  char buf[16];
  std::strftime(buf, sizeof(buf), "%Y-%m-%d", &tm_buf);
  return buf;
}

json dynamo_s(const std::string& v) { return json{{"S", v}}; }
json dynamo_n(int64_t v) { return json{{"N", std::to_string(v)}}; }
json dynamo_n(double v) { return json{{"N", std::to_string(v)}}; }
json dynamo_n(size_t v) { return json{{"N", std::to_string(v)}}; }

std::string dynamo_get_s(const json& attr) { return attr.at("S").get<std::string>(); }
int64_t dynamo_get_n_i64(const json& attr) { return std::stoll(attr.at("N").get<std::string>()); }
double dynamo_get_n_dbl(const json& attr) { return std::stod(attr.at("N").get<std::string>()); }
size_t dynamo_get_n_sz(const json& attr)
{
  return static_cast<size_t>(std::stoull(attr.at("N").get<std::string>()));
}

size_t curl_write_cb(char* ptr, size_t size, size_t nmemb, void* userdata)
{
  auto* out = static_cast<std::string*>(userdata);
  out->append(ptr, size * nmemb);
  return size * nmemb;
}

}  // anonymous namespace

// ---- construction -----------------------------------------------------------

DynamoDBBackend::DynamoDBBackend(const std::string& table_name, const std::string& region)
    : table_name_(table_name),
      region_(region),
      endpoint_("https://dynamodb." + region + ".amazonaws.com")
{
  curl_global_init(CURL_GLOBAL_DEFAULT);
}

DynamoDBBackend::~DynamoDBBackend()
{
  curl_global_cleanup();
}

// ---- low-level DynamoDB HTTP ------------------------------------------------

std::string DynamoDBBackend::dynamo_request(const std::string& action, const std::string& body)
{
  CURL* curl = curl_easy_init();
  if (!curl)
  {
    throw std::runtime_error("DynamoDBBackend: failed to init curl");
  }

  std::string response_body;

  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/x-amz-json-1.0");

  std::string target_header = "X-Amz-Target: DynamoDB_20120810." + action;
  headers = curl_slist_append(headers, target_header.c_str());

  // TODO: Replace with full AWS Signature V4 from aws_signer module.
  // For now, rely on environment credentials (e.g. IAM role, instance profile,
  // or the AWS_ACCESS_KEY_ID / AWS_SECRET_ACCESS_KEY env vars picked up by
  // a future signing layer).
  headers = curl_slist_append(headers, "X-Amz-Date: 20260101T000000Z");
  headers = curl_slist_append(headers, "Authorization: AWS4-HMAC-SHA256 Credential=PLACEHOLDER");

  curl_easy_setopt(curl, CURLOPT_URL, endpoint_.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_body);
  curl_easy_setopt(curl, CURLOPT_TIMEOUT, 10L);

  CURLcode res = curl_easy_perform(curl);
  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  if (res != CURLE_OK)
  {
    throw std::runtime_error(std::string("DynamoDBBackend: curl error: ") +
                             curl_easy_strerror(res));
  }
  return response_body;
}

// ---- MemoryBackend: events --------------------------------------------------

void DynamoDBBackend::store_event(const std::string& store_name, const MemoryEventRecord& event)
{
  std::string ts = std::to_string(event.timestamp);
  std::string sk = ts + "#" + generate_uuid();

  json item = {
      {"PK", dynamo_s("EVENT#" + store_name)},
      {"SK", dynamo_s(sk)},
      {"ts", dynamo_n(event.timestamp)},
      {"type", dynamo_s(event.type)},
      {"data", dynamo_s(event.data)},
      {"agent", dynamo_s(event.agent)},
  };

  json req = {{"TableName", table_name_}, {"Item", item}};
  dynamo_request("PutItem", req.dump());
}

std::vector<MemoryEventRecord> DynamoDBBackend::load_events(const std::string& store_name)
{
  json req = {
      {"TableName", table_name_},
      {"KeyConditionExpression", "PK = :pk"},
      {"ExpressionAttributeValues", {{":pk", dynamo_s("EVENT#" + store_name)}}},
      {"ScanIndexForward", true},
  };

  auto resp = json::parse(dynamo_request("Query", req.dump()));
  std::vector<MemoryEventRecord> result;
  if (!resp.contains("Items"))
  {
    return result;
  }
  for (const auto& item : resp["Items"])
  {
    MemoryEventRecord r;
    r.timestamp = dynamo_get_n_i64(item.at("ts"));
    r.type = dynamo_get_s(item.at("type"));
    r.data = dynamo_get_s(item.at("data"));
    r.agent = dynamo_get_s(item.at("agent"));
    result.push_back(std::move(r));
  }
  return result;
}

void DynamoDBBackend::save_checkpoint(const std::string& store_name, const std::string& label,
                                       std::size_t position)
{
  json item = {
      {"PK", dynamo_s("CHECKPOINT#" + store_name)},
      {"SK", dynamo_s(label)},
      {"pos", dynamo_n(position)},
  };
  json req = {{"TableName", table_name_}, {"Item", item}};
  dynamo_request("PutItem", req.dump());
}

std::optional<std::size_t> DynamoDBBackend::load_checkpoint(const std::string& store_name,
                                                              const std::string& label)
{
  json key = {
      {"PK", dynamo_s("CHECKPOINT#" + store_name)},
      {"SK", dynamo_s(label)},
  };
  json req = {{"TableName", table_name_}, {"Key", key}};
  auto resp = json::parse(dynamo_request("GetItem", req.dump()));
  if (!resp.contains("Item"))
  {
    return std::nullopt;
  }
  return dynamo_get_n_sz(resp["Item"].at("pos"));
}

std::vector<MemoryEventRecord> DynamoDBBackend::search_events(const std::string& store_name,
                                                                const std::string& query,
                                                                std::size_t limit)
{
  // DynamoDB does not support full-text search natively; use a FilterExpression
  // with contains() on the data attribute as a pragmatic approximation.
  json req = {
      {"TableName", table_name_},
      {"KeyConditionExpression", "PK = :pk"},
      {"FilterExpression", "contains(#d, :q)"},
      {"ExpressionAttributeNames", {{"#d", "data"}}},
      {"ExpressionAttributeValues",
       {{":pk", dynamo_s("EVENT#" + store_name)}, {":q", dynamo_s(query)}}},
      {"Limit", static_cast<int>(limit)},
      {"ScanIndexForward", false},
  };

  auto resp = json::parse(dynamo_request("Query", req.dump()));
  std::vector<MemoryEventRecord> result;
  if (!resp.contains("Items"))
  {
    return result;
  }
  for (const auto& item : resp["Items"])
  {
    MemoryEventRecord r;
    r.timestamp = dynamo_get_n_i64(item.at("ts"));
    r.type = dynamo_get_s(item.at("type"));
    r.data = dynamo_get_s(item.at("data"));
    r.agent = dynamo_get_s(item.at("agent"));
    result.push_back(std::move(r));
  }
  return result;
}

void DynamoDBBackend::clear(const std::string& store_name)
{
  // Query all items under this partition, then batch-delete them.
  json req = {
      {"TableName", table_name_},
      {"KeyConditionExpression", "PK = :pk"},
      {"ExpressionAttributeValues", {{":pk", dynamo_s("EVENT#" + store_name)}}},
      {"ProjectionExpression", "PK, SK"},
  };

  auto resp = json::parse(dynamo_request("Query", req.dump()));
  if (!resp.contains("Items"))
  {
    return;
  }

  // Delete in batches of 25 (DynamoDB BatchWriteItem limit).
  json deletes = json::array();
  for (const auto& item : resp["Items"])
  {
    deletes.push_back({{"DeleteRequest", {{"Key", {{"PK", item["PK"]}, {"SK", item["SK"]}}}}}});
    if (deletes.size() == 25)
    {
      json batch = {{"RequestItems", {{table_name_, deletes}}}};
      dynamo_request("BatchWriteItem", batch.dump());
      deletes.clear();
    }
  }
  if (!deletes.empty())
  {
    json batch = {{"RequestItems", {{table_name_, deletes}}}};
    dynamo_request("BatchWriteItem", batch.dump());
  }
}

// ---- StateBackend: learning -------------------------------------------------

void DynamoDBBackend::store_learning_interaction(const LearningInteraction& interaction)
{
  std::string ts = std::to_string(interaction.timestamp);
  std::string sk = ts + "#" + generate_uuid();

  json item = {
      {"PK", dynamo_s("LEARNING#" + interaction.agent_name)},
      {"SK", dynamo_s(sk)},
      {"query", dynamo_s(interaction.query)},
      {"response", dynamo_s(interaction.response)},
      {"reflection_score", dynamo_n(interaction.reflection_score)},
      {"feedback_score", dynamo_n(interaction.feedback_score)},
      {"tokens_used", dynamo_n(interaction.tokens_used)},
      {"ts", dynamo_n(interaction.timestamp)},
  };
  json req = {{"TableName", table_name_}, {"Item", item}};
  dynamo_request("PutItem", req.dump());
}

std::vector<LearningInteraction> DynamoDBBackend::load_recent_interactions(
    const std::string& agent, size_t limit)
{
  json req = {
      {"TableName", table_name_},
      {"KeyConditionExpression", "PK = :pk"},
      {"ExpressionAttributeValues", {{":pk", dynamo_s("LEARNING#" + agent)}}},
      {"ScanIndexForward", false},
      {"Limit", static_cast<int>(limit)},
  };

  auto resp = json::parse(dynamo_request("Query", req.dump()));
  std::vector<LearningInteraction> result;
  if (!resp.contains("Items"))
  {
    return result;
  }
  for (const auto& item : resp["Items"])
  {
    LearningInteraction li;
    li.agent_name = agent;
    li.query = dynamo_get_s(item.at("query"));
    li.response = dynamo_get_s(item.at("response"));
    li.reflection_score = dynamo_get_n_dbl(item.at("reflection_score"));
    li.feedback_score = dynamo_get_n_dbl(item.at("feedback_score"));
    li.tokens_used = dynamo_get_n_sz(item.at("tokens_used"));
    li.timestamp = dynamo_get_n_i64(item.at("ts"));
    result.push_back(std::move(li));
  }
  return result;
}

void DynamoDBBackend::store_learning_review(const LearningReview& review)
{
  std::string ts = std::to_string(review.timestamp);
  std::string sk = ts + "#" + generate_uuid();

  json item = {
      {"PK", dynamo_s("REVIEW#" + review.agent_name)},
      {"SK", dynamo_s(sk)},
      {"strategy", dynamo_s(review.strategy)},
      {"interactions_reviewed", dynamo_n(review.interactions_reviewed)},
      {"avg_reflection_score", dynamo_n(review.avg_reflection_score)},
      {"lessons_json", dynamo_s(review.lessons_json)},
      {"prompt_addendum", dynamo_s(review.prompt_addendum)},
      {"ts", dynamo_n(review.timestamp)},
  };
  json req = {{"TableName", table_name_}, {"Item", item}};
  dynamo_request("PutItem", req.dump());
}

// ---- StateBackend: evolution ------------------------------------------------

void DynamoDBBackend::store_prompt_version(const PromptVersion& version)
{
  std::string sk = "V#" + std::to_string(version.version);
  json item = {
      {"PK", dynamo_s("EVOLUTION#" + version.agent_name)},
      {"SK", dynamo_s(sk)},
      {"version", dynamo_n(static_cast<int64_t>(version.version))},
      {"original_prompt", dynamo_s(version.original_prompt)},
      {"evolved_prompt", dynamo_s(version.evolved_prompt)},
      {"reasoning", dynamo_s(version.reasoning)},
      {"status", dynamo_s(version.status)},
      {"ts", dynamo_n(version.timestamp)},
  };
  json req = {{"TableName", table_name_}, {"Item", item}};
  dynamo_request("PutItem", req.dump());
}

std::vector<PromptVersion> DynamoDBBackend::load_prompt_history(const std::string& agent)
{
  json req = {
      {"TableName", table_name_},
      {"KeyConditionExpression", "PK = :pk"},
      {"ExpressionAttributeValues", {{":pk", dynamo_s("EVOLUTION#" + agent)}}},
      {"ScanIndexForward", true},
  };

  auto resp = json::parse(dynamo_request("Query", req.dump()));
  std::vector<PromptVersion> result;
  if (!resp.contains("Items"))
  {
    return result;
  }
  for (const auto& item : resp["Items"])
  {
    PromptVersion pv;
    pv.agent_name = agent;
    pv.version = static_cast<int>(dynamo_get_n_i64(item.at("version")));
    pv.original_prompt = dynamo_get_s(item.at("original_prompt"));
    pv.evolved_prompt = dynamo_get_s(item.at("evolved_prompt"));
    pv.reasoning = dynamo_get_s(item.at("reasoning"));
    pv.status = dynamo_get_s(item.at("status"));
    pv.timestamp = dynamo_get_n_i64(item.at("ts"));
    result.push_back(std::move(pv));
  }
  return result;
}

bool DynamoDBBackend::try_update_prompt(const std::string& agent, int expected_version,
                                         const PromptVersion& new_version)
{
  // Use a conditional PutItem: only succeed if the SK does not already exist,
  // guaranteeing optimistic concurrency on the version number.
  std::string sk = "V#" + std::to_string(new_version.version);
  json item = {
      {"PK", dynamo_s("EVOLUTION#" + agent)},
      {"SK", dynamo_s(sk)},
      {"version", dynamo_n(static_cast<int64_t>(new_version.version))},
      {"original_prompt", dynamo_s(new_version.original_prompt)},
      {"evolved_prompt", dynamo_s(new_version.evolved_prompt)},
      {"reasoning", dynamo_s(new_version.reasoning)},
      {"status", dynamo_s(new_version.status)},
      {"ts", dynamo_n(new_version.timestamp)},
  };

  json req = {
      {"TableName", table_name_},
      {"Item", item},
      {"ConditionExpression", "attribute_not_exists(SK)"},
  };

  try
  {
    auto resp_str = dynamo_request("PutItem", req.dump());
    auto resp = json::parse(resp_str);
    // If the response contains an error indicating condition failure, return false.
    if (resp.contains("__type") &&
        resp["__type"].get<std::string>().find("ConditionalCheckFailedException") !=
            std::string::npos)
    {
      return false;
    }
    return true;
  }
  catch (...)
  {
    return false;
  }
}

// ---- StateBackend: autonomous -----------------------------------------------

void DynamoDBBackend::store_autonomous_action(const AutonomousAction& action)
{
  std::string ts = std::to_string(action.timestamp);
  std::string sk = ts + "#" + generate_uuid();

  json item = {
      {"PK", dynamo_s("LEARNING#" + action.agent_name)},
      {"SK", dynamo_s(sk)},
      {"trigger_type", dynamo_s(action.trigger_type)},
      {"action_taken", dynamo_s(action.action_taken)},
      {"tokens_used", dynamo_n(action.tokens_used)},
      {"ts", dynamo_n(action.timestamp)},
  };
  json req = {{"TableName", table_name_}, {"Item", item}};
  dynamo_request("PutItem", req.dump());
}

DailyBudget DynamoDBBackend::load_budget(const std::string& agent, const std::string& date)
{
  json key = {
      {"PK", dynamo_s("BUDGET#" + agent)},
      {"SK", dynamo_s(date)},
  };
  json req = {{"TableName", table_name_}, {"Key", key}};
  auto resp = json::parse(dynamo_request("GetItem", req.dump()));

  DailyBudget budget;
  budget.date = date;
  if (!resp.contains("Item"))
  {
    return budget;
  }
  const auto& item = resp["Item"];
  budget.calls_used = dynamo_get_n_sz(item.at("calls_used"));
  budget.tokens_used = dynamo_get_n_sz(item.at("tokens_used"));
  budget.cost_used = dynamo_get_n_dbl(item.at("cost_used"));
  return budget;
}

void DynamoDBBackend::update_budget(const std::string& agent, const DailyBudget& budget)
{
  json item = {
      {"PK", dynamo_s("BUDGET#" + agent)},
      {"SK", dynamo_s(budget.date)},
      {"calls_used", dynamo_n(budget.calls_used)},
      {"tokens_used", dynamo_n(budget.tokens_used)},
      {"cost_used", dynamo_n(budget.cost_used)},
  };
  json req = {{"TableName", table_name_}, {"Item", item}};
  dynamo_request("PutItem", req.dump());
}

// ---- StateBackend: distributed locking --------------------------------------

bool DynamoDBBackend::try_acquire_lock(const std::string& lock_name,
                                        const std::string& holder_id,
                                        std::chrono::seconds ttl)
{
  auto now = std::chrono::system_clock::now();
  auto ttl_epoch =
      std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count() +
      ttl.count();

  json item = {
      {"PK", dynamo_s("LOCK#" + lock_name)},
      {"SK", dynamo_s("LOCK")},
      {"holder_id", dynamo_s(holder_id)},
      {"ttl", dynamo_n(static_cast<int64_t>(ttl_epoch))},
  };

  // Conditional put: succeed only if no lock exists or the existing TTL has
  // expired (i.e. the item does not exist, or its ttl is in the past).
  auto now_epoch =
      std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count();

  json req = {
      {"TableName", table_name_},
      {"Item", item},
      {"ConditionExpression", "attribute_not_exists(PK) OR #t < :now"},
      {"ExpressionAttributeNames", {{"#t", "ttl"}}},
      {"ExpressionAttributeValues", {{":now", dynamo_n(static_cast<int64_t>(now_epoch))}}},
  };

  try
  {
    auto resp_str = dynamo_request("PutItem", req.dump());
    auto resp = json::parse(resp_str);
    if (resp.contains("__type") &&
        resp["__type"].get<std::string>().find("ConditionalCheckFailedException") !=
            std::string::npos)
    {
      return false;
    }
    return true;
  }
  catch (...)
  {
    return false;
  }
}

void DynamoDBBackend::release_lock(const std::string& lock_name, const std::string& holder_id)
{
  json key = {
      {"PK", dynamo_s("LOCK#" + lock_name)},
      {"SK", dynamo_s("LOCK")},
  };

  // Only delete if the current holder matches, preventing accidental release
  // of locks owned by other processes.
  json req = {
      {"TableName", table_name_},
      {"Key", key},
      {"ConditionExpression", "holder_id = :hid"},
      {"ExpressionAttributeValues", {{":hid", dynamo_s(holder_id)}}},
  };

  try
  {
    dynamo_request("DeleteItem", req.dump());
  }
  catch (...)
  {
    // Swallow: if the lock was already released or held by another holder,
    // there is nothing to do.
  }
}

bool DynamoDBBackend::renew_lock(const std::string& lock_name, const std::string& holder_id,
                                  std::chrono::seconds ttl)
{
  auto now = std::chrono::system_clock::now();
  auto ttl_epoch =
      std::chrono::duration_cast<std::chrono::seconds>(now.time_since_epoch()).count() +
      ttl.count();

  json key = {
      {"PK", dynamo_s("LOCK#" + lock_name)},
      {"SK", dynamo_s("LOCK")},
  };

  json req = {
      {"TableName", table_name_},
      {"Key", key},
      {"UpdateExpression", "SET #t = :newttl"},
      {"ConditionExpression", "holder_id = :hid"},
      {"ExpressionAttributeNames", {{"#t", "ttl"}}},
      {"ExpressionAttributeValues",
       {{":hid", dynamo_s(holder_id)},
        {":newttl", dynamo_n(static_cast<int64_t>(ttl_epoch))}}},
  };

  try
  {
    auto resp_str = dynamo_request("UpdateItem", req.dump());
    auto resp = json::parse(resp_str);
    if (resp.contains("__type") &&
        resp["__type"].get<std::string>().find("ConditionalCheckFailedException") !=
            std::string::npos)
    {
      return false;
    }
    return true;
  }
  catch (...)
  {
    return false;
  }
}

}  // namespace neamc::vm

#endif  // NEAM_BACKEND_AWS
