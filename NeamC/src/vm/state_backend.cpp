// SPDX-License-Identifier: Apache-2.0
//
// Neam Virtual Machine - State Backend Factory (v0.6.0)
//

#include "neamc/vm/state_backend.hpp"

#include "neamc/vm/sqlite_memory.hpp"

#ifdef NEAM_BACKEND_POSTGRES
#include "neamc/vm/state_postgres.hpp"
#endif

#ifdef NEAM_BACKEND_REDIS
#include "neamc/vm/state_redis.hpp"
#endif

#ifdef NEAM_BACKEND_AWS
#include "neamc/vm/state_dynamodb.hpp"
#endif

#ifdef NEAM_BACKEND_AZURE
#include "neamc/vm/state_cosmosdb.hpp"
#endif

#include <stdexcept>

namespace neamc::vm
{
std::unique_ptr<StateBackend> create_state_backend(const std::string& backend_type,
                                                    const std::string& connection_string)
{
  if (backend_type.empty() || backend_type == "sqlite")
  {
    std::string path = connection_string.empty() ? "neam_state.db" : connection_string;
    return std::make_unique<SqliteMemoryBackend>(path);
  }

#ifdef NEAM_BACKEND_POSTGRES
  if (backend_type == "postgres" || backend_type == "postgresql")
  {
    return std::make_unique<PostgresBackend>(connection_string);
  }
#endif

#ifdef NEAM_BACKEND_REDIS
  if (backend_type == "redis")
  {
    return std::make_unique<RedisBackend>(connection_string);
  }
#endif

#ifdef NEAM_BACKEND_AWS
  if (backend_type == "dynamodb")
  {
    // connection_string format: "table_name:region" or just "table_name"
    auto colon = connection_string.find(':');
    if (colon != std::string::npos)
    {
      return std::make_unique<DynamoDBBackend>(connection_string.substr(0, colon),
                                                connection_string.substr(colon + 1));
    }
    return std::make_unique<DynamoDBBackend>(connection_string, "us-east-1");
  }
#endif

#ifdef NEAM_BACKEND_AZURE
  if (backend_type == "cosmosdb")
  {
    // connection_string format: "endpoint:database:container"
    auto first = connection_string.find(':');
    auto second = connection_string.find(':', first + 1);
    if (first != std::string::npos && second != std::string::npos)
    {
      return std::make_unique<CosmosDBBackend>(connection_string.substr(0, first),
                                                connection_string.substr(first + 1, second - first - 1),
                                                connection_string.substr(second + 1));
    }
    throw std::runtime_error(
        "CosmosDB connection string must be 'endpoint:database:container'");
  }
#endif

  throw std::runtime_error("Unknown or unsupported state backend: " + backend_type +
                           ". Available backends: sqlite"
#ifdef NEAM_BACKEND_POSTGRES
                           ", postgres"
#endif
#ifdef NEAM_BACKEND_REDIS
                           ", redis"
#endif
#ifdef NEAM_BACKEND_AWS
                           ", dynamodb"
#endif
#ifdef NEAM_BACKEND_AZURE
                           ", cosmosdb"
#endif
  );
}
}  // namespace neamc::vm
