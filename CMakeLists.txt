cmake_minimum_required(VERSION 3.16)

project(NeamC LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

FetchContent_Declare(
    tree_sitter
    URL https://github.com/tree-sitter/tree-sitter/archive/refs/tags/v0.22.6.tar.gz
)
FetchContent_MakeAvailable(tree_sitter)

FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.tar.gz
)
FetchContent_MakeAvailable(nlohmann_json)

FetchContent_Declare(
    nlohmann_json_schema_validator
    URL https://github.com/pboettch/json-schema-validator/archive/refs/tags/2.1.0.tar.gz
)
FetchContent_MakeAvailable(nlohmann_json_schema_validator)
if (TARGET nlohmann_json_schema_validator)
    set_target_properties(nlohmann_json_schema_validator PROPERTIES POSITION_INDEPENDENT_CODE ON)
endif ()

FetchContent_Declare(
    miniz
    URL https://github.com/richgel999/miniz/archive/refs/tags/3.0.2.tar.gz
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
)
FetchContent_Declare(
    lexbor
    URL https://github.com/lexbor/lexbor/archive/refs/tags/v2.2.0.tar.gz
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

find_package(CURL REQUIRED)

add_library(tree_sitter_lib STATIC
    ${tree_sitter_SOURCE_DIR}/lib/src/lib.c
)
set_target_properties(tree_sitter_lib PROPERTIES LINKER_LANGUAGE C POSITION_INDEPENDENT_CODE ON)
target_include_directories(tree_sitter_lib PUBLIC ${tree_sitter_SOURCE_DIR}/lib/include)

set(NEAMC_CORE_SOURCES
    NeamC/src/parser.cpp
    NeamC/src/tree_sitter_parser.cpp
    NeamC/src/compiler.cpp
    NeamC/src/pipeline.cpp
    NeamC/src/package/manifest.cpp
    NeamC/src/lsp/server.cpp
    NeamC/src/vm/memory.cpp
    NeamC/src/vm/object.cpp
    NeamC/src/vm/table.cpp
    NeamC/src/vm/native.cpp
    NeamC/src/vm/llm/http_client.cpp
    NeamC/src/vm/llm/message_codec.cpp
    NeamC/src/vm/llm/ollama_adapter.cpp
    NeamC/src/vm/llm/openai_adapter.cpp
    NeamC/src/vm/llm/provider_factory.cpp
    NeamC/src/vm/knowledge.cpp
    NeamC/src/vm/value.cpp
    NeamC/src/vm/bytecode.cpp
    NeamC/src/vm/vm.cpp
    NeamC/src/vm/trace.cpp
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
if (TARGET nlohmann_json_schema_validator)
    set(NEAM_JSON_SCHEMA_TARGET nlohmann_json_schema_validator)
elseif (TARGET nlohmann_json_schema_validator::nlohmann_json_schema_validator)
    set(NEAM_JSON_SCHEMA_TARGET nlohmann_json_schema_validator::nlohmann_json_schema_validator)
endif ()
