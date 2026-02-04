cmake_minimum_required(VERSION 3.16)

# Set CMake policies for compatibility
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

# Allow older cmake_minimum_required in fetched projects
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Minimum CMake policy version")

project(NeamC LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# HNSW vector index (uSearch) - disable for Windows cross-compilation
option(NEAM_USE_HNSW "Use uSearch HNSW index for fast vector search (disable for MinGW)" ON)
if (CMAKE_CROSSCOMPILING AND WIN32)
    set(NEAM_USE_HNSW OFF CACHE BOOL "" FORCE)
endif ()

include(FetchContent)

# Set default for DOWNLOAD_EXTRACT_TIMESTAMP
if(POLICY CMP0135)
    set(FETCHCONTENT_DOWNLOAD_EXTRACT_TIMESTAMP TRUE)
endif()

FetchContent_Declare(
    tree_sitter
    URL https://github.com/tree-sitter/tree-sitter/archive/refs/tags/v0.22.6.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(tree_sitter)

FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

# Use newer version of json-schema-validator compatible with modern CMake
FetchContent_Declare(
    nlohmann_json_schema_validator
    URL https://github.com/pboettch/json-schema-validator/archive/refs/tags/2.3.0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(nlohmann_json_schema_validator)
if (TARGET nlohmann_json_schema_validator)
    set_target_properties(nlohmann_json_schema_validator PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif ()

FetchContent_Declare(
    miniz
    URL https://github.com/richgel999/miniz/archive/refs/tags/3.0.2.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_GetProperties(miniz)
if (NOT miniz_POPULATED)
    FetchContent_Populate(miniz)
endif ()
if (NOT TARGET miniz)
    add_library(miniz STATIC
        ${miniz_SOURCE_DIR}/miniz.c
        ${miniz_SOURCE_DIR}/miniz_tdef.c
        ${miniz_SOURCE_DIR}/miniz_tinfl.c
        ${miniz_SOURCE_DIR}/miniz_zip.c
    )
    target_include_directories(miniz PUBLIC ${miniz_SOURCE_DIR})
    set(MINIZ_EXPORT_HEADER "${CMAKE_CURRENT_BINARY_DIR}/miniz_export.h")
    file(WRITE "${MINIZ_EXPORT_HEADER}" "#pragma once\n#define MINIZ_EXPORT\n")
    target_include_directories(miniz PUBLIC ${CMAKE_CURRENT_BINARY_DIR})
endif ()

if (NEAM_USE_HNSW)
    FetchContent_Declare(
        usearch
        URL https://github.com/unum-cloud/usearch/archive/refs/tags/v2.6.0.tar.gz
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
    )
endif ()
FetchContent_Declare(
    lexbor
    URL https://github.com/lexbor/lexbor/archive/refs/tags/v2.2.0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

FetchContent_Declare(
    benchmark
    URL https://github.com/google/benchmark/archive/refs/tags/v1.8.3.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)

if (NEAM_USE_HNSW)
    FetchContent_GetProperties(usearch)
    if (NOT usearch_POPULATED)
        FetchContent_Populate(usearch)
    endif ()
    if (NOT TARGET usearch)
        add_library(usearch INTERFACE)
        target_include_directories(usearch SYSTEM INTERFACE ${usearch_SOURCE_DIR}/include)
        # Fix uSearch v2.6.0 bug: index_->node_at_() should be index_.node_at_()
        # (index_ is a reference, not a pointer)
        file(READ "${usearch_SOURCE_DIR}/include/usearch/index.hpp" _usearch_index_content)
        string(REPLACE "index_->node_at_(slot()).key()" "index_.node_at_(slot()).key()" _usearch_index_content "${_usearch_index_content}")
        file(WRITE "${usearch_SOURCE_DIR}/include/usearch/index.hpp" "${_usearch_index_content}")
    endif ()
else ()
    # Provide a stub usearch target when HNSW is disabled
    if (NOT TARGET usearch)
        add_library(usearch INTERFACE)
    endif ()
endif ()

FetchContent_GetProperties(lexbor)
if (NOT lexbor_POPULATED)
    FetchContent_Populate(lexbor)
endif ()
if (NOT TARGET lexbor)
    add_library(lexbor INTERFACE)
    target_include_directories(lexbor INTERFACE ${lexbor_SOURCE_DIR}/source)
endif ()

# SQLite3 amalgamation
if (NOT TARGET sqlite3)
    add_library(sqlite3 STATIC deps/sqlite3/sqlite3.c)
    target_include_directories(sqlite3 PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/deps/sqlite3)
    set_target_properties(sqlite3 PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif ()

set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "Disable -Werror in benchmark" FORCE)
FetchContent_MakeAvailable(googletest benchmark)
if (TARGET benchmark)
    set_target_properties(benchmark PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif ()

find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)

add_library(tree_sitter_lib STATIC
    ${tree_sitter_SOURCE_DIR}/lib/src/lib.c
)
set_target_properties(tree_sitter_lib PROPERTIES LINKER_LANGUAGE C POSITION_INDEPENDENT_CODE ON)
target_include_directories(tree_sitter_lib PUBLIC ${tree_sitter_SOURCE_DIR}/lib/include)
# tree-sitter bundled ICU unicode headers are under lib/src/unicode/
# Add lib/src as private include so "unicode/umachine.h" resolves correctly
target_include_directories(tree_sitter_lib PRIVATE ${tree_sitter_SOURCE_DIR}/lib/src)

set(NEAMC_CORE_SOURCES
    NeamC/src/parser.cpp
    NeamC/src/tree_sitter_parser.cpp
    NeamC/src/compiler.cpp
    NeamC/src/pipeline.cpp
    NeamC/src/package/manifest.cpp
    NeamC/src/module/resolver.cpp
    NeamC/src/module/graph.cpp
    NeamC/src/module/visibility.cpp
    NeamC/src/module/compilation_unit.cpp
    NeamC/src/mcp/jsonrpc.cpp
    NeamC/src/mcp/stdio_transport.cpp
    NeamC/src/mcp/sse_transport.cpp
    NeamC/src/mcp/client.cpp
    NeamC/src/mcp/registry.cpp
    NeamC/src/lsp/server.cpp
    NeamC/src/project_manifest.cpp
    NeamC/src/deploy/aws_lambda.cpp
    NeamC/src/deploy/docker.cpp
    NeamC/src/deploy/gcp_cloudrun.cpp
    NeamC/src/deploy/helm.cpp
    NeamC/src/deploy/kubernetes.cpp
    NeamC/src/deploy/terraform.cpp
    NeamC/src/vm/memory.cpp
    NeamC/src/vm/object.cpp
    NeamC/src/vm/table.cpp
    NeamC/src/vm/native.cpp
    NeamC/src/vm/llm/http_client.cpp
    NeamC/src/vm/llm/message_codec.cpp
    NeamC/src/vm/llm/ollama_adapter.cpp
    NeamC/src/vm/llm/openai_adapter.cpp
    NeamC/src/vm/llm/provider_factory.cpp
    NeamC/src/vm/llm/embedding_provider.cpp
    NeamC/src/vm/llm/openai_embedding.cpp
    NeamC/src/vm/llm/anthropic_adapter.cpp
    NeamC/src/vm/llm/gemini_adapter.cpp
    NeamC/src/vm/llm/cost_table.cpp
    NeamC/src/vm/llm/ollama_embedding.cpp
    NeamC/src/vm/knowledge.cpp
    NeamC/src/vm/value.cpp
    NeamC/src/vm/runtime_type.cpp
    NeamC/src/vm/bytecode.cpp
    NeamC/src/vm/vm.cpp
    NeamC/src/vm/trace.cpp
    NeamC/src/vm/memory_backend.cpp
    NeamC/src/vm/sqlite_memory.cpp
    NeamC/src/vm/autonomous.cpp
    NeamC/src/runtime/executor.cpp
    NeamC/src/types/type_repr.cpp
    NeamC/src/types/constraints.cpp
    NeamC/src/types/unification.cpp
    NeamC/src/types/environment.cpp
    NeamC/src/types/inferencer.cpp
    NeamC/src/testing/assertions.cpp
    NeamC/src/testing/framework.cpp
    NeamC/src/testing/coverage.cpp
    NeamC/src/io/filesystem.cpp
    NeamC/src/io/temp.cpp
    NeamC/src/eval/graders.cpp
    NeamC/src/voice/stt_whisper.cpp
    NeamC/src/voice/stt_gemini.cpp
    NeamC/src/voice/stt_whisper_local.cpp
    NeamC/src/voice/tts_openai.cpp
    NeamC/src/voice/tts_gemini.cpp
    NeamC/src/voice/tts_kokoro.cpp
    NeamC/src/voice/tts_piper.cpp
    NeamC/src/voice/pipeline.cpp
    NeamC/src/voice/realtime.cpp
    NeamC/src/voice/realtime_openai.cpp
    NeamC/src/voice/realtime_gemini.cpp
    NeamC/src/voice/realtime_local.cpp
    NeamC/src/voice/audio_buffer.cpp
    NeamC/src/voice/event_loop.cpp
    NeamC/src/util/base64.cpp
    NeamC/src/vm/llm/websocket_client.cpp
    NeamC/src/vm/health_manager.cpp
    NeamC/src/vm/telemetry.cpp
    NeamC/src/vm/llm_gateway.cpp
    NeamC/src/vm/state_backend.cpp
    NeamC/src/vm/distributed_autonomous.cpp
    NeamC/src/vm/llm/azure_openai_adapter.cpp
    NeamC/src/deploy/azure_container_apps.cpp
    NeamC/src/deploy/azure_aks.cpp
    NeamC/src/deploy/aws_ecs.cpp
    NeamC/src/deploy/docker_push.cpp
    NeamC/src/config/cloud_config.cpp
    NeamC/src/config/secrets_resolver.cpp
    NeamC/src/api/http_server_pool.cpp
)

# v0.6.0 Cloud Backend Options
option(NEAM_BACKEND_POSTGRES "Enable PostgreSQL state backend (requires libpq)" OFF)
option(NEAM_BACKEND_REDIS "Enable Redis state backend (requires hiredis)" OFF)
option(NEAM_BACKEND_AWS "Enable AWS backends - DynamoDB, Bedrock (uses libcurl)" OFF)
option(NEAM_BACKEND_GCP "Enable GCP backends - Vertex AI (uses libcurl)" OFF)
option(NEAM_BACKEND_AZURE "Enable Azure backends - CosmosDB (uses libcurl)" OFF)

# Conditional backend sources
if(NEAM_BACKEND_POSTGRES)
    find_package(PostgreSQL REQUIRED)
    list(APPEND NEAMC_CORE_SOURCES NeamC/src/vm/state_postgres.cpp)
endif()

if(NEAM_BACKEND_REDIS)
    find_path(HIREDIS_INCLUDE_DIR hiredis/hiredis.h)
    find_library(HIREDIS_LIBRARY hiredis)
    if(NOT HIREDIS_INCLUDE_DIR OR NOT HIREDIS_LIBRARY)
        message(FATAL_ERROR "hiredis not found. Install hiredis or disable NEAM_BACKEND_REDIS")
    endif()
    list(APPEND NEAMC_CORE_SOURCES NeamC/src/vm/state_redis.cpp)
endif()

if(NEAM_BACKEND_AWS)
    list(APPEND NEAMC_CORE_SOURCES
        NeamC/src/vm/state_dynamodb.cpp
        NeamC/src/vm/llm/bedrock_adapter.cpp
        NeamC/src/util/aws_signer.cpp
    )
endif()

if(NEAM_BACKEND_GCP)
    list(APPEND NEAMC_CORE_SOURCES
        NeamC/src/vm/llm/vertex_adapter.cpp
        NeamC/src/util/gcp_auth.cpp
    )
endif()

if(NEAM_BACKEND_AZURE)
    list(APPEND NEAMC_CORE_SOURCES
        NeamC/src/vm/state_cosmosdb.cpp
        NeamC/src/util/azure_auth.cpp
    )
endif()

add_library(neamc_core STATIC ${NEAMC_CORE_SOURCES})

target_include_directories(neamc_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/NeamC/include
)
if (NEAM_USE_HNSW)
    target_compile_definitions(neamc_core PUBLIC NEAM_USE_HNSW=1)
endif ()

# v0.6.0 backend compile definitions
if(NEAM_BACKEND_POSTGRES)
    target_compile_definitions(neamc_core PUBLIC NEAM_BACKEND_POSTGRES=1)
    target_link_libraries(neamc_core PUBLIC PostgreSQL::PostgreSQL)
endif()
if(NEAM_BACKEND_REDIS)
    target_compile_definitions(neamc_core PUBLIC NEAM_BACKEND_REDIS=1)
    target_include_directories(neamc_core PUBLIC ${HIREDIS_INCLUDE_DIR})
    target_link_libraries(neamc_core PUBLIC ${HIREDIS_LIBRARY})
endif()
if(NEAM_BACKEND_AWS)
    target_compile_definitions(neamc_core PUBLIC NEAM_BACKEND_AWS=1)
endif()
if(NEAM_BACKEND_GCP)
    target_compile_definitions(neamc_core PUBLIC NEAM_BACKEND_GCP=1)
endif()
if(NEAM_BACKEND_AZURE)
    target_compile_definitions(neamc_core PUBLIC NEAM_BACKEND_AZURE=1)
endif()

target_link_libraries(neamc_core
    PUBLIC
        tree_sitter_lib
        CURL::libcurl
        OpenSSL::Crypto
        OpenSSL::SSL
        nlohmann_json::nlohmann_json
        nlohmann_json_schema_validator
        usearch
        lexbor
        miniz
        sqlite3
)
if (WIN32)
    # Windows static CURL requires additional dependencies
    set(CURL_WIN_DEPS_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deps/windows/curl-8.12.1_1-win64-mingw/lib")
    target_link_libraries(neamc_core PUBLIC
        ${CURL_WIN_DEPS_DIR}/libnghttp2.a
        ${CURL_WIN_DEPS_DIR}/libnghttp3.a
        ${CURL_WIN_DEPS_DIR}/libngtcp2.a
        ${CURL_WIN_DEPS_DIR}/libngtcp2_crypto_quictls.a
        ${CURL_WIN_DEPS_DIR}/libssh2.a
        ${CURL_WIN_DEPS_DIR}/libbrotlidec.a
        ${CURL_WIN_DEPS_DIR}/libbrotlicommon.a
        ${CURL_WIN_DEPS_DIR}/libzstd.a
        ${CURL_WIN_DEPS_DIR}/libpsl.a
        ${CURL_WIN_DEPS_DIR}/libz.a
        OpenSSL::SSL
        ws2_32 wldap32 crypt32 bcrypt
    )
endif ()
if (DEFINED nlohmann_json_schema_validator_SOURCE_DIR)
    target_include_directories(neamc_core PUBLIC ${nlohmann_json_schema_validator_SOURCE_DIR}/src)
endif ()

# Neam C API for language bindings
set(NEAM_C_API_SOURCES
    NeamC/src/neam_c_api.cpp
)

add_library(libneam SHARED ${NEAMC_CORE_SOURCES} ${NEAM_C_API_SOURCES})
set_target_properties(libneam PROPERTIES
    OUTPUT_NAME "neam"
    VERSION 0.6.2
    SOVERSION 0
    PUBLIC_HEADER "NeamC/include/neamc/neam_c_api.h"
)
target_compile_definitions(libneam PRIVATE NEAM_BUILDING_DLL)
if (WIN32)
    target_link_libraries(libneam PUBLIC
        -Wl,--start-group
        tree_sitter_lib CURL::libcurl nlohmann_json::nlohmann_json
        nlohmann_json_schema_validator
        OpenSSL::Crypto OpenSSL::SSL
        usearch lexbor miniz sqlite3
        ${CURL_WIN_DEPS_DIR}/libnghttp2.a
        ${CURL_WIN_DEPS_DIR}/libnghttp3.a
        ${CURL_WIN_DEPS_DIR}/libngtcp2.a
        ${CURL_WIN_DEPS_DIR}/libngtcp2_crypto_quictls.a
        ${CURL_WIN_DEPS_DIR}/libssh2.a
        ${CURL_WIN_DEPS_DIR}/libbrotlidec.a
        ${CURL_WIN_DEPS_DIR}/libbrotlicommon.a
        ${CURL_WIN_DEPS_DIR}/libzstd.a
        ${CURL_WIN_DEPS_DIR}/libpsl.a
        ${CURL_WIN_DEPS_DIR}/libz.a
        ws2_32 wldap32 crypt32 bcrypt
        -Wl,--end-group
    )
else ()
    target_link_libraries(libneam PUBLIC tree_sitter_lib CURL::libcurl nlohmann_json::nlohmann_json
        nlohmann_json_schema_validator
        OpenSSL::Crypto
        OpenSSL::SSL
        usearch
        lexbor
        sqlite3
        miniz
    )
endif ()
if (DEFINED nlohmann_json_schema_validator_SOURCE_DIR)
    target_include_directories(libneam PUBLIC ${nlohmann_json_schema_validator_SOURCE_DIR}/src)
endif ()
target_include_directories(libneam
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/NeamC/include
)

add_executable(neamc
    NeamC/src/neamc/main.cpp
)
target_link_libraries(neamc PRIVATE neamc_core)
target_link_libraries(neamc PRIVATE nlohmann_json_schema_validator)

add_executable(neam
    NeamC/src/neam_run.cpp
)
target_link_libraries(neam PRIVATE neamc_core)
target_link_libraries(neam PRIVATE nlohmann_json_schema_validator)

add_executable(neam-gym
    NeamC/src/tools/neam_gym.cpp
)
target_link_libraries(neam-gym PRIVATE neamc_core)
target_link_libraries(neam-gym PRIVATE nlohmann_json_schema_validator)

add_executable(neam-cli
    NeamC/src/neam_cli.cpp
)
target_link_libraries(neam-cli PRIVATE neamc_core)
target_link_libraries(neam-cli PRIVATE nlohmann_json_schema_validator)

add_executable(neam-lsp
    NeamC/src/lsp/main.cpp
)
target_link_libraries(neam-lsp PRIVATE neamc_core)
target_link_libraries(neam-lsp PRIVATE nlohmann_json_schema_validator)

add_executable(neam-dap
    NeamC/src/dap/main.cpp
)
target_link_libraries(neam-dap PRIVATE neamc_core)
target_link_libraries(neam-dap PRIVATE nlohmann_json_schema_validator)

add_executable(neam-api
    NeamC/src/neam_api.cpp
    NeamC/src/api/http_server.cpp
    NeamC/src/api/a2a_handler.cpp
)
target_link_libraries(neam-api PRIVATE neamc_core)
target_link_libraries(neam-api PRIVATE nlohmann_json_schema_validator)

add_executable(neam-pkg
    NeamC/src/neam_pkg.cpp
    NeamC/src/pkg/registry.cpp
    NeamC/src/pkg/installer.cpp
)
target_link_libraries(neam-pkg PRIVATE neamc_core)
target_link_libraries(neam-pkg PRIVATE nlohmann_json_schema_validator)

enable_testing()

add_executable(vm_test
    tests/vm_test.cpp
)
target_link_libraries(vm_test PRIVATE neamc_core)
target_link_libraries(vm_test PRIVATE nlohmann_json_schema_validator)
add_test(NAME vm_test COMMAND vm_test)

add_executable(compiler_test
    tests/compiler_test.cpp
)
target_link_libraries(compiler_test PRIVATE neamc_core)
target_link_libraries(compiler_test PRIVATE nlohmann_json_schema_validator)
add_test(NAME compiler_test COMMAND compiler_test)

add_executable(async_unit_test
    tests/unit/async_test.cpp
)
target_link_libraries(async_unit_test PRIVATE neamc_core gtest_main)
add_test(NAME async_unit_test COMMAND async_unit_test)

add_executable(runtime_executor_test
    tests/runtime/executor_test.cpp
)
target_link_libraries(runtime_executor_test PRIVATE neamc_core gtest_main)
add_test(NAME runtime_executor_test COMMAND runtime_executor_test)

add_executable(stdlib_list_test
    tests/stdlib/list_test.cpp
)
target_link_libraries(stdlib_list_test PRIVATE neamc_core gtest_main)
add_test(NAME stdlib_list_test COMMAND stdlib_list_test)

add_executable(stdlib_map_test
    tests/stdlib/map_test.cpp
)
target_link_libraries(stdlib_map_test PRIVATE neamc_core gtest_main)
add_test(NAME stdlib_map_test COMMAND stdlib_map_test)

add_executable(vm_benchmark
    tests/benchmark/vm_benchmark.cpp
)
target_link_libraries(vm_benchmark PRIVATE neamc_core benchmark::benchmark)

add_executable(type_unification_test
    tests/types/unification_test.cpp
)
target_link_libraries(type_unification_test PRIVATE neamc_core gtest_main)
add_test(NAME type_unification_test COMMAND type_unification_test)

add_executable(test_framework_test
    tests/testing/framework_test.cpp
)
target_link_libraries(test_framework_test PRIVATE neamc_core gtest_main)
add_test(NAME test_framework_test COMMAND test_framework_test)

add_executable(io_file_test
    tests/io/file_test.cpp
)
target_link_libraries(io_file_test PRIVATE neamc_core gtest_main)
add_test(NAME io_file_test COMMAND io_file_test)
if (TARGET nlohmann_json_schema_validator)
    set(NEAM_JSON_SCHEMA_TARGET nlohmann_json_schema_validator)
elseif (TARGET nlohmann_json_schema_validator::nlohmann_json_schema_validator)
    set(NEAM_JSON_SCHEMA_TARGET nlohmann_json_schema_validator::nlohmann_json_schema_validator)
endif ()

# --- v0.6.0 Extended Test Suite ---

add_executable(parser_test tests/parser/parser_test.cpp)
target_link_libraries(parser_test PRIVATE neamc_core gtest_main)
add_test(NAME parser_test COMMAND parser_test)

add_executable(compiler_extended_test tests/compiler/compiler_extended_test.cpp)
target_link_libraries(compiler_extended_test PRIVATE neamc_core gtest_main)
add_test(NAME compiler_extended_test COMMAND compiler_extended_test)

add_executable(vm_extended_test tests/vm/vm_extended_test.cpp)
target_link_libraries(vm_extended_test PRIVATE neamc_core gtest_main)
add_test(NAME vm_extended_test COMMAND vm_extended_test)

add_executable(c_api_test tests/capi/c_api_test.cpp)
target_link_libraries(c_api_test PRIVATE libneam gtest_main)
add_test(NAME c_api_test COMMAND c_api_test)

add_executable(agent_system_test tests/agent/agent_system_test.cpp)
target_link_libraries(agent_system_test PRIVATE neamc_core gtest_main)
add_test(NAME agent_system_test COMMAND agent_system_test)

add_executable(knowledge_test tests/knowledge/knowledge_test.cpp)
target_link_libraries(knowledge_test PRIVATE neamc_core gtest_main)
add_test(NAME knowledge_test COMMAND knowledge_test)

add_executable(mcp_test tests/mcp/mcp_test.cpp)
target_link_libraries(mcp_test PRIVATE neamc_core gtest_main)
add_test(NAME mcp_test COMMAND mcp_test)

add_executable(state_backend_test tests/state/state_backend_test.cpp)
target_link_libraries(state_backend_test PRIVATE neamc_core gtest_main)
add_test(NAME state_backend_test COMMAND state_backend_test)

add_executable(http_server_test
    tests/api/http_server_test.cpp
    NeamC/src/api/http_server.cpp
    NeamC/src/api/http_server_pool.cpp
    NeamC/src/api/a2a_handler.cpp)
target_link_libraries(http_server_test PRIVATE neamc_core gtest_main)
add_test(NAME http_server_test COMMAND http_server_test)

add_executable(voice_unit_test tests/voice/voice_unit_test.cpp)
target_link_libraries(voice_unit_test PRIVATE neamc_core gtest_main)
add_test(NAME voice_unit_test COMMAND voice_unit_test)

add_executable(autonomous_test tests/cognitive/autonomous_test.cpp)
target_link_libraries(autonomous_test PRIVATE neamc_core gtest_main)
add_test(NAME autonomous_test COMMAND autonomous_test)

add_executable(llm_gateway_test tests/gateway/llm_gateway_test.cpp)
target_link_libraries(llm_gateway_test PRIVATE neamc_core gtest_main)
add_test(NAME llm_gateway_test COMMAND llm_gateway_test)

add_executable(telemetry_test tests/telemetry/telemetry_test.cpp)
target_link_libraries(telemetry_test PRIVATE neamc_core gtest_main)
add_test(NAME telemetry_test COMMAND telemetry_test)

add_executable(health_manager_test tests/health/health_manager_test.cpp)
target_link_libraries(health_manager_test PRIVATE neamc_core gtest_main)
add_test(NAME health_manager_test COMMAND health_manager_test)

add_executable(cloud_config_test tests/config/cloud_config_test.cpp)
target_link_libraries(cloud_config_test PRIVATE neamc_core gtest_main)
add_test(NAME cloud_config_test COMMAND cloud_config_test)

add_executable(package_test
    tests/pkg/package_test.cpp
    NeamC/src/pkg/installer.cpp
    NeamC/src/pkg/registry.cpp)
target_link_libraries(package_test PRIVATE neamc_core gtest_main)
add_test(NAME package_test COMMAND package_test)

add_executable(deploy_test tests/deploy/deploy_test.cpp)
target_link_libraries(deploy_test PRIVATE neamc_core gtest_main)
add_test(NAME deploy_test COMMAND deploy_test)

add_executable(multi_provider_test tests/integration/multi_provider_test.cpp)
target_link_libraries(multi_provider_test PRIVATE neamc_core gtest_main)
add_test(NAME multi_provider_test COMMAND multi_provider_test)
