// SPDX-License-Identifier: Apache-2.0
//
// Neam Virtual Machine - Redis State Backend (v0.6.0)
//

#ifdef NEAM_BACKEND_REDIS

#include "neamc/vm/state_redis.hpp"

#include "neamc/vm/autonomous.hpp"

#include <algorithm>
#include <stdexcept>
#include <string>

#include <hiredis/hiredis.h>
#include <nlohmann/json.hpp>

namespace neamc::vm
{
namespace
{

struct ReplyGuard
{
  redisReply* reply{nullptr};
  ReplyGuard(void* r) : reply(static_cast<redisReply*>(r)) {}
  ~ReplyGuard()
  {
    if (reply)
    {
      freeReplyObject(reply);
    }
  }
  operator bool() const { return reply != nullptr; }
  redisReply* operator->() const { return reply; }
  redisReply* get() const { return reply; }
};

void parse_connection_string(const std::string& connection_string,
                             std::string& host, int& port)
{
  std::string s = connection_string;

  // Strip redis:// prefix if present
  const std::string prefix = "redis://";
  if (s.size() >= prefix.size() && s.substr(0, prefix.size()) == prefix)
  {
    s = s.substr(prefix.size());
  }

  // Strip trailing slash
  if (!s.empty() && s.back() == '/')
  {
    s.pop_back();
  }

  auto colon = s.rfind(':');
  if (colon != std::string::npos)
  {
    host = s.substr(0, colon);
    port = std::stoi(s.substr(colon + 1));
  }
  else
  {
    host = s;
    port = 6379;
  }

  if (host.empty())
  {
    host = "127.0.0.1";
  }
}

redisReply* exec_cmd(redisContext* conn, const char* fmt, ...)
{
  va_list ap;
  va_start(ap, fmt);
  auto* reply = static_cast<redisReply*>(redisvCommand(conn, fmt, ap));
  va_end(ap);
  if (!reply)
  {
    throw std::runtime_error(std::string("Redis command failed: ") +
                             (conn->errstr ? conn->errstr : "unknown error"));
  }
  if (reply->type == REDIS_REPLY_ERROR)
  {
    std::string msg(reply->str, reply->len);
    freeReplyObject(reply);
    throw std::runtime_error("Redis error: " + msg);
  }
  return reply;
}

nlohmann::json event_to_json(const MemoryEventRecord& event)
{
  return nlohmann::json{
      {"timestamp", event.timestamp},
      {"type", event.type},
      {"data", event.data},
      {"agent", event.agent}};
}

MemoryEventRecord event_from_json(const nlohmann::json& j)
{
  MemoryEventRecord e;
  e.timestamp = j.value("timestamp", int64_t{0});
  e.type = j.value("type", "");
  e.data = j.value("data", "");
  e.agent = j.value("agent", "");
  return e;
}

}  // namespace

RedisBackend::RedisBackend(const std::string& connection_string)
{
  std::string host;
  int port = 6379;
  parse_connection_string(connection_string, host, port);

  conn_ = redisConnect(host.c_str(), port);
  if (!conn_)
  {
    throw std::runtime_error("Redis: failed to allocate context");
  }
  if (conn_->err)
  {
    std::string msg = conn_->errstr;
    redisFree(conn_);
    conn_ = nullptr;
    throw std::runtime_error("Redis connection failed: " + msg);
  }
}

RedisBackend::~RedisBackend()
{
  if (conn_)
  {
    redisFree(conn_);
  }
}

// --- MemoryBackend interface ---

void RedisBackend::store_event(const std::string& store_name,
                               const MemoryEventRecord& event)
{
  const std::string key = "neam:events:" + store_name;
  std::string payload = event_to_json(event).dump();
  ReplyGuard r(exec_cmd(conn_, "ZADD %s %lld %s",
                         key.c_str(),
                         static_cast<long long>(event.timestamp),
                         payload.c_str()));
}

std::vector<MemoryEventRecord> RedisBackend::load_events(const std::string& store_name)
{
  const std::string key = "neam:events:" + store_name;
  ReplyGuard r(exec_cmd(conn_, "ZRANGEBYSCORE %s -inf +inf", key.c_str()));

  std::vector<MemoryEventRecord> events;
  if (r->type == REDIS_REPLY_ARRAY)
  {
    events.reserve(r->elements);
    for (size_t i = 0; i < r->elements; ++i)
    {
      auto* elem = r->element[i];
      if (elem->type == REDIS_REPLY_STRING)
      {
        auto j = nlohmann::json::parse(std::string(elem->str, elem->len),
                                        nullptr, false);
        if (!j.is_discarded())
        {
          events.push_back(event_from_json(j));
        }
      }
    }
  }
  return events;
}

void RedisBackend::save_checkpoint(const std::string& store_name,
                                   const std::string& label,
                                   std::size_t position)
{
  const std::string key = "neam:checkpoints:" + store_name;
  ReplyGuard r(exec_cmd(conn_, "HSET %s %s %lld",
                         key.c_str(), label.c_str(),
                         static_cast<long long>(position)));
}

std::optional<std::size_t> RedisBackend::load_checkpoint(const std::string& store_name,
                                                          const std::string& label)
{
  const std::string key = "neam:checkpoints:" + store_name;
  ReplyGuard r(exec_cmd(conn_, "HGET %s %s", key.c_str(), label.c_str()));

  if (r->type == REDIS_REPLY_STRING && r->str)
  {
    return static_cast<std::size_t>(std::stoll(std::string(r->str, r->len)));
  }
  return std::nullopt;
}

std::vector<MemoryEventRecord> RedisBackend::search_events(const std::string& store_name,
                                                            const std::string& query,
                                                            std::size_t limit)
{
  // SCAN all members of the sorted set and substring-match
  const std::string key = "neam:events:" + store_name;
  std::vector<MemoryEventRecord> results;
  unsigned long long cursor = 0;

  do
  {
    ReplyGuard r(exec_cmd(conn_, "ZSCAN %s %llu COUNT 100",
                           key.c_str(), cursor));

    if (r->type != REDIS_REPLY_ARRAY || r->elements < 2)
    {
      break;
    }

    cursor = std::stoull(std::string(r->element[0]->str, r->element[0]->len));
    auto* arr = r->element[1];

    // ZSCAN returns [member, score, member, score, ...]
    for (size_t i = 0; i + 1 < arr->elements; i += 2)
    {
      auto* elem = arr->element[i];
      std::string payload(elem->str, elem->len);

      if (payload.find(query) != std::string::npos)
      {
        auto j = nlohmann::json::parse(payload, nullptr, false);
        if (!j.is_discarded())
        {
          results.push_back(event_from_json(j));
          if (results.size() >= limit)
          {
            return results;
          }
        }
      }
    }
  } while (cursor != 0);

  return results;
}

void RedisBackend::clear(const std::string& store_name)
{
  const std::string events_key = "neam:events:" + store_name;
  const std::string ckpt_key = "neam:checkpoints:" + store_name;
  ReplyGuard r1(exec_cmd(conn_, "DEL %s", events_key.c_str()));
  ReplyGuard r2(exec_cmd(conn_, "DEL %s", ckpt_key.c_str()));
}

// --- Learning ---

void RedisBackend::store_learning_interaction(const LearningInteraction& interaction)
{
  const std::string key = "neam:learning:" + interaction.agent_name;
  nlohmann::json j{
      {"agent_name", interaction.agent_name},
      {"query", interaction.query},
      {"response", interaction.response},
      {"reflection_score", interaction.reflection_score},
      {"feedback_score", interaction.feedback_score},
      {"tokens_used", interaction.tokens_used},
      {"timestamp", interaction.timestamp}};
  std::string payload = j.dump();
  ReplyGuard r(exec_cmd(conn_, "ZADD %s %lld %s",
                         key.c_str(),
                         static_cast<long long>(interaction.timestamp),
                         payload.c_str()));
}

std::vector<LearningInteraction> RedisBackend::load_recent_interactions(
    const std::string& agent, size_t limit)
{
  const std::string key = "neam:learning:" + agent;
  ReplyGuard r(exec_cmd(conn_, "ZREVRANGEBYSCORE %s +inf -inf LIMIT 0 %llu",
                         key.c_str(),
                         static_cast<unsigned long long>(limit)));

  std::vector<LearningInteraction> results;
  if (r->type == REDIS_REPLY_ARRAY)
  {
    results.reserve(r->elements);
    for (size_t i = 0; i < r->elements; ++i)
    {
      auto* elem = r->element[i];
      if (elem->type != REDIS_REPLY_STRING) continue;
      auto j = nlohmann::json::parse(std::string(elem->str, elem->len),
                                      nullptr, false);
      if (j.is_discarded()) continue;

      LearningInteraction li;
      li.agent_name = j.value("agent_name", "");
      li.query = j.value("query", "");
      li.response = j.value("response", "");
      li.reflection_score = j.value("reflection_score", 0.0);
      li.feedback_score = j.value("feedback_score", 0.0);
      li.tokens_used = j.value("tokens_used", size_t{0});
      li.timestamp = j.value("timestamp", int64_t{0});
      results.push_back(std::move(li));
    }
  }
  return results;
}

void RedisBackend::store_learning_review(const LearningReview& review)
{
  const std::string key = "neam:learning_reviews:" + review.agent_name;
  nlohmann::json j{
      {"agent_name", review.agent_name},
      {"strategy", review.strategy},
      {"interactions_reviewed", review.interactions_reviewed},
      {"avg_reflection_score", review.avg_reflection_score},
      {"lessons_json", review.lessons_json},
      {"prompt_addendum", review.prompt_addendum},
      {"timestamp", review.timestamp}};
  std::string payload = j.dump();
  ReplyGuard r(exec_cmd(conn_, "ZADD %s %lld %s",
                         key.c_str(),
                         static_cast<long long>(review.timestamp),
                         payload.c_str()));
}

// --- Evolution ---

void RedisBackend::store_prompt_version(const PromptVersion& version)
{
  const std::string key = "neam:evolution:" + version.agent_name + ":" +
                          std::to_string(version.version);
  nlohmann::json j{
      {"agent_name", version.agent_name},
      {"version", version.version},
      {"original_prompt", version.original_prompt},
      {"evolved_prompt", version.evolved_prompt},
      {"reasoning", version.reasoning},
      {"status", version.status},
      {"timestamp", version.timestamp}};
  std::string payload = j.dump();

  ReplyGuard r(exec_cmd(conn_, "HSET %s data %s", key.c_str(), payload.c_str()));

  // Also track versions in a sorted set for ordering
  const std::string idx_key = "neam:evolution_idx:" + version.agent_name;
  ReplyGuard r2(exec_cmd(conn_, "ZADD %s %d %d",
                          idx_key.c_str(), version.version, version.version));
}

std::vector<PromptVersion> RedisBackend::load_prompt_history(const std::string& agent)
{
  const std::string idx_key = "neam:evolution_idx:" + agent;
  ReplyGuard r(exec_cmd(conn_, "ZRANGEBYSCORE %s -inf +inf", idx_key.c_str()));

  std::vector<PromptVersion> results;
  if (r->type != REDIS_REPLY_ARRAY) return results;

  for (size_t i = 0; i < r->elements; ++i)
  {
    auto* elem = r->element[i];
    if (elem->type != REDIS_REPLY_STRING) continue;
    int ver = std::stoi(std::string(elem->str, elem->len));

    const std::string key = "neam:evolution:" + agent + ":" + std::to_string(ver);
    ReplyGuard rh(exec_cmd(conn_, "HGET %s data", key.c_str()));
    if (rh->type != REDIS_REPLY_STRING || !rh->str) continue;

    auto j = nlohmann::json::parse(std::string(rh->str, rh->len),
                                    nullptr, false);
    if (j.is_discarded()) continue;

    PromptVersion pv;
    pv.agent_name = j.value("agent_name", "");
    pv.version = j.value("version", 0);
    pv.original_prompt = j.value("original_prompt", "");
    pv.evolved_prompt = j.value("evolved_prompt", "");
    pv.reasoning = j.value("reasoning", "");
    pv.status = j.value("status", "active");
    pv.timestamp = j.value("timestamp", int64_t{0});
    results.push_back(std::move(pv));
  }
  return results;
}

bool RedisBackend::try_update_prompt(const std::string& agent, int expected_version,
                                     const PromptVersion& new_version)
{
  // Use WATCH for optimistic locking
  const std::string key = "neam:evolution:" + agent + ":" +
                          std::to_string(expected_version);
  ReplyGuard rw(exec_cmd(conn_, "WATCH %s", key.c_str()));

  // Verify expected version exists
  ReplyGuard rh(exec_cmd(conn_, "HGET %s data", key.c_str()));
  if (rh->type != REDIS_REPLY_STRING || !rh->str)
  {
    exec_cmd(conn_, "UNWATCH");
    return false;
  }

  ReplyGuard rm(exec_cmd(conn_, "MULTI"));
  store_prompt_version(new_version);
  ReplyGuard re(exec_cmd(conn_, "EXEC"));

  // EXEC returns nil array if WATCH detected a change
  return re->type == REDIS_REPLY_ARRAY;
}

// --- Autonomous ---

void RedisBackend::store_autonomous_action(const AutonomousAction& action)
{
  const std::string key = "neam:actions:" + action.agent_name;
  nlohmann::json j{
      {"agent_name", action.agent_name},
      {"trigger_type", action.trigger_type},
      {"action_taken", action.action_taken},
      {"tokens_used", action.tokens_used},
      {"timestamp", action.timestamp}};
  std::string payload = j.dump();
  ReplyGuard r(exec_cmd(conn_, "ZADD %s %lld %s",
                         key.c_str(),
                         static_cast<long long>(action.timestamp),
                         payload.c_str()));
}

DailyBudget RedisBackend::load_budget(const std::string& agent,
                                       const std::string& date)
{
  const std::string key = "neam:budget:" + agent + ":" + date;
  ReplyGuard r(exec_cmd(conn_, "HGETALL %s", key.c_str()));

  DailyBudget budget;
  budget.date = date;

  if (r->type == REDIS_REPLY_ARRAY && r->elements >= 2)
  {
    for (size_t i = 0; i + 1 < r->elements; i += 2)
    {
      std::string field(r->element[i]->str, r->element[i]->len);
      std::string value(r->element[i + 1]->str, r->element[i + 1]->len);

      if (field == "calls_used") budget.calls_used = std::stoull(value);
      else if (field == "tokens_used") budget.tokens_used = std::stoull(value);
      else if (field == "cost_used") budget.cost_used = std::stod(value);
    }
  }
  return budget;
}

void RedisBackend::update_budget(const std::string& agent,
                                  const DailyBudget& budget)
{
  const std::string key = "neam:budget:" + agent + ":" + budget.date;
  ReplyGuard r(exec_cmd(conn_, "HSET %s calls_used %llu tokens_used %llu cost_used %s",
                         key.c_str(),
                         static_cast<unsigned long long>(budget.calls_used),
                         static_cast<unsigned long long>(budget.tokens_used),
                         std::to_string(budget.cost_used).c_str()));
}

// --- Distributed Locking ---

bool RedisBackend::try_acquire_lock(const std::string& lock_name,
                                    const std::string& holder_id,
                                    std::chrono::seconds ttl)
{
  const std::string key = "neam:lock:" + lock_name;
  ReplyGuard r(exec_cmd(conn_, "SET %s %s EX %lld NX",
                         key.c_str(), holder_id.c_str(),
                         static_cast<long long>(ttl.count())));
  // SET ... NX returns OK on success, nil on failure
  return r->type == REDIS_REPLY_STATUS && r->str &&
         std::string(r->str, r->len) == "OK";
}

void RedisBackend::release_lock(const std::string& lock_name,
                                const std::string& holder_id)
{
  // Only release if we hold the lock (Lua script for atomicity)
  const char* lua = R"(
    if redis.call('GET', KEYS[1]) == ARGV[1] then
      return redis.call('DEL', KEYS[1])
    else
      return 0
    end
  )";
  const std::string key = "neam:lock:" + lock_name;
  ReplyGuard r(exec_cmd(conn_, "EVAL %s 1 %s %s",
                         lua, key.c_str(), holder_id.c_str()));
}

bool RedisBackend::renew_lock(const std::string& lock_name,
                              const std::string& holder_id,
                              std::chrono::seconds ttl)
{
  // Only renew if we hold the lock (Lua script for atomicity)
  const char* lua = R"(
    if redis.call('GET', KEYS[1]) == ARGV[1] then
      return redis.call('EXPIRE', KEYS[1], ARGV[2])
    else
      return 0
    end
  )";
  const std::string key = "neam:lock:" + lock_name;
  std::string ttl_str = std::to_string(ttl.count());
  ReplyGuard r(exec_cmd(conn_, "EVAL %s 1 %s %s %s",
                         lua, key.c_str(), holder_id.c_str(),
                         ttl_str.c_str()));
  return r->type == REDIS_REPLY_INTEGER && r->integer == 1;
}

}  // namespace neamc::vm

#endif  // NEAM_BACKEND_REDIS
