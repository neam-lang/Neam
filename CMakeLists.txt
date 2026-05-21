cmake_minimum_required(VERSION 3.16)

# Default to Release if not specified
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    message(STATUS "Setting build type to 'Release' as none was specified")
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
    set_property(CACHE CMAKE_BUILD_TYPE PROPERTY
        STRINGS "Debug" "Release" "RelWithDebInfo" "MinSizeRel")
endif()

# Set CMake policies for compatibility
if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()

# Allow older cmake_minimum_required in fetched projects
set(CMAKE_POLICY_VERSION_MINIMUM 3.5 CACHE STRING "Minimum CMake policy version")

# Read version from VERSION file
file(STRINGS "${CMAKE_SOURCE_DIR}/VERSION" NEAM_VERSION)
project(NeamC VERSION ${NEAM_VERSION} LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Generate version header from template
configure_file(
    "${CMAKE_SOURCE_DIR}/NeamC/include/neamc/version.hpp.in"
    "${CMAKE_BINARY_DIR}/include/neamc/version.hpp"
)

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

FetchContent_Declare(
    usearch
    URL https://github.com/unum-cloud/usearch/archive/refs/tags/v2.6.0.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
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

# Disable benchmark tests for cross-compilation (avoids -Werror issues)
if(CMAKE_CROSSCOMPILING)
    set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
endif()

FetchContent_GetProperties(usearch)
if (NOT usearch_POPULATED)
    FetchContent_Populate(usearch)
endif ()
if (NOT TARGET usearch)
    add_library(usearch INTERFACE)
    target_include_directories(usearch INTERFACE ${usearch_SOURCE_DIR}/include)
endif ()

FetchContent_GetProperties(lexbor)
if (NOT lexbor_POPULATED)
    FetchContent_Populate(lexbor)
endif ()
if (NOT TARGET lexbor)
    add_library(lexbor INTERFACE)
    target_include_directories(lexbor INTERFACE ${lexbor_SOURCE_DIR}/source)
endif ()

FetchContent_MakeAvailable(googletest benchmark)
if (TARGET benchmark)
    set_target_properties(benchmark PROPERTIES POSITION_INDEPENDENT_CODE ON)
    if(CMAKE_CROSSCOMPILING)
        target_compile_options(benchmark PRIVATE -Wno-switch)
    endif()
endif ()

find_package(CURL REQUIRED)

# For MinGW cross-compilation, FindOpenSSL can't locate LibreSSL in the
# deps bundle. Create the imported target manually from toolchain hints.
if(CMAKE_CROSSCOMPILING AND DEFINED OPENSSL_CRYPTO_LIBRARY)
    if(NOT TARGET OpenSSL::Crypto)
        add_library(OpenSSL::Crypto STATIC IMPORTED)
        set_target_properties(OpenSSL::Crypto PROPERTIES
            IMPORTED_LOCATION "${OPENSSL_CRYPTO_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_INCLUDE_DIR}"
        )
    endif()
    if(NOT TARGET OpenSSL::SSL)
        add_library(OpenSSL::SSL STATIC IMPORTED)
        set_target_properties(OpenSSL::SSL PROPERTIES
            IMPORTED_LOCATION "${OPENSSL_SSL_LIBRARY}"
            INTERFACE_INCLUDE_DIRECTORIES "${OPENSSL_INCLUDE_DIR}"
        )
    endif()
else()
    find_package(OpenSSL REQUIRED)
endif()

add_library(tree_sitter_lib STATIC
    ${tree_sitter_SOURCE_DIR}/lib/src/lib.c
)
set_target_properties(tree_sitter_lib PROPERTIES LINKER_LANGUAGE C POSITION_INDEPENDENT_CODE ON)
target_include_directories(tree_sitter_lib PUBLIC ${tree_sitter_SOURCE_DIR}/lib/include)
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
    NeamC/src/lsp/server.cpp
    NeamC/src/project_manifest.cpp
    NeamC/src/deploy/aws_lambda.cpp
    NeamC/src/deploy/docker.cpp
    NeamC/src/deploy/gcp_cloudrun.cpp
    NeamC/src/deploy/helm.cpp
    NeamC/src/deploy/kubernetes.cpp
    NeamC/src/deploy/terraform.cpp
    NeamC/src/deploy/deploy_scripts.cpp
    NeamC/src/security/audit_log.cpp
    NeamC/src/security/tool_policy.cpp
    NeamC/src/security/injection_scanner.cpp
    NeamC/src/security/network_policy.cpp
    NeamC/src/security/credential_manager.cpp
    NeamC/src/security/rate_limiter.cpp
    NeamC/src/security/behavioral_monitor.cpp
    NeamC/src/security/human_in_the_loop.cpp
    NeamC/src/vm/memory.cpp
    NeamC/src/vm/object.cpp
    NeamC/src/vm/external_skill.cpp
    NeamC/src/vm/mcp_client.cpp
    NeamC/src/vm/table.cpp
    NeamC/src/vm/native.cpp
    NeamC/src/vm/llm/http_client.cpp
    NeamC/src/vm/llm/message_codec.cpp
    NeamC/src/vm/llm/bedrock_adapter.cpp
    NeamC/src/vm/llm/ollama_adapter.cpp
    NeamC/src/vm/llm/openai_adapter.cpp
    NeamC/src/vm/llm/provider_factory.cpp
    NeamC/src/vm/knowledge.cpp
    NeamC/src/vm/struct_type.cpp
    NeamC/src/vm/sealed_type.cpp
    NeamC/src/vm/value.cpp
    NeamC/src/vm/value_hash.cpp
    NeamC/src/vm/runtime_type.cpp
    NeamC/src/vm/bytecode.cpp
    NeamC/src/vm/bytecode_cache.cpp
    NeamC/src/vm/vm.cpp
    NeamC/src/vm/vm_pool.cpp
    NeamC/src/vm/metrics.cpp
    NeamC/src/vm/session_manager.cpp
    NeamC/src/vm/harness_types.cpp
    NeamC/src/vm/handoff_runtime.cpp
    NeamC/src/vm/harness_runtime.cpp
    NeamC/src/vm/belief_runtime.cpp           # v1.5 NeamEvolve P0
    NeamC/src/vm/evolve_agent_runtime.cpp     # v1.5 NeamEvolve P0
    NeamC/src/vm/skill_library_runtime.cpp    # v1.5 NeamEvolve P0
    NeamC/src/vm/curriculum_runtime.cpp       # v1.5 NeamEvolve P1
    NeamC/src/vm/design_runtime.cpp           # v1.5 NeamEvolve P2
    NeamC/src/vm/process_runtime.cpp          # v1.6 NeamMesh shim (≤350 LOC by ADR-003)
    NeamC/src/vm/process_persistence.cpp      # v1.6 NeamMesh file-backed persistence
    NeamC/src/vm/hitl_runtime.cpp             # v1.6 NeamMesh HITL resume (Phase F)
    NeamC/src/vm/context_builder.cpp
    NeamC/src/vm/compaction.cpp
    NeamC/src/vm/memory_index.cpp
    NeamC/src/vm/dag_executor.cpp
    NeamC/src/vm/forge_loop.cpp
    NeamC/src/vm/lane_queue.cpp
    NeamC/src/vm/channels/cli_channel.cpp
    NeamC/src/vm/channels/http_channel.cpp
    NeamC/src/vm/schedulable_runtime.cpp
    NeamC/src/vm/data_agent_types.cpp
    NeamC/src/vm/etl_agent_types.cpp
    NeamC/src/vm/migration_types.cpp
    NeamC/src/vm/dataops_types.cpp
    NeamC/src/vm/governance_types.cpp
    NeamC/src/vm/modeling_types.cpp
    NeamC/src/vm/analyst_types.cpp
    NeamC/src/vm/deploy_types.cpp
    NeamC/src/vm/datascientist_types.cpp
    NeamC/src/vm/causal_types.cpp
    NeamC/src/vm/mlops_types.cpp
    NeamC/src/vm/databa_types.cpp
    NeamC/src/vm/datatest_types.cpp
    NeamC/src/vm/dio_types.cpp
    NeamC/src/vm/security_v10_types.cpp
    NeamC/src/vm/neamos_types.cpp
    NeamC/src/vm/prod_types.cpp
    NeamC/src/vm/lab_types.cpp
    NeamC/src/vm/migration/audit_trail.cpp
    NeamC/src/vm/migration/schema_translator.cpp
    NeamC/src/vm/migration/data_movement.cpp
    NeamC/src/vm/migration/reconciliation.cpp
    NeamC/src/vm/migration/self_healing.cpp
    NeamC/src/vm/migration/cdc_manager.cpp
    NeamC/src/vm/migration/cutover_manager.cpp
    NeamC/src/vm/migration/security_migrator.cpp
    NeamC/src/vm/migration/sp_translator.cpp
    NeamC/src/vm/trace.cpp
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
    NeamC/src/multicloud/cost_router.cpp
)

add_library(neamc_core STATIC ${NEAMC_CORE_SOURCES})

target_include_directories(neamc_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/NeamC/include
        ${CMAKE_BINARY_DIR}/include
    PRIVATE
        ${CMAKE_CURRENT_SOURCE_DIR}/NeamC/src/vm/migration
)

target_link_libraries(neamc_core
    PUBLIC
        tree_sitter_lib
        CURL::libcurl
        OpenSSL::Crypto
        nlohmann_json::nlohmann_json
        nlohmann_json_schema_validator
        usearch
        lexbor
        miniz
)
if (DEFINED nlohmann_json_schema_validator_SOURCE_DIR)
    target_include_directories(neamc_core PUBLIC ${nlohmann_json_schema_validator_SOURCE_DIR}/src)
endif ()

# ============================================================================
# Static curl/SSL deps for MinGW cross-compilation (v0.6.7)
# When cross-compiling with static libcurl, transitive deps must be linked.
# ============================================================================
if(DEFINED CURL_STATIC_DEPS)
    target_link_libraries(neamc_core PUBLIC ${CURL_STATIC_DEPS})
endif()

# ============================================================================
# Compiler warnings (v0.6.5)
# ============================================================================

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
    set(NEAM_WARNING_FLAGS
        -Wall
        -Wextra
        -Wpedantic
        -Wno-unused-parameter
        -Wno-unused-private-field
    )
    target_compile_options(neamc_core PRIVATE ${NEAM_WARNING_FLAGS})
elseif(MSVC)
    target_compile_options(neamc_core PRIVATE /W4)
endif()

# Optional: Sanitizers for debug builds
option(NEAM_ENABLE_ASAN "Enable AddressSanitizer" OFF)
option(NEAM_ENABLE_UBSAN "Enable UndefinedBehaviorSanitizer" OFF)

if(NEAM_ENABLE_ASAN)
    target_compile_options(neamc_core PUBLIC -fsanitize=address -fno-omit-frame-pointer)
    target_link_options(neamc_core PUBLIC -fsanitize=address)
endif()

if(NEAM_ENABLE_UBSAN)
    target_compile_options(neamc_core PUBLIC -fsanitize=undefined)
    target_link_options(neamc_core PUBLIC -fsanitize=undefined)
endif()

# Shared library is not built during MinGW cross-compilation
if(NOT CMAKE_CROSSCOMPILING)
    add_library(libneam SHARED ${NEAMC_CORE_SOURCES})
    target_link_libraries(libneam PUBLIC tree_sitter_lib CURL::libcurl nlohmann_json::nlohmann_json
        nlohmann_json_schema_validator
        OpenSSL::Crypto
        usearch
        lexbor
        miniz
    )
    if (DEFINED nlohmann_json_schema_validator_SOURCE_DIR)
        target_include_directories(libneam PUBLIC ${nlohmann_json_schema_validator_SOURCE_DIR}/src)
    endif ()
    target_include_directories(libneam
        PUBLIC
            ${CMAKE_CURRENT_SOURCE_DIR}/NeamC/include
            ${CMAKE_BINARY_DIR}/include
    )
    if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang|AppleClang")
        target_compile_options(libneam PRIVATE ${NEAM_WARNING_FLAGS})
    elseif(MSVC)
        target_compile_options(libneam PRIVATE /W4)
    endif()
endif()

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

# v0.7.2: Lambda custom runtime
add_executable(neam-lambda
    NeamC/src/neam_lambda.cpp
)
target_link_libraries(neam-lambda PRIVATE neamc_core)
target_link_libraries(neam-lambda PRIVATE nlohmann_json_schema_validator)

# v0.8 Phase 9: Forge CLI
add_executable(neam-forge
    NeamC/src/neam_forge.cpp
)
target_link_libraries(neam-forge PRIVATE neamc_core)
target_link_libraries(neam-forge PRIVATE nlohmann_json_schema_validator)

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

add_executable(hotvm_bench
    tests/benchmark/hotvm_bench.cpp
)
target_link_libraries(hotvm_bench PRIVATE neamc_core)

# v0.7.2: Memory benchmark with Google Benchmark
add_executable(hotvm_memory_bench
    tests/benchmark/hotvm_memory_bench.cpp
)
target_link_libraries(hotvm_memory_bench PRIVATE neamc_core benchmark::benchmark)

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

# ============================================================================
# v0.6.5 Test Targets
# ============================================================================

add_executable(llm_adapter_test
    tests/llm/adapter_test.cpp
)
target_link_libraries(llm_adapter_test PRIVATE neamc_core gtest_main)
add_test(NAME llm_adapter_test COMMAND llm_adapter_test)

add_executable(http_client_test
    tests/llm/http_client_test.cpp
)
target_link_libraries(http_client_test PRIVATE neamc_core gtest_main)
add_test(NAME http_client_test COMMAND http_client_test)

add_executable(package_manager_test
    tests/pkg/package_manager_test.cpp
    NeamC/src/pkg/installer.cpp
    NeamC/src/pkg/registry.cpp
)
target_link_libraries(package_manager_test PRIVATE neamc_core gtest_main)
add_test(NAME package_manager_test COMMAND package_manager_test)

add_executable(compile_run_test
    tests/integration/compile_run_test.cpp
)
target_link_libraries(compile_run_test PRIVATE neamc_core gtest_main)
add_test(NAME compile_run_test COMMAND compile_run_test)

# ============================================================================
# v0.6.8 Test Targets
# ============================================================================

add_executable(v068_test
    tests/unit/v068_test.cpp
)
target_link_libraries(v068_test PRIVATE neamc_core gtest_main)
add_test(NAME v068_test COMMAND v068_test)

# ============================================================================
# v0.6.9 Test Targets
# ============================================================================

add_executable(v069_test
    tests/unit/v069_test.cpp
)
target_link_libraries(v069_test PRIVATE neamc_core gtest_main)
add_test(NAME v069_test COMMAND v069_test)

# ============================================================================
# v0.7.0 Test Targets
# ============================================================================

add_executable(v070_test
    tests/unit/v070_test.cpp
)
target_link_libraries(v070_test PRIVATE neamc_core gtest_main)
add_test(NAME v070_test COMMAND v070_test)

# ============================================================================
# v0.7.2 Test Targets
# ============================================================================

add_executable(v072_phase2_test
    tests/unit/v072_phase2_test.cpp
)
target_link_libraries(v072_phase2_test PRIVATE neamc_core gtest_main)
add_test(NAME v072_phase2_test COMMAND v072_phase2_test)

add_executable(v072_manifest_agents_test
    tests/unit/v072_manifest_agents_test.cpp
)
target_link_libraries(v072_manifest_agents_test PRIVATE neamc_core gtest_main)
add_test(NAME v072_manifest_agents_test COMMAND v072_manifest_agents_test)

# ============================================================================
# v0.9 Test Targets
# ============================================================================

add_executable(v090_data_agent_test
    tests/unit/v090_data_agent_test.cpp
)
target_link_libraries(v090_data_agent_test PRIVATE neamc_core gtest_main)
add_test(NAME v090_data_agent_test COMMAND v090_data_agent_test)

# ============================================================================
# v0.9.1 Test Targets
# ============================================================================

add_executable(v091_etl_agent_test
    tests/unit/v091_etl_agent_test.cpp
)
target_link_libraries(v091_etl_agent_test PRIVATE neamc_core gtest_main)
add_test(NAME v091_etl_agent_test COMMAND v091_etl_agent_test)

# ============================================================================
# v0.9.2 Test Targets
# ============================================================================

add_executable(v092_migration_agent_test
    tests/unit/v092_migration_agent_test.cpp
)
target_link_libraries(v092_migration_agent_test PRIVATE neamc_core gtest_main)
add_test(NAME v092_migration_agent_test COMMAND v092_migration_agent_test)

# ============================================================================
# v0.9.3 Test Targets
# ============================================================================

add_executable(v093_dataops_agent_test
    tests/unit/v093_dataops_agent_test.cpp
)
target_link_libraries(v093_dataops_agent_test PRIVATE neamc_core gtest_main)
add_test(NAME v093_dataops_agent_test COMMAND v093_dataops_agent_test)

# ============================================================================
# v0.9.4 Test Targets
# ============================================================================

add_executable(v094_governance_agent_test
    tests/unit/v094_governance_agent_test.cpp
)
target_link_libraries(v094_governance_agent_test PRIVATE neamc_core gtest_main)
add_test(NAME v094_governance_agent_test COMMAND v094_governance_agent_test)

# ============================================================================
# v0.9.5 Modeling Agent Unit Tests
# ============================================================================

add_executable(v095_modeling_agent_test
    tests/unit/v095_modeling_agent_test.cpp
)
target_link_libraries(v095_modeling_agent_test PRIVATE neamc_core gtest_main)
add_test(NAME v095_modeling_agent_test COMMAND v095_modeling_agent_test)

# ============================================================================
# v0.9.8–v0.9.9 Intelligence Agent Tests
# ============================================================================

add_executable(v098_datascientist_agent_test
    tests/unit/v098_datascientist_agent_test.cpp
)
target_link_libraries(v098_datascientist_agent_test PRIVATE neamc_core gtest_main)
add_test(NAME v098_datascientist_agent_test COMMAND v098_datascientist_agent_test)

add_executable(v0981_causal_agent_test
    tests/unit/v0981_causal_agent_test.cpp
)
target_link_libraries(v0981_causal_agent_test PRIVATE neamc_core gtest_main)
add_test(NAME v0981_causal_agent_test COMMAND v0981_causal_agent_test)

add_executable(v0982_mlops_agent_test
    tests/unit/v0982_mlops_agent_test.cpp
)
target_link_libraries(v0982_mlops_agent_test PRIVATE neamc_core gtest_main)
add_test(NAME v0982_mlops_agent_test COMMAND v0982_mlops_agent_test)

add_executable(v0983_databa_agent_test
    tests/unit/v0983_databa_agent_test.cpp
)
target_link_libraries(v0983_databa_agent_test PRIVATE neamc_core gtest_main)
add_test(NAME v0983_databa_agent_test COMMAND v0983_databa_agent_test)

add_executable(v0984_datatest_agent_test
    tests/unit/v0984_datatest_agent_test.cpp
)
target_link_libraries(v0984_datatest_agent_test PRIVATE neamc_core gtest_main)
add_test(NAME v0984_datatest_agent_test COMMAND v0984_datatest_agent_test)

add_executable(v099_dio_agent_test
    tests/unit/v099_dio_agent_test.cpp
)
target_link_libraries(v099_dio_agent_test PRIVATE neamc_core gtest_main)
add_test(NAME v099_dio_agent_test COMMAND v099_dio_agent_test)

# v1.0: Security, Cloud, RAG, Evaluation & Special Agents tests
add_executable(v100_security_cloud_eval_test
    tests/unit/v100_security_cloud_eval_test.cpp
)
target_link_libraries(v100_security_cloud_eval_test PRIVATE neamc_core gtest_main)
add_test(NAME v100_security_cloud_eval_test COMMAND v100_security_cloud_eval_test)

# v1.1: NeamOS Foundation tests (knowledge cards, personas, governance, adapters, blueprints, new agents)
add_executable(v110_neamos_foundation_test
    tests/unit/v110_neamos_foundation_test.cpp
)
target_link_libraries(v110_neamos_foundation_test PRIVATE neamc_core gtest_main)
add_test(NAME v110_neamos_foundation_test COMMAND v110_neamos_foundation_test)

# v1.2: NeamProd tests (plugins, sessions, evaluation, artifacts, streaming, interop)
add_executable(v120_prod_readiness_test
    tests/unit/v120_prod_readiness_test.cpp
)
target_link_libraries(v120_prod_readiness_test PRIVATE neamc_core gtest_main)
add_test(NAME v120_prod_readiness_test COMMAND v120_prod_readiness_test)

# v1.3: NeamLab tests (auto research agent, program, experiment_loop, metric_extractor)
add_executable(v130_research_agent_test
    tests/unit/v130_research_agent_test.cpp
)
target_link_libraries(v130_research_agent_test PRIVATE neamc_core gtest_main)
add_test(NAME v130_research_agent_test COMMAND v130_research_agent_test)

# v1.4: NeamWiki tests (compiled llm-wiki: wiki, wiki agent, page schema, ops)
add_executable(v140_wiki_test
    tests/unit/v140_wiki_test.cpp
)
target_link_libraries(v140_wiki_test PRIVATE neamc_core gtest_main)
add_test(NAME v140_wiki_test COMMAND v140_wiki_test)

# v1.4.5: NeamHarness tests (Phase 0 — harness declaration parse/compile/VM dispatch)
add_executable(v145_harness_test
    tests/unit/v145_harness_test.cpp
)
target_link_libraries(v145_harness_test PRIVATE neamc_core gtest_main)
add_test(NAME v145_harness_test COMMAND v145_harness_test)

# v1.5: NeamEvolve tests (Phase A–F — evolve agent + belief)
add_executable(v15_evolve_test
    tests/unit/v15_evolve_test.cpp
)
target_link_libraries(v15_evolve_test PRIVATE neamc_core gtest_main)
add_test(NAME v15_evolve_test COMMAND v15_evolve_test)

# v1.6: NeamMesh tests (Phases A–H — process / task / decision / event / pool + HITL)
add_executable(v16_process_test
    tests/unit/v16_process_test.cpp
)
target_link_libraries(v16_process_test PRIVATE neamc_core gtest_main)
add_test(NAME v16_process_test COMMAND v16_process_test)

# ============================================================================
# Full Suite Integration Test (v0.8.0 – v0.9.1)
# ============================================================================

add_executable(neam_full_suite_test
    tests/integration/neam_full_suite_test.cpp
)
target_link_libraries(neam_full_suite_test PRIVATE neamc_core gtest_main)
add_test(NAME neam_full_suite_test COMMAND neam_full_suite_test)

# ============================================================================
# Install targets (v0.6.5)
# ============================================================================

include(GNUInstallDirs)

# Executables
install(TARGETS neamc neam neam-cli neam-pkg neam-api neam-lambda neam-forge neam-lsp neam-dap neam-gym
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Core static library
install(TARGETS neamc_core
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})

# Shared library (not built during cross-compilation)
if(NOT CMAKE_CROSSCOMPILING)
    install(TARGETS libneam
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR})
endif()

# Headers
install(DIRECTORY ${CMAKE_SOURCE_DIR}/NeamC/include/neamc
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}
        FILES_MATCHING PATTERN "*.hpp" PATTERN "*.h")

# Generated version header
install(FILES ${CMAKE_BINARY_DIR}/include/neamc/version.hpp
        DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/neamc)

# Standard library (.neam files)
install(DIRECTORY ${CMAKE_SOURCE_DIR}/NeamC/stdlib/
        DESTINATION ${CMAKE_INSTALL_DATADIR}/neam/stdlib
        FILES_MATCHING PATTERN "*.neam")

# Set RPATH for installed binaries
set(CMAKE_INSTALL_RPATH "${CMAKE_INSTALL_PREFIX}/${CMAKE_INSTALL_LIBDIR}")
set(CMAKE_INSTALL_RPATH_USE_LINK_PATH TRUE)
