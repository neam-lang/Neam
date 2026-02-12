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
    NeamC/src/multicloud/cost_router.cpp
)

add_library(neamc_core STATIC ${NEAMC_CORE_SOURCES})

target_include_directories(neamc_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/NeamC/include
        ${CMAKE_BINARY_DIR}/include
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
# Install targets (v0.6.5)
# ============================================================================

include(GNUInstallDirs)

# Executables
install(TARGETS neamc neam neam-cli neam-pkg neam-api neam-lsp neam-dap neam-gym
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
