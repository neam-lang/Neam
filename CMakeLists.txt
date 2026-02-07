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
endif ()

find_package(CURL REQUIRED)
find_package(OpenSSL REQUIRED)

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
    NeamC/src/vm/memory.cpp
    NeamC/src/vm/object.cpp
    NeamC/src/vm/table.cpp
    NeamC/src/vm/native.cpp
    NeamC/src/vm/llm/http_client.cpp
    NeamC/src/vm/llm/message_codec.cpp
    NeamC/src/vm/llm/bedrock_adapter.cpp
    NeamC/src/vm/llm/ollama_adapter.cpp
    NeamC/src/vm/llm/openai_adapter.cpp
    NeamC/src/vm/llm/provider_factory.cpp
    NeamC/src/vm/knowledge.cpp
    NeamC/src/vm/value.cpp
    NeamC/src/vm/runtime_type.cpp
    NeamC/src/vm/bytecode.cpp
    NeamC/src/vm/vm.cpp
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
)

add_library(neamc_core STATIC ${NEAMC_CORE_SOURCES})

target_include_directories(neamc_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/NeamC/include
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
