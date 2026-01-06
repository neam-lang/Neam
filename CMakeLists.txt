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
    NeamC/src/vm/memory.cpp
    NeamC/src/vm/object.cpp
    NeamC/src/vm/table.cpp
    NeamC/src/vm/native.cpp
    NeamC/src/vm/value.cpp
    NeamC/src/vm/bytecode.cpp
    NeamC/src/vm/vm.cpp
)

add_library(neamc_core STATIC ${NEAMC_CORE_SOURCES})

target_include_directories(neamc_core
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/NeamC/include
)

target_link_libraries(neamc_core
    PUBLIC
        tree_sitter_lib
)

add_library(libneam SHARED ${NEAMC_CORE_SOURCES})
target_link_libraries(libneam PUBLIC tree_sitter_lib)
target_include_directories(libneam
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/NeamC/include
)

add_executable(neamc
    NeamC/src/neamc/main.cpp
)
target_link_libraries(neamc PRIVATE neamc_core)

add_executable(neam
    NeamC/src/neam_run.cpp
)
target_link_libraries(neam PRIVATE neamc_core)

enable_testing()

add_executable(vm_test
    tests/vm_test.cpp
)
target_link_libraries(vm_test PRIVATE neamc_core)
add_test(NAME vm_test COMMAND vm_test)

add_executable(compiler_test
    tests/compiler_test.cpp
)
target_link_libraries(compiler_test PRIVATE neamc_core)
add_test(NAME compiler_test COMMAND compiler_test)
