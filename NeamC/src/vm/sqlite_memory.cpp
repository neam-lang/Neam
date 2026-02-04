// SPDX-License-Identifier: Apache-2.0
//
// Neam Virtual Machine - SQLite memory backend
//

#include "neamc/vm/sqlite_memory.hpp"

#include "neamc/vm/autonomous.hpp"

#include <chrono>
#include <filesystem>
#include <stdexcept>

#include "sqlite3.h"

namespace neamc::vm
{
SqliteMemoryBackend::SqliteMemoryBackend(const std::string& db_path)
{
  // Ensure parent directory exists
  std::filesystem::path path(db_path);
  if (path.has_parent_path())
  {
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
  }

  const int rc = sqlite3_open_v2(db_path.c_str(), &db_,
                                  SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE, nullptr);
  if (rc != SQLITE_OK)
  {
    throw std::runtime_error("Failed to open SQLite database: " + db_path);
  }
  initialize_schema();
}

SqliteMemoryBackend::~SqliteMemoryBackend()
{
  if (db_)
  {
    sqlite3_close(db_);
  }
}

void SqliteMemoryBackend::initialize_schema()
{
  const char* schema = R"(
    CREATE TABLE IF NOT EXISTS events (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      store_name TEXT NOT NULL,
      timestamp INTEGER NOT NULL,
      type TEXT NOT NULL,
      data TEXT NOT NULL,
      agent TEXT NOT NULL
    );
    CREATE INDEX IF NOT EXISTS idx_events_store ON events(store_name);
    CREATE INDEX IF NOT EXISTS idx_events_timestamp ON events(store_name, timestamp);

    CREATE TABLE IF NOT EXISTS checkpoints (
      store_name TEXT NOT NULL,
      label TEXT NOT NULL,
      position INTEGER NOT NULL,
      PRIMARY KEY (store_name, label)
    );

    CREATE TABLE IF NOT EXISTS learning_interactions (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      agent_name TEXT NOT NULL,
      query TEXT NOT NULL,
      response TEXT NOT NULL,
      reflection_score REAL,
      feedback_score REAL,
      tokens_used INTEGER DEFAULT 0,
      timestamp INTEGER NOT NULL
    );
    CREATE INDEX IF NOT EXISTS idx_learning_agent ON learning_interactions(agent_name);
    CREATE INDEX IF NOT EXISTS idx_learning_timestamp ON learning_interactions(agent_name, timestamp);

    CREATE TABLE IF NOT EXISTS learning_reviews (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      agent_name TEXT NOT NULL,
      strategy TEXT NOT NULL,
      interactions_reviewed INTEGER DEFAULT 0,
      avg_reflection_score REAL,
      lessons_json TEXT,
      prompt_addendum TEXT,
      timestamp INTEGER NOT NULL
    );
    CREATE INDEX IF NOT EXISTS idx_reviews_agent ON learning_reviews(agent_name);

    CREATE TABLE IF NOT EXISTS prompt_evolution (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      agent_name TEXT NOT NULL,
      version INTEGER NOT NULL,
      original_prompt TEXT,
      evolved_prompt TEXT NOT NULL,
      reasoning TEXT,
      status TEXT DEFAULT 'active',
      timestamp INTEGER NOT NULL,
      UNIQUE(agent_name, version)
    );
    CREATE INDEX IF NOT EXISTS idx_evolution_agent ON prompt_evolution(agent_name);

    CREATE TABLE IF NOT EXISTS autonomous_actions (
      id INTEGER PRIMARY KEY AUTOINCREMENT,
      agent_name TEXT NOT NULL,
      trigger_type TEXT NOT NULL,
      action_taken TEXT,
      tokens_used INTEGER DEFAULT 0,
      timestamp INTEGER NOT NULL
    );
    CREATE INDEX IF NOT EXISTS idx_autonomous_agent ON autonomous_actions(agent_name);

    CREATE TABLE IF NOT EXISTS autonomous_budgets (
      agent_name TEXT NOT NULL,
      date TEXT NOT NULL,
      calls_used INTEGER DEFAULT 0,
      tokens_used INTEGER DEFAULT 0,
      cost_used REAL DEFAULT 0.0,
      PRIMARY KEY (agent_name, date)
    );

    CREATE TABLE IF NOT EXISTS distributed_locks (
      lock_name TEXT PRIMARY KEY,
      holder_id TEXT NOT NULL,
      expires_at INTEGER NOT NULL
    );
  )";

  char* errmsg = nullptr;
  const int rc = sqlite3_exec(db_, schema, nullptr, nullptr, &errmsg);
  if (rc != SQLITE_OK)
  {
    std::string msg = errmsg ? errmsg : "unknown error";
    sqlite3_free(errmsg);
    throw std::runtime_error("Failed to initialize schema: " + msg);
  }
}

void SqliteMemoryBackend::store_event(const std::string& store_name,
                                       const MemoryEventRecord& event)
{
  const char* sql = "INSERT INTO events (store_name, timestamp, type, data, agent) VALUES (?, ?, ?, ?, ?)";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
  {
    throw std::runtime_error(std::string("Failed to prepare insert: ") + sqlite3_errmsg(db_));
  }

  sqlite3_bind_text(stmt, 1, store_name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 2, event.timestamp);
  sqlite3_bind_text(stmt, 3, event.type.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, event.data.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 5, event.agent.c_str(), -1, SQLITE_TRANSIENT);

  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

std::vector<MemoryEventRecord> SqliteMemoryBackend::load_events(const std::string& store_name)
{
  const char* sql = "SELECT timestamp, type, data, agent FROM events WHERE store_name = ? ORDER BY timestamp";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
  {
    return {};
  }

  sqlite3_bind_text(stmt, 1, store_name.c_str(), -1, SQLITE_TRANSIENT);

  std::vector<MemoryEventRecord> events;
  while (sqlite3_step(stmt) == SQLITE_ROW)
  {
    MemoryEventRecord event;
    event.timestamp = sqlite3_column_int64(stmt, 0);
    const auto* type_text = sqlite3_column_text(stmt, 1);
    event.type = type_text ? reinterpret_cast<const char*>(type_text) : "";
    const auto* data_text = sqlite3_column_text(stmt, 2);
    event.data = data_text ? reinterpret_cast<const char*>(data_text) : "";
    const auto* agent_text = sqlite3_column_text(stmt, 3);
    event.agent = agent_text ? reinterpret_cast<const char*>(agent_text) : "";
    events.push_back(std::move(event));
  }
  sqlite3_finalize(stmt);
  return events;
}

void SqliteMemoryBackend::save_checkpoint(const std::string& store_name,
                                           const std::string& label, std::size_t position)
{
  const char* sql = "INSERT OR REPLACE INTO checkpoints (store_name, label, position) VALUES (?, ?, ?)";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
  {
    return;
  }

  sqlite3_bind_text(stmt, 1, store_name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, label.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 3, static_cast<long long>(position));

  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

std::optional<std::size_t> SqliteMemoryBackend::load_checkpoint(const std::string& store_name,
                                                                 const std::string& label)
{
  const char* sql = "SELECT position FROM checkpoints WHERE store_name = ? AND label = ?";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
  {
    return std::nullopt;
  }

  sqlite3_bind_text(stmt, 1, store_name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, label.c_str(), -1, SQLITE_TRANSIENT);

  std::optional<std::size_t> result;
  if (sqlite3_step(stmt) == SQLITE_ROW)
  {
    result = static_cast<std::size_t>(sqlite3_column_int64(stmt, 0));
  }
  sqlite3_finalize(stmt);
  return result;
}

std::vector<MemoryEventRecord> SqliteMemoryBackend::search_events(const std::string& store_name,
                                                                   const std::string& query,
                                                                   std::size_t limit)
{
  const char* sql = "SELECT timestamp, type, data, agent FROM events WHERE store_name = ? AND data LIKE ? ORDER BY timestamp LIMIT ?";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
  {
    return {};
  }

  const std::string like_pattern = "%" + query + "%";
  sqlite3_bind_text(stmt, 1, store_name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, like_pattern.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 3, static_cast<long long>(limit));

  std::vector<MemoryEventRecord> events;
  while (sqlite3_step(stmt) == SQLITE_ROW)
  {
    MemoryEventRecord event;
    event.timestamp = sqlite3_column_int64(stmt, 0);
    const auto* type_text = sqlite3_column_text(stmt, 1);
    event.type = type_text ? reinterpret_cast<const char*>(type_text) : "";
    const auto* data_text = sqlite3_column_text(stmt, 2);
    event.data = data_text ? reinterpret_cast<const char*>(data_text) : "";
    const auto* agent_text = sqlite3_column_text(stmt, 3);
    event.agent = agent_text ? reinterpret_cast<const char*>(agent_text) : "";
    events.push_back(std::move(event));
  }
  sqlite3_finalize(stmt);
  return events;
}

void SqliteMemoryBackend::clear(const std::string& store_name)
{
  const char* sql = "DELETE FROM events WHERE store_name = ?";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
  {
    return;
  }
  sqlite3_bind_text(stmt, 1, store_name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_step(stmt);
  sqlite3_finalize(stmt);

  const char* sql2 = "DELETE FROM checkpoints WHERE store_name = ?";
  sqlite3_stmt* stmt2 = nullptr;
  if (sqlite3_prepare_v2(db_, sql2, -1, &stmt2, nullptr) != SQLITE_OK)
  {
    return;
  }
  sqlite3_bind_text(stmt2, 1, store_name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_step(stmt2);
  sqlite3_finalize(stmt2);
}

// --- StateBackend extensions (v0.6.0) ---

void SqliteMemoryBackend::store_learning_interaction(const LearningInteraction& interaction)
{
  const char* sql =
      "INSERT INTO learning_interactions (agent_name, query, response, reflection_score, "
      "feedback_score, tokens_used, timestamp) VALUES (?, ?, ?, ?, ?, ?, ?)";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return;

  sqlite3_bind_text(stmt, 1, interaction.agent_name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, interaction.query.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, interaction.response.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_double(stmt, 4, interaction.reflection_score);
  sqlite3_bind_double(stmt, 5, interaction.feedback_score);
  sqlite3_bind_int64(stmt, 6, static_cast<long long>(interaction.tokens_used));
  sqlite3_bind_int64(stmt, 7, interaction.timestamp);

  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

std::vector<LearningInteraction> SqliteMemoryBackend::load_recent_interactions(
    const std::string& agent, size_t limit)
{
  const char* sql =
      "SELECT agent_name, query, response, reflection_score, feedback_score, tokens_used, "
      "timestamp FROM learning_interactions WHERE agent_name = ? ORDER BY timestamp DESC LIMIT ?";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return {};

  sqlite3_bind_text(stmt, 1, agent.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 2, static_cast<long long>(limit));

  std::vector<LearningInteraction> results;
  while (sqlite3_step(stmt) == SQLITE_ROW)
  {
    LearningInteraction li;
    const auto* col0 = sqlite3_column_text(stmt, 0);
    li.agent_name = col0 ? reinterpret_cast<const char*>(col0) : "";
    const auto* col1 = sqlite3_column_text(stmt, 1);
    li.query = col1 ? reinterpret_cast<const char*>(col1) : "";
    const auto* col2 = sqlite3_column_text(stmt, 2);
    li.response = col2 ? reinterpret_cast<const char*>(col2) : "";
    li.reflection_score = sqlite3_column_double(stmt, 3);
    li.feedback_score = sqlite3_column_double(stmt, 4);
    li.tokens_used = static_cast<size_t>(sqlite3_column_int64(stmt, 5));
    li.timestamp = sqlite3_column_int64(stmt, 6);
    results.push_back(std::move(li));
  }
  sqlite3_finalize(stmt);
  return results;
}

void SqliteMemoryBackend::store_learning_review(const LearningReview& review)
{
  const char* sql =
      "INSERT INTO learning_reviews (agent_name, strategy, interactions_reviewed, "
      "avg_reflection_score, lessons_json, prompt_addendum, timestamp) "
      "VALUES (?, ?, ?, ?, ?, ?, ?)";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return;

  sqlite3_bind_text(stmt, 1, review.agent_name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, review.strategy.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 3, static_cast<long long>(review.interactions_reviewed));
  sqlite3_bind_double(stmt, 4, review.avg_reflection_score);
  sqlite3_bind_text(stmt, 5, review.lessons_json.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 6, review.prompt_addendum.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 7, review.timestamp);

  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

void SqliteMemoryBackend::store_prompt_version(const PromptVersion& version)
{
  const char* sql =
      "INSERT OR REPLACE INTO prompt_evolution (agent_name, version, original_prompt, "
      "evolved_prompt, reasoning, status, timestamp) VALUES (?, ?, ?, ?, ?, ?, ?)";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return;

  sqlite3_bind_text(stmt, 1, version.agent_name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 2, version.version);
  sqlite3_bind_text(stmt, 3, version.original_prompt.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, version.evolved_prompt.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 5, version.reasoning.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 6, version.status.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 7, version.timestamp);

  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

std::vector<PromptVersion> SqliteMemoryBackend::load_prompt_history(const std::string& agent)
{
  const char* sql =
      "SELECT agent_name, version, original_prompt, evolved_prompt, reasoning, status, timestamp "
      "FROM prompt_evolution WHERE agent_name = ? ORDER BY version";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return {};

  sqlite3_bind_text(stmt, 1, agent.c_str(), -1, SQLITE_TRANSIENT);

  std::vector<PromptVersion> results;
  while (sqlite3_step(stmt) == SQLITE_ROW)
  {
    PromptVersion pv;
    const auto* col0 = sqlite3_column_text(stmt, 0);
    pv.agent_name = col0 ? reinterpret_cast<const char*>(col0) : "";
    pv.version = sqlite3_column_int(stmt, 1);
    const auto* col2 = sqlite3_column_text(stmt, 2);
    pv.original_prompt = col2 ? reinterpret_cast<const char*>(col2) : "";
    const auto* col3 = sqlite3_column_text(stmt, 3);
    pv.evolved_prompt = col3 ? reinterpret_cast<const char*>(col3) : "";
    const auto* col4 = sqlite3_column_text(stmt, 4);
    pv.reasoning = col4 ? reinterpret_cast<const char*>(col4) : "";
    const auto* col5 = sqlite3_column_text(stmt, 5);
    pv.status = col5 ? reinterpret_cast<const char*>(col5) : "";
    pv.timestamp = sqlite3_column_int64(stmt, 6);
    results.push_back(std::move(pv));
  }
  sqlite3_finalize(stmt);
  return results;
}

bool SqliteMemoryBackend::try_update_prompt(const std::string& agent, int expected_version,
                                             const PromptVersion& new_version)
{
  const char* sql =
      "INSERT INTO prompt_evolution (agent_name, version, original_prompt, evolved_prompt, "
      "reasoning, status, timestamp) "
      "SELECT ?, ?, ?, ?, ?, ?, ? "
      "WHERE NOT EXISTS (SELECT 1 FROM prompt_evolution WHERE agent_name = ? AND version = ?)";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

  sqlite3_bind_text(stmt, 1, new_version.agent_name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 2, new_version.version);
  sqlite3_bind_text(stmt, 3, new_version.original_prompt.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 4, new_version.evolved_prompt.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 5, new_version.reasoning.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 6, new_version.status.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 7, new_version.timestamp);
  sqlite3_bind_text(stmt, 8, agent.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int(stmt, 9, new_version.version);

  sqlite3_step(stmt);
  int changes = sqlite3_changes(db_);
  sqlite3_finalize(stmt);
  return changes > 0;
}

void SqliteMemoryBackend::store_autonomous_action(const AutonomousAction& action)
{
  const char* sql =
      "INSERT INTO autonomous_actions (agent_name, trigger_type, action_taken, tokens_used, "
      "timestamp) VALUES (?, ?, ?, ?, ?)";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return;

  sqlite3_bind_text(stmt, 1, action.agent_name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, action.trigger_type.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, action.action_taken.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 4, static_cast<long long>(action.tokens_used));
  sqlite3_bind_int64(stmt, 5, action.timestamp);

  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

DailyBudget SqliteMemoryBackend::load_budget(const std::string& agent, const std::string& date)
{
  const char* sql =
      "SELECT calls_used, tokens_used, cost_used FROM autonomous_budgets "
      "WHERE agent_name = ? AND date = ?";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK)
  {
    return DailyBudget{date, 0, 0, 0.0};
  }

  sqlite3_bind_text(stmt, 1, agent.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, date.c_str(), -1, SQLITE_TRANSIENT);

  DailyBudget budget;
  budget.date = date;
  if (sqlite3_step(stmt) == SQLITE_ROW)
  {
    budget.calls_used = static_cast<size_t>(sqlite3_column_int64(stmt, 0));
    budget.tokens_used = static_cast<size_t>(sqlite3_column_int64(stmt, 1));
    budget.cost_used = sqlite3_column_double(stmt, 2);
  }
  sqlite3_finalize(stmt);
  return budget;
}

void SqliteMemoryBackend::update_budget(const std::string& agent, const DailyBudget& budget)
{
  const char* sql =
      "INSERT OR REPLACE INTO autonomous_budgets (agent_name, date, calls_used, tokens_used, "
      "cost_used) VALUES (?, ?, ?, ?, ?)";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return;

  sqlite3_bind_text(stmt, 1, agent.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, budget.date.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 3, static_cast<long long>(budget.calls_used));
  sqlite3_bind_int64(stmt, 4, static_cast<long long>(budget.tokens_used));
  sqlite3_bind_double(stmt, 5, budget.cost_used);

  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

bool SqliteMemoryBackend::try_acquire_lock(const std::string& lock_name,
                                            const std::string& holder_id,
                                            std::chrono::seconds ttl)
{
  int64_t now = std::chrono::duration_cast<std::chrono::seconds>(
                    std::chrono::system_clock::now().time_since_epoch())
                    .count();
  int64_t expires = now + ttl.count();

  const char* sql =
      "INSERT OR REPLACE INTO distributed_locks (lock_name, holder_id, expires_at) "
      "SELECT ?, ?, ? "
      "WHERE NOT EXISTS ("
      "  SELECT 1 FROM distributed_locks WHERE lock_name = ? AND holder_id != ? AND expires_at > ?"
      ")";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

  sqlite3_bind_text(stmt, 1, lock_name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, holder_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 3, expires);
  sqlite3_bind_text(stmt, 4, lock_name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 5, holder_id.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_int64(stmt, 6, now);

  sqlite3_step(stmt);
  int changes = sqlite3_changes(db_);
  sqlite3_finalize(stmt);
  return changes > 0;
}

void SqliteMemoryBackend::release_lock(const std::string& lock_name,
                                        const std::string& holder_id)
{
  const char* sql = "DELETE FROM distributed_locks WHERE lock_name = ? AND holder_id = ?";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return;

  sqlite3_bind_text(stmt, 1, lock_name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 2, holder_id.c_str(), -1, SQLITE_TRANSIENT);

  sqlite3_step(stmt);
  sqlite3_finalize(stmt);
}

bool SqliteMemoryBackend::renew_lock(const std::string& lock_name, const std::string& holder_id,
                                      std::chrono::seconds ttl)
{
  int64_t expires = std::chrono::duration_cast<std::chrono::seconds>(
                        std::chrono::system_clock::now().time_since_epoch())
                        .count() +
                    ttl.count();

  const char* sql =
      "UPDATE distributed_locks SET expires_at = ? WHERE lock_name = ? AND holder_id = ?";
  sqlite3_stmt* stmt = nullptr;
  if (sqlite3_prepare_v2(db_, sql, -1, &stmt, nullptr) != SQLITE_OK) return false;

  sqlite3_bind_int64(stmt, 1, expires);
  sqlite3_bind_text(stmt, 2, lock_name.c_str(), -1, SQLITE_TRANSIENT);
  sqlite3_bind_text(stmt, 3, holder_id.c_str(), -1, SQLITE_TRANSIENT);

  sqlite3_step(stmt);
  int changes = sqlite3_changes(db_);
  sqlite3_finalize(stmt);
  return changes > 0;
}

std::unique_ptr<MemoryBackend> create_sqlite_backend(const std::string& path)
{
  return std::make_unique<SqliteMemoryBackend>(path);
}
}  // namespace neamc::vm
