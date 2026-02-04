// SPDX-License-Identifier: Apache-2.0
//
// Neam Virtual Machine - PostgreSQL State Backend (v0.6.0)
//

#ifdef NEAM_BACKEND_POSTGRES

#include "neamc/vm/state_postgres.hpp"
#include "neamc/vm/autonomous.hpp"

#include <libpq-fe.h>
#include <stdexcept>
#include <string>

namespace neamc::vm
{

// ---------------------------------------------------------------------------
// RAII guard for PGresult
// ---------------------------------------------------------------------------
struct PGResultGuard
{
  PGresult* res{nullptr};
  PGResultGuard(PGresult* r) : res(r) {}
  ~PGResultGuard() { if (res) PQclear(res); }
  PGResultGuard(const PGResultGuard&) = delete;
  PGResultGuard& operator=(const PGResultGuard&) = delete;
  operator PGresult*() const { return res; }
};

// ---------------------------------------------------------------------------
// Helper: check PGresult for expected status, throw on failure
// ---------------------------------------------------------------------------
static void check_result(PGconn* conn, PGresult* res, ExecStatusType expected)
{
  if (PQresultStatus(res) != expected)
  {
    std::string msg = PQerrorMessage(conn);
    throw std::runtime_error("PostgreSQL error: " + msg);
  }
}

// ---------------------------------------------------------------------------
// Construction / destruction
// ---------------------------------------------------------------------------
PostgresBackend::PostgresBackend(const std::string& connection_string)
{
  conn_ = PQconnectdb(connection_string.c_str());
  if (PQstatus(conn_) != CONNECTION_OK)
  {
    std::string msg = PQerrorMessage(conn_);
    PQfinish(conn_);
    conn_ = nullptr;
    throw std::runtime_error("Failed to connect to PostgreSQL: " + msg);
  }
  initialize_schema();
}

PostgresBackend::~PostgresBackend()
{
  if (conn_)
  {
    PQfinish(conn_);
  }
}

// ---------------------------------------------------------------------------
// exec helper (non-parameterized DDL / utility statements)
// ---------------------------------------------------------------------------
void PostgresBackend::exec(const std::string& sql)
{
  PGResultGuard res(PQexec(conn_, sql.c_str()));
  check_result(conn_, res, PGRES_COMMAND_OK);
}

// ---------------------------------------------------------------------------
// Schema initialisation (neam_ prefixed tables)
// ---------------------------------------------------------------------------
void PostgresBackend::initialize_schema()
{
  const char* schema = R"(
    CREATE TABLE IF NOT EXISTS neam_events (
      id BIGSERIAL PRIMARY KEY,
      store_name TEXT NOT NULL,
      timestamp BIGINT NOT NULL,
      type TEXT NOT NULL,
      data TEXT NOT NULL,
      agent TEXT NOT NULL
    );
    CREATE INDEX IF NOT EXISTS idx_neam_events_store ON neam_events(store_name);
    CREATE INDEX IF NOT EXISTS idx_neam_events_ts ON neam_events(store_name, timestamp);

    CREATE TABLE IF NOT EXISTS neam_checkpoints (
      store_name TEXT NOT NULL,
      label TEXT NOT NULL,
      position BIGINT NOT NULL,
      PRIMARY KEY (store_name, label)
    );

    CREATE TABLE IF NOT EXISTS neam_learning_interactions (
      id BIGSERIAL PRIMARY KEY,
      agent_name TEXT NOT NULL,
      query TEXT NOT NULL,
      response TEXT NOT NULL,
      reflection_score DOUBLE PRECISION,
      feedback_score DOUBLE PRECISION,
      tokens_used INTEGER DEFAULT 0,
      timestamp BIGINT NOT NULL
    );
    CREATE INDEX IF NOT EXISTS idx_neam_li_agent ON neam_learning_interactions(agent_name);
    CREATE INDEX IF NOT EXISTS idx_neam_li_ts ON neam_learning_interactions(agent_name, timestamp);

    CREATE TABLE IF NOT EXISTS neam_learning_reviews (
      id BIGSERIAL PRIMARY KEY,
      agent_name TEXT NOT NULL,
      strategy TEXT NOT NULL,
      interactions_reviewed INTEGER DEFAULT 0,
      avg_reflection_score DOUBLE PRECISION,
      lessons_json TEXT,
      prompt_addendum TEXT,
      timestamp BIGINT NOT NULL
    );
    CREATE INDEX IF NOT EXISTS idx_neam_lr_agent ON neam_learning_reviews(agent_name);

    CREATE TABLE IF NOT EXISTS neam_prompt_evolution (
      id BIGSERIAL PRIMARY KEY,
      agent_name TEXT NOT NULL,
      version INTEGER NOT NULL,
      original_prompt TEXT,
      evolved_prompt TEXT NOT NULL,
      reasoning TEXT,
      status TEXT DEFAULT 'active',
      timestamp BIGINT NOT NULL,
      UNIQUE(agent_name, version)
    );
    CREATE INDEX IF NOT EXISTS idx_neam_pe_agent ON neam_prompt_evolution(agent_name);

    CREATE TABLE IF NOT EXISTS neam_autonomous_actions (
      id BIGSERIAL PRIMARY KEY,
      agent_name TEXT NOT NULL,
      trigger_type TEXT NOT NULL,
      action_taken TEXT,
      tokens_used INTEGER DEFAULT 0,
      timestamp BIGINT NOT NULL
    );
    CREATE INDEX IF NOT EXISTS idx_neam_aa_agent ON neam_autonomous_actions(agent_name);

    CREATE TABLE IF NOT EXISTS neam_autonomous_budgets (
      agent_name TEXT NOT NULL,
      date TEXT NOT NULL,
      calls_used INTEGER DEFAULT 0,
      tokens_used INTEGER DEFAULT 0,
      cost_used DOUBLE PRECISION DEFAULT 0.0,
      PRIMARY KEY (agent_name, date)
    );

    CREATE TABLE IF NOT EXISTS neam_distributed_locks (
      lock_name TEXT PRIMARY KEY,
      holder_id TEXT NOT NULL,
      acquired_at TIMESTAMPTZ NOT NULL DEFAULT NOW(),
      expires_at TIMESTAMPTZ NOT NULL
    );
  )";

  exec(schema);
}

// ---------------------------------------------------------------------------
// MemoryBackend: store_event
// ---------------------------------------------------------------------------
void PostgresBackend::store_event(const std::string& store_name,
                                   const MemoryEventRecord& event)
{
  const char* sql =
      "INSERT INTO neam_events (store_name, timestamp, type, data, agent) "
      "VALUES ($1, $2, $3, $4, $5)";

  std::string ts = std::to_string(event.timestamp);

  const char* params[] = {store_name.c_str(), ts.c_str(), event.type.c_str(),
                          event.data.c_str(), event.agent.c_str()};

  PGResultGuard res(PQexecParams(conn_, sql, 5, nullptr, params, nullptr, nullptr, 0));
  check_result(conn_, res, PGRES_COMMAND_OK);
}

// ---------------------------------------------------------------------------
// MemoryBackend: load_events
// ---------------------------------------------------------------------------
std::vector<MemoryEventRecord> PostgresBackend::load_events(const std::string& store_name)
{
  const char* sql =
      "SELECT timestamp, type, data, agent FROM neam_events "
      "WHERE store_name = $1 ORDER BY timestamp";

  const char* params[] = {store_name.c_str()};

  PGResultGuard res(PQexecParams(conn_, sql, 1, nullptr, params, nullptr, nullptr, 0));
  check_result(conn_, res, PGRES_TUPLES_OK);

  const int rows = PQntuples(res);
  std::vector<MemoryEventRecord> events;
  events.reserve(static_cast<size_t>(rows));

  for (int i = 0; i < rows; ++i)
  {
    MemoryEventRecord ev;
    ev.timestamp = std::stoll(PQgetvalue(res, i, 0));
    ev.type      = PQgetvalue(res, i, 1);
    ev.data      = PQgetvalue(res, i, 2);
    ev.agent     = PQgetvalue(res, i, 3);
    events.push_back(std::move(ev));
  }
  return events;
}

// ---------------------------------------------------------------------------
// MemoryBackend: save_checkpoint (upsert)
// ---------------------------------------------------------------------------
void PostgresBackend::save_checkpoint(const std::string& store_name,
                                       const std::string& label,
                                       std::size_t position)
{
  const char* sql =
      "INSERT INTO neam_checkpoints (store_name, label, position) "
      "VALUES ($1, $2, $3) "
      "ON CONFLICT (store_name, label) DO UPDATE SET position = EXCLUDED.position";

  std::string pos_str = std::to_string(position);
  const char* params[] = {store_name.c_str(), label.c_str(), pos_str.c_str()};

  PGResultGuard res(PQexecParams(conn_, sql, 3, nullptr, params, nullptr, nullptr, 0));
  check_result(conn_, res, PGRES_COMMAND_OK);
}

// ---------------------------------------------------------------------------
// MemoryBackend: load_checkpoint
// ---------------------------------------------------------------------------
std::optional<std::size_t> PostgresBackend::load_checkpoint(const std::string& store_name,
                                                             const std::string& label)
{
  const char* sql =
      "SELECT position FROM neam_checkpoints WHERE store_name = $1 AND label = $2";

  const char* params[] = {store_name.c_str(), label.c_str()};

  PGResultGuard res(PQexecParams(conn_, sql, 2, nullptr, params, nullptr, nullptr, 0));
  check_result(conn_, res, PGRES_TUPLES_OK);

  if (PQntuples(res) == 0)
  {
    return std::nullopt;
  }
  return static_cast<std::size_t>(std::stoull(PQgetvalue(res, 0, 0)));
}

// ---------------------------------------------------------------------------
// MemoryBackend: search_events (ILIKE for case-insensitive search)
// ---------------------------------------------------------------------------
std::vector<MemoryEventRecord> PostgresBackend::search_events(const std::string& store_name,
                                                               const std::string& query,
                                                               std::size_t limit)
{
  const char* sql =
      "SELECT timestamp, type, data, agent FROM neam_events "
      "WHERE store_name = $1 AND data ILIKE $2 ORDER BY timestamp LIMIT $3";

  const std::string like_pattern = "%" + query + "%";
  std::string limit_str = std::to_string(limit);

  const char* params[] = {store_name.c_str(), like_pattern.c_str(), limit_str.c_str()};

  PGResultGuard res(PQexecParams(conn_, sql, 3, nullptr, params, nullptr, nullptr, 0));
  check_result(conn_, res, PGRES_TUPLES_OK);

  const int rows = PQntuples(res);
  std::vector<MemoryEventRecord> events;
  events.reserve(static_cast<size_t>(rows));

  for (int i = 0; i < rows; ++i)
  {
    MemoryEventRecord ev;
    ev.timestamp = std::stoll(PQgetvalue(res, i, 0));
    ev.type      = PQgetvalue(res, i, 1);
    ev.data      = PQgetvalue(res, i, 2);
    ev.agent     = PQgetvalue(res, i, 3);
    events.push_back(std::move(ev));
  }
  return events;
}

// ---------------------------------------------------------------------------
// MemoryBackend: clear
// ---------------------------------------------------------------------------
void PostgresBackend::clear(const std::string& store_name)
{
  {
    const char* sql = "DELETE FROM neam_events WHERE store_name = $1";
    const char* params[] = {store_name.c_str()};
    PGResultGuard res(PQexecParams(conn_, sql, 1, nullptr, params, nullptr, nullptr, 0));
    check_result(conn_, res, PGRES_COMMAND_OK);
  }
  {
    const char* sql = "DELETE FROM neam_checkpoints WHERE store_name = $1";
    const char* params[] = {store_name.c_str()};
    PGResultGuard res(PQexecParams(conn_, sql, 1, nullptr, params, nullptr, nullptr, 0));
    check_result(conn_, res, PGRES_COMMAND_OK);
  }
}

// ---------------------------------------------------------------------------
// Learning: store_learning_interaction
// ---------------------------------------------------------------------------
void PostgresBackend::store_learning_interaction(const LearningInteraction& interaction)
{
  const char* sql =
      "INSERT INTO neam_learning_interactions "
      "(agent_name, query, response, reflection_score, feedback_score, tokens_used, timestamp) "
      "VALUES ($1, $2, $3, $4, $5, $6, $7)";

  std::string refl  = std::to_string(interaction.reflection_score);
  std::string fb    = std::to_string(interaction.feedback_score);
  std::string tok   = std::to_string(interaction.tokens_used);
  std::string ts    = std::to_string(interaction.timestamp);

  const char* params[] = {interaction.agent_name.c_str(), interaction.query.c_str(),
                          interaction.response.c_str(), refl.c_str(), fb.c_str(),
                          tok.c_str(), ts.c_str()};

  PGResultGuard res(PQexecParams(conn_, sql, 7, nullptr, params, nullptr, nullptr, 0));
  check_result(conn_, res, PGRES_COMMAND_OK);
}

// ---------------------------------------------------------------------------
// Learning: load_recent_interactions
// ---------------------------------------------------------------------------
std::vector<LearningInteraction> PostgresBackend::load_recent_interactions(
    const std::string& agent, size_t limit)
{
  const char* sql =
      "SELECT agent_name, query, response, reflection_score, feedback_score, "
      "tokens_used, timestamp FROM neam_learning_interactions "
      "WHERE agent_name = $1 ORDER BY timestamp DESC LIMIT $2";

  std::string limit_str = std::to_string(limit);
  const char* params[] = {agent.c_str(), limit_str.c_str()};

  PGResultGuard res(PQexecParams(conn_, sql, 2, nullptr, params, nullptr, nullptr, 0));
  check_result(conn_, res, PGRES_TUPLES_OK);

  const int rows = PQntuples(res);
  std::vector<LearningInteraction> interactions;
  interactions.reserve(static_cast<size_t>(rows));

  for (int i = 0; i < rows; ++i)
  {
    LearningInteraction li;
    li.agent_name       = PQgetvalue(res, i, 0);
    li.query            = PQgetvalue(res, i, 1);
    li.response         = PQgetvalue(res, i, 2);
    li.reflection_score = std::stod(PQgetvalue(res, i, 3));
    li.feedback_score   = std::stod(PQgetvalue(res, i, 4));
    li.tokens_used      = static_cast<size_t>(std::stoull(PQgetvalue(res, i, 5)));
    li.timestamp        = std::stoll(PQgetvalue(res, i, 6));
    interactions.push_back(std::move(li));
  }
  return interactions;
}

// ---------------------------------------------------------------------------
// Learning: store_learning_review
// ---------------------------------------------------------------------------
void PostgresBackend::store_learning_review(const LearningReview& review)
{
  const char* sql =
      "INSERT INTO neam_learning_reviews "
      "(agent_name, strategy, interactions_reviewed, avg_reflection_score, "
      "lessons_json, prompt_addendum, timestamp) "
      "VALUES ($1, $2, $3, $4, $5, $6, $7)";

  std::string reviewed = std::to_string(review.interactions_reviewed);
  std::string avg      = std::to_string(review.avg_reflection_score);
  std::string ts       = std::to_string(review.timestamp);

  const char* params[] = {review.agent_name.c_str(), review.strategy.c_str(),
                          reviewed.c_str(), avg.c_str(), review.lessons_json.c_str(),
                          review.prompt_addendum.c_str(), ts.c_str()};

  PGResultGuard res(PQexecParams(conn_, sql, 7, nullptr, params, nullptr, nullptr, 0));
  check_result(conn_, res, PGRES_COMMAND_OK);
}

// ---------------------------------------------------------------------------
// Evolution: store_prompt_version
// ---------------------------------------------------------------------------
void PostgresBackend::store_prompt_version(const PromptVersion& version)
{
  const char* sql =
      "INSERT INTO neam_prompt_evolution "
      "(agent_name, version, original_prompt, evolved_prompt, reasoning, status, timestamp) "
      "VALUES ($1, $2, $3, $4, $5, $6, $7)";

  std::string ver = std::to_string(version.version);
  std::string ts  = std::to_string(version.timestamp);

  const char* params[] = {version.agent_name.c_str(), ver.c_str(),
                          version.original_prompt.c_str(), version.evolved_prompt.c_str(),
                          version.reasoning.c_str(), version.status.c_str(), ts.c_str()};

  PGResultGuard res(PQexecParams(conn_, sql, 7, nullptr, params, nullptr, nullptr, 0));
  check_result(conn_, res, PGRES_COMMAND_OK);
}

// ---------------------------------------------------------------------------
// Evolution: load_prompt_history
// ---------------------------------------------------------------------------
std::vector<PromptVersion> PostgresBackend::load_prompt_history(const std::string& agent)
{
  const char* sql =
      "SELECT agent_name, version, original_prompt, evolved_prompt, reasoning, "
      "status, timestamp FROM neam_prompt_evolution "
      "WHERE agent_name = $1 ORDER BY version";

  const char* params[] = {agent.c_str()};

  PGResultGuard res(PQexecParams(conn_, sql, 1, nullptr, params, nullptr, nullptr, 0));
  check_result(conn_, res, PGRES_TUPLES_OK);

  const int rows = PQntuples(res);
  std::vector<PromptVersion> versions;
  versions.reserve(static_cast<size_t>(rows));

  for (int i = 0; i < rows; ++i)
  {
    PromptVersion pv;
    pv.agent_name      = PQgetvalue(res, i, 0);
    pv.version         = std::stoi(PQgetvalue(res, i, 1));
    pv.original_prompt = PQgetvalue(res, i, 2);
    pv.evolved_prompt  = PQgetvalue(res, i, 3);
    pv.reasoning       = PQgetvalue(res, i, 4);
    pv.status          = PQgetvalue(res, i, 5);
    pv.timestamp       = std::stoll(PQgetvalue(res, i, 6));
    versions.push_back(std::move(pv));
  }
  return versions;
}

// ---------------------------------------------------------------------------
// Evolution: try_update_prompt (optimistic concurrency via version check)
// ---------------------------------------------------------------------------
bool PostgresBackend::try_update_prompt(const std::string& agent, int expected_version,
                                         const PromptVersion& new_version)
{
  // Verify the current maximum version matches the expected value
  {
    const char* check_sql =
        "SELECT COALESCE(MAX(version), 0) FROM neam_prompt_evolution "
        "WHERE agent_name = $1";

    const char* params[] = {agent.c_str()};

    PGResultGuard res(PQexecParams(conn_, check_sql, 1, nullptr, params, nullptr, nullptr, 0));
    check_result(conn_, res, PGRES_TUPLES_OK);

    if (PQntuples(res) == 0)
    {
      return false;
    }

    int current_version = std::stoi(PQgetvalue(res, 0, 0));
    if (current_version != expected_version)
    {
      return false;
    }
  }

  // Insert the new version
  store_prompt_version(new_version);
  return true;
}

// ---------------------------------------------------------------------------
// Autonomous: store_autonomous_action
// ---------------------------------------------------------------------------
void PostgresBackend::store_autonomous_action(const AutonomousAction& action)
{
  const char* sql =
      "INSERT INTO neam_autonomous_actions "
      "(agent_name, trigger_type, action_taken, tokens_used, timestamp) "
      "VALUES ($1, $2, $3, $4, $5)";

  std::string tok = std::to_string(action.tokens_used);
  std::string ts  = std::to_string(action.timestamp);

  const char* params[] = {action.agent_name.c_str(), action.trigger_type.c_str(),
                          action.action_taken.c_str(), tok.c_str(), ts.c_str()};

  PGResultGuard res(PQexecParams(conn_, sql, 5, nullptr, params, nullptr, nullptr, 0));
  check_result(conn_, res, PGRES_COMMAND_OK);
}

// ---------------------------------------------------------------------------
// Autonomous: load_budget
// ---------------------------------------------------------------------------
DailyBudget PostgresBackend::load_budget(const std::string& agent, const std::string& date)
{
  const char* sql =
      "SELECT calls_used, tokens_used, cost_used FROM neam_autonomous_budgets "
      "WHERE agent_name = $1 AND date = $2";

  const char* params[] = {agent.c_str(), date.c_str()};

  PGResultGuard res(PQexecParams(conn_, sql, 2, nullptr, params, nullptr, nullptr, 0));
  check_result(conn_, res, PGRES_TUPLES_OK);

  DailyBudget budget;
  budget.date = date;

  if (PQntuples(res) > 0)
  {
    budget.calls_used  = static_cast<size_t>(std::stoull(PQgetvalue(res, 0, 0)));
    budget.tokens_used = static_cast<size_t>(std::stoull(PQgetvalue(res, 0, 1)));
    budget.cost_used   = std::stod(PQgetvalue(res, 0, 2));
  }

  return budget;
}

// ---------------------------------------------------------------------------
// Autonomous: update_budget (upsert)
// ---------------------------------------------------------------------------
void PostgresBackend::update_budget(const std::string& agent, const DailyBudget& budget)
{
  const char* sql =
      "INSERT INTO neam_autonomous_budgets (agent_name, date, calls_used, tokens_used, cost_used) "
      "VALUES ($1, $2, $3, $4, $5) "
      "ON CONFLICT (agent_name, date) DO UPDATE SET "
      "calls_used = EXCLUDED.calls_used, "
      "tokens_used = EXCLUDED.tokens_used, "
      "cost_used = EXCLUDED.cost_used";

  std::string calls = std::to_string(budget.calls_used);
  std::string tok   = std::to_string(budget.tokens_used);
  std::string cost  = std::to_string(budget.cost_used);

  const char* params[] = {agent.c_str(), budget.date.c_str(), calls.c_str(),
                          tok.c_str(), cost.c_str()};

  PGResultGuard res(PQexecParams(conn_, sql, 5, nullptr, params, nullptr, nullptr, 0));
  check_result(conn_, res, PGRES_COMMAND_OK);
}

// ---------------------------------------------------------------------------
// Distributed Locking: try_acquire_lock
// ---------------------------------------------------------------------------
bool PostgresBackend::try_acquire_lock(const std::string& lock_name,
                                        const std::string& holder_id,
                                        std::chrono::seconds ttl)
{
  // Attempt to insert. On conflict, only take the lock if it has expired.
  const char* sql =
      "INSERT INTO neam_distributed_locks (lock_name, holder_id, expires_at) "
      "VALUES ($1, $2, NOW() + ($3 || ' seconds')::INTERVAL) "
      "ON CONFLICT (lock_name) DO UPDATE SET "
      "holder_id = EXCLUDED.holder_id, "
      "acquired_at = NOW(), "
      "expires_at = EXCLUDED.expires_at "
      "WHERE neam_distributed_locks.expires_at < NOW()";

  std::string ttl_str = std::to_string(ttl.count());
  const char* params[] = {lock_name.c_str(), holder_id.c_str(), ttl_str.c_str()};

  PGResultGuard res(PQexecParams(conn_, sql, 3, nullptr, params, nullptr, nullptr, 0));
  check_result(conn_, res, PGRES_COMMAND_OK);

  // If a row was affected, the lock was acquired (either fresh insert or expired takeover)
  const char* affected = PQcmdTuples(res);
  return affected && affected[0] != '0';
}

// ---------------------------------------------------------------------------
// Distributed Locking: release_lock
// ---------------------------------------------------------------------------
void PostgresBackend::release_lock(const std::string& lock_name,
                                    const std::string& holder_id)
{
  const char* sql =
      "DELETE FROM neam_distributed_locks WHERE lock_name = $1 AND holder_id = $2";

  const char* params[] = {lock_name.c_str(), holder_id.c_str()};

  PGResultGuard res(PQexecParams(conn_, sql, 2, nullptr, params, nullptr, nullptr, 0));
  check_result(conn_, res, PGRES_COMMAND_OK);
}

// ---------------------------------------------------------------------------
// Distributed Locking: renew_lock
// ---------------------------------------------------------------------------
bool PostgresBackend::renew_lock(const std::string& lock_name,
                                  const std::string& holder_id,
                                  std::chrono::seconds ttl)
{
  const char* sql =
      "UPDATE neam_distributed_locks SET expires_at = NOW() + ($3 || ' seconds')::INTERVAL "
      "WHERE lock_name = $1 AND holder_id = $2";

  std::string ttl_str = std::to_string(ttl.count());
  const char* params[] = {lock_name.c_str(), holder_id.c_str(), ttl_str.c_str()};

  PGResultGuard res(PQexecParams(conn_, sql, 3, nullptr, params, nullptr, nullptr, 0));
  check_result(conn_, res, PGRES_COMMAND_OK);

  const char* affected = PQcmdTuples(res);
  return affected && affected[0] != '0';
}

}  // namespace neamc::vm

#endif  // NEAM_BACKEND_POSTGRES
