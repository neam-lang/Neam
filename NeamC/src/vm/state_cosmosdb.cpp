// SPDX-License-Identifier: Apache-2.0
//
// Neam Virtual Machine - CosmosDB State Backend (v0.6.0)
//

#ifdef NEAM_BACKEND_AZURE

#include "neamc/vm/state_cosmosdb.hpp"
#include "neamc/vm/autonomous.hpp"

#include <chrono>
#include <cstdlib>
#include <stdexcept>
#include <string>

#include <curl/curl.h>
#include <nlohmann/json.hpp>

namespace neamc::vm
{

// ---------------------------------------------------------------------------
// Helpers
// ---------------------------------------------------------------------------

static std::string get_auth_token()
{
  const char* key = std::getenv("AZURE_COSMOS_KEY");
  if (key && key[0] != '\0')
  {
    return std::string(key);
  }
  // TODO: implement managed-identity token acquisition via IMDS
  throw std::runtime_error("CosmosDB: AZURE_COSMOS_KEY not set and managed identity not yet implemented");
}

static std::string make_id(const std::string& type, const std::string& partition,
                           const std::string& suffix)
{
  return type + "|" + partition + "|" + suffix;
}

static int64_t epoch_millis()
{
  using namespace std::chrono;
  return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

static size_t curl_write_cb(char* ptr, size_t size, size_t nmemb, void* userdata)
{
  auto* buf = static_cast<std::string*>(userdata);
  buf->append(ptr, size * nmemb);
  return size * nmemb;
}

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------

CosmosDBBackend::CosmosDBBackend(const std::string& endpoint,
                                 const std::string& database,
                                 const std::string& container)
    : endpoint_(endpoint), database_(database), container_(container)
{
  curl_global_init(CURL_GLOBAL_DEFAULT);
}

CosmosDBBackend::~CosmosDBBackend()
{
  curl_global_cleanup();
}

// ---------------------------------------------------------------------------
// Low-level REST helper
// ---------------------------------------------------------------------------

std::string CosmosDBBackend::cosmos_request(const std::string& method,
                                            const std::string& path,
                                            const std::string& body,
                                            const std::string& partition_key)
{
  const std::string url = endpoint_ + path;
  const std::string token = get_auth_token();

  CURL* curl = curl_easy_init();
  if (!curl)
  {
    throw std::runtime_error("CosmosDB: curl_easy_init failed");
  }

  std::string response_buf;

  struct curl_slist* headers = nullptr;
  headers = curl_slist_append(headers, "Content-Type: application/json");
  headers = curl_slist_append(headers, ("Authorization: type%3Dmaster%26ver%3D1.0%26sig%3D" + token).c_str());
  headers = curl_slist_append(headers, "x-ms-version: 2018-12-31");
  if (!partition_key.empty())
  {
    headers = curl_slist_append(headers,
                                ("x-ms-documentdb-partitionkey: [\"" + partition_key + "\"]").c_str());
  }
  // For query requests
  if (method == "POST" && path.find("/docs") != std::string::npos &&
      body.find("\"query\"") != std::string::npos)
  {
    headers = curl_slist_append(headers, "x-ms-documentdb-isquery: True");
    headers = curl_slist_append(headers, "Content-Type: application/query+json");
  }

  curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
  curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
  curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, curl_write_cb);
  curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response_buf);
  curl_easy_setopt(curl, CURLOPT_CUSTOMREQUEST, method.c_str());

  if (!body.empty())
  {
    curl_easy_setopt(curl, CURLOPT_POSTFIELDS, body.c_str());
  }

  CURLcode res = curl_easy_perform(curl);
  long http_code = 0;
  curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);

  curl_slist_free_all(headers);
  curl_easy_cleanup(curl);

  if (res != CURLE_OK)
  {
    throw std::runtime_error(std::string("CosmosDB request failed: ") + curl_easy_strerror(res));
  }
  if (http_code >= 400 && http_code != 404 && http_code != 409)
  {
    throw std::runtime_error("CosmosDB HTTP " + std::to_string(http_code) + ": " + response_buf);
  }

  return response_buf;
}

// ---------------------------------------------------------------------------
// Convenience: collection path, upsert, query
// ---------------------------------------------------------------------------

static std::string coll_path(const std::string& db, const std::string& coll)
{
  return "/dbs/" + db + "/colls/" + coll;
}

// ---------------------------------------------------------------------------
// MemoryBackend interface
// ---------------------------------------------------------------------------

void CosmosDBBackend::store_event(const std::string& store_name,
                                  const MemoryEventRecord& event)
{
  nlohmann::json doc;
  doc["id"] = make_id("event", store_name, std::to_string(epoch_millis()));
  doc["type"] = "event";
  doc["agent_name"] = store_name;
  doc["timestamp"] = event.timestamp;
  doc["event_type"] = event.type;
  doc["data"] = event.data;
  doc["agent"] = event.agent;

  cosmos_request("POST", coll_path(database_, container_) + "/docs",
                 doc.dump(), store_name);
}

std::vector<MemoryEventRecord> CosmosDBBackend::load_events(const std::string& store_name)
{
  nlohmann::json qbody;
  qbody["query"] = "SELECT * FROM c WHERE c.type = 'event' AND c.agent_name = @sn ORDER BY c.timestamp ASC";
  qbody["parameters"] = {{{"name", "@sn"}, {"value", store_name}}};

  auto resp = cosmos_request("POST", coll_path(database_, container_) + "/docs",
                             qbody.dump(), store_name);
  auto j = nlohmann::json::parse(resp);

  std::vector<MemoryEventRecord> result;
  if (j.contains("Documents"))
  {
    for (auto& d : j["Documents"])
    {
      MemoryEventRecord r;
      r.timestamp = d.value("timestamp", int64_t{0});
      r.type = d.value("event_type", "");
      r.data = d.value("data", "");
      r.agent = d.value("agent", "");
      result.push_back(std::move(r));
    }
  }
  return result;
}

void CosmosDBBackend::save_checkpoint(const std::string& store_name,
                                      const std::string& label,
                                      std::size_t position)
{
  nlohmann::json doc;
  doc["id"] = make_id("checkpoint", store_name, label);
  doc["type"] = "checkpoint";
  doc["agent_name"] = store_name;
  doc["label"] = label;
  doc["position"] = position;

  // Upsert via x-ms-documentdb-is-upsert header (handled by POST with unique id)
  cosmos_request("POST", coll_path(database_, container_) + "/docs",
                 doc.dump(), store_name);
}

std::optional<std::size_t> CosmosDBBackend::load_checkpoint(const std::string& store_name,
                                                            const std::string& label)
{
  const std::string doc_id = make_id("checkpoint", store_name, label);
  auto resp = cosmos_request("GET",
                             coll_path(database_, container_) + "/docs/" + doc_id,
                             "", store_name);
  if (resp.empty())
  {
    return std::nullopt;
  }
  auto j = nlohmann::json::parse(resp, nullptr, false);
  if (j.is_discarded() || !j.contains("position"))
  {
    return std::nullopt;
  }
  return static_cast<std::size_t>(j["position"].get<uint64_t>());
}

std::vector<MemoryEventRecord> CosmosDBBackend::search_events(const std::string& store_name,
                                                              const std::string& query,
                                                              std::size_t limit)
{
  nlohmann::json qbody;
  qbody["query"] =
      "SELECT TOP @lim * FROM c WHERE c.type = 'event' AND c.agent_name = @sn "
      "AND CONTAINS(c.data, @q) ORDER BY c.timestamp DESC";
  qbody["parameters"] = {
      {{"name", "@sn"}, {"value", store_name}},
      {{"name", "@q"}, {"value", query}},
      {{"name", "@lim"}, {"value", static_cast<int64_t>(limit)}}};

  auto resp = cosmos_request("POST", coll_path(database_, container_) + "/docs",
                             qbody.dump(), store_name);
  auto j = nlohmann::json::parse(resp);

  std::vector<MemoryEventRecord> result;
  if (j.contains("Documents"))
  {
    for (auto& d : j["Documents"])
    {
      MemoryEventRecord r;
      r.timestamp = d.value("timestamp", int64_t{0});
      r.type = d.value("event_type", "");
      r.data = d.value("data", "");
      r.agent = d.value("agent", "");
      result.push_back(std::move(r));
    }
  }
  return result;
}

void CosmosDBBackend::clear(const std::string& store_name)
{
  // Query all document ids for this partition, then delete each one.
  nlohmann::json qbody;
  qbody["query"] = "SELECT c.id FROM c WHERE c.agent_name = @sn";
  qbody["parameters"] = {{{"name", "@sn"}, {"value", store_name}}};

  auto resp = cosmos_request("POST", coll_path(database_, container_) + "/docs",
                             qbody.dump(), store_name);
  auto j = nlohmann::json::parse(resp);

  if (j.contains("Documents"))
  {
    for (auto& d : j["Documents"])
    {
      const std::string doc_id = d.value("id", "");
      if (!doc_id.empty())
      {
        cosmos_request("DELETE",
                       coll_path(database_, container_) + "/docs/" + doc_id,
                       "", store_name);
      }
    }
  }
}

// ---------------------------------------------------------------------------
// Learning interactions
// ---------------------------------------------------------------------------

void CosmosDBBackend::store_learning_interaction(const LearningInteraction& interaction)
{
  nlohmann::json doc;
  doc["id"] = make_id("learning_interaction", interaction.agent_name,
                       std::to_string(epoch_millis()));
  doc["type"] = "learning_interaction";
  doc["agent_name"] = interaction.agent_name;
  doc["query"] = interaction.query;
  doc["response"] = interaction.response;
  doc["reflection_score"] = interaction.reflection_score;
  doc["feedback_score"] = interaction.feedback_score;
  doc["tokens_used"] = interaction.tokens_used;
  doc["timestamp"] = interaction.timestamp;

  cosmos_request("POST", coll_path(database_, container_) + "/docs",
                 doc.dump(), interaction.agent_name);
}

std::vector<LearningInteraction> CosmosDBBackend::load_recent_interactions(
    const std::string& agent, size_t limit)
{
  nlohmann::json qbody;
  qbody["query"] =
      "SELECT TOP @lim * FROM c WHERE c.type = 'learning_interaction' "
      "AND c.agent_name = @ag ORDER BY c.timestamp DESC";
  qbody["parameters"] = {
      {{"name", "@ag"}, {"value", agent}},
      {{"name", "@lim"}, {"value", static_cast<int64_t>(limit)}}};

  auto resp = cosmos_request("POST", coll_path(database_, container_) + "/docs",
                             qbody.dump(), agent);
  auto j = nlohmann::json::parse(resp);

  std::vector<LearningInteraction> result;
  if (j.contains("Documents"))
  {
    for (auto& d : j["Documents"])
    {
      LearningInteraction li;
      li.agent_name = d.value("agent_name", "");
      li.query = d.value("query", "");
      li.response = d.value("response", "");
      li.reflection_score = d.value("reflection_score", 0.0);
      li.feedback_score = d.value("feedback_score", 0.0);
      li.tokens_used = d.value("tokens_used", size_t{0});
      li.timestamp = d.value("timestamp", int64_t{0});
      result.push_back(std::move(li));
    }
  }
  return result;
}

void CosmosDBBackend::store_learning_review(const LearningReview& review)
{
  nlohmann::json doc;
  doc["id"] = make_id("learning_review", review.agent_name,
                       std::to_string(epoch_millis()));
  doc["type"] = "learning_review";
  doc["agent_name"] = review.agent_name;
  doc["strategy"] = review.strategy;
  doc["interactions_reviewed"] = review.interactions_reviewed;
  doc["avg_reflection_score"] = review.avg_reflection_score;
  doc["lessons_json"] = review.lessons_json;
  doc["prompt_addendum"] = review.prompt_addendum;
  doc["timestamp"] = review.timestamp;

  cosmos_request("POST", coll_path(database_, container_) + "/docs",
                 doc.dump(), review.agent_name);
}

// ---------------------------------------------------------------------------
// Prompt evolution
// ---------------------------------------------------------------------------

void CosmosDBBackend::store_prompt_version(const PromptVersion& version)
{
  nlohmann::json doc;
  doc["id"] = make_id("prompt_evolution", version.agent_name,
                       "v" + std::to_string(version.version));
  doc["type"] = "prompt_evolution";
  doc["agent_name"] = version.agent_name;
  doc["version"] = version.version;
  doc["original_prompt"] = version.original_prompt;
  doc["evolved_prompt"] = version.evolved_prompt;
  doc["reasoning"] = version.reasoning;
  doc["status"] = version.status;
  doc["timestamp"] = version.timestamp;

  cosmos_request("POST", coll_path(database_, container_) + "/docs",
                 doc.dump(), version.agent_name);
}

std::vector<PromptVersion> CosmosDBBackend::load_prompt_history(const std::string& agent)
{
  nlohmann::json qbody;
  qbody["query"] =
      "SELECT * FROM c WHERE c.type = 'prompt_evolution' "
      "AND c.agent_name = @ag ORDER BY c.version ASC";
  qbody["parameters"] = {{{"name", "@ag"}, {"value", agent}}};

  auto resp = cosmos_request("POST", coll_path(database_, container_) + "/docs",
                             qbody.dump(), agent);
  auto j = nlohmann::json::parse(resp);

  std::vector<PromptVersion> result;
  if (j.contains("Documents"))
  {
    for (auto& d : j["Documents"])
    {
      PromptVersion pv;
      pv.agent_name = d.value("agent_name", "");
      pv.version = d.value("version", 0);
      pv.original_prompt = d.value("original_prompt", "");
      pv.evolved_prompt = d.value("evolved_prompt", "");
      pv.reasoning = d.value("reasoning", "");
      pv.status = d.value("status", "active");
      pv.timestamp = d.value("timestamp", int64_t{0});
      result.push_back(std::move(pv));
    }
  }
  return result;
}

bool CosmosDBBackend::try_update_prompt(const std::string& agent,
                                        int expected_version,
                                        const PromptVersion& new_version)
{
  // Read current latest version, verify it matches expected_version.
  auto history = load_prompt_history(agent);
  if (!history.empty() && history.back().version != expected_version)
  {
    return false;
  }
  store_prompt_version(new_version);
  return true;
}

// ---------------------------------------------------------------------------
// Autonomous actions & budget
// ---------------------------------------------------------------------------

void CosmosDBBackend::store_autonomous_action(const AutonomousAction& action)
{
  nlohmann::json doc;
  doc["id"] = make_id("autonomous_action", action.agent_name,
                       std::to_string(epoch_millis()));
  doc["type"] = "autonomous_action";
  doc["agent_name"] = action.agent_name;
  doc["trigger_type"] = action.trigger_type;
  doc["action_taken"] = action.action_taken;
  doc["tokens_used"] = action.tokens_used;
  doc["timestamp"] = action.timestamp;

  cosmos_request("POST", coll_path(database_, container_) + "/docs",
                 doc.dump(), action.agent_name);
}

DailyBudget CosmosDBBackend::load_budget(const std::string& agent,
                                         const std::string& date)
{
  const std::string doc_id = make_id("budget", agent, date);
  auto resp = cosmos_request("GET",
                             coll_path(database_, container_) + "/docs/" + doc_id,
                             "", agent);

  DailyBudget budget;
  budget.date = date;

  if (resp.empty())
  {
    return budget;
  }
  auto j = nlohmann::json::parse(resp, nullptr, false);
  if (j.is_discarded() || !j.contains("date"))
  {
    return budget;
  }
  budget.calls_used = j.value("calls_used", size_t{0});
  budget.tokens_used = j.value("tokens_used", size_t{0});
  budget.cost_used = j.value("cost_used", 0.0);
  return budget;
}

void CosmosDBBackend::update_budget(const std::string& agent,
                                    const DailyBudget& budget)
{
  nlohmann::json doc;
  doc["id"] = make_id("budget", agent, budget.date);
  doc["type"] = "budget";
  doc["agent_name"] = agent;
  doc["date"] = budget.date;
  doc["calls_used"] = budget.calls_used;
  doc["tokens_used"] = budget.tokens_used;
  doc["cost_used"] = budget.cost_used;

  // Upsert: CosmosDB will replace if id already exists when using POST with
  // x-ms-documentdb-is-upsert: true.  For the placeholder we simply POST;
  // a production implementation should add the upsert header.
  cosmos_request("POST", coll_path(database_, container_) + "/docs",
                 doc.dump(), agent);
}

// ---------------------------------------------------------------------------
// Distributed locking via conditional writes
// ---------------------------------------------------------------------------

bool CosmosDBBackend::try_acquire_lock(const std::string& lock_name,
                                       const std::string& holder_id,
                                       std::chrono::seconds ttl)
{
  const int64_t now = epoch_millis();
  const int64_t expires = now + static_cast<int64_t>(ttl.count()) * 1000;

  nlohmann::json doc;
  doc["id"] = make_id("lock", lock_name, "singleton");
  doc["type"] = "lock";
  doc["agent_name"] = lock_name;
  doc["holder_id"] = holder_id;
  doc["acquired_at"] = now;
  doc["expires_at"] = expires;

  // Attempt create; HTTP 409 means lock already held.
  auto resp = cosmos_request("POST", coll_path(database_, container_) + "/docs",
                             doc.dump(), lock_name);

  auto j = nlohmann::json::parse(resp, nullptr, false);
  if (j.is_discarded())
  {
    return false;
  }
  // If the document was created we own the lock.
  if (j.contains("id"))
  {
    return true;
  }
  return false;
}

void CosmosDBBackend::release_lock(const std::string& lock_name,
                                   const std::string& holder_id)
{
  const std::string doc_id = make_id("lock", lock_name, "singleton");

  // Verify holder before deleting.
  auto resp = cosmos_request("GET",
                             coll_path(database_, container_) + "/docs/" + doc_id,
                             "", lock_name);
  auto j = nlohmann::json::parse(resp, nullptr, false);
  if (j.is_discarded() || j.value("holder_id", "") != holder_id)
  {
    throw std::runtime_error("CosmosDB: lock not held by " + holder_id);
  }

  cosmos_request("DELETE",
                 coll_path(database_, container_) + "/docs/" + doc_id,
                 "", lock_name);
}

bool CosmosDBBackend::renew_lock(const std::string& lock_name,
                                 const std::string& holder_id,
                                 std::chrono::seconds ttl)
{
  const std::string doc_id = make_id("lock", lock_name, "singleton");

  auto resp = cosmos_request("GET",
                             coll_path(database_, container_) + "/docs/" + doc_id,
                             "", lock_name);
  auto j = nlohmann::json::parse(resp, nullptr, false);
  if (j.is_discarded() || j.value("holder_id", "") != holder_id)
  {
    return false;
  }

  const int64_t now = epoch_millis();
  j["expires_at"] = now + static_cast<int64_t>(ttl.count()) * 1000;

  // Replace the document.
  cosmos_request("PUT",
                 coll_path(database_, container_) + "/docs/" + doc_id,
                 j.dump(), lock_name);
  return true;
}

}  // namespace neamc::vm

#endif  // NEAM_BACKEND_AZURE
