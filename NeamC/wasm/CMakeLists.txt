# WebAssembly Build Configuration for Neam
#
# This CMakeLists.txt builds Neam for WebAssembly using Emscripten.
#
# Prerequisites:
#   - Emscripten SDK installed and activated
#   - Run: source /path/to/emsdk/emsdk_env.sh
#
# Build:
#   mkdir build-wasm && cd build-wasm
#   emcmake cmake ../NeamC/wasm
#   cmake --build .
#
# Output:
#   - neam.js - JavaScript glue code
#   - neam.wasm - WebAssembly binary

cmake_minimum_required(VERSION 3.16)
project(NeamWasm LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

# Check for Emscripten
if(NOT EMSCRIPTEN)
  message(FATAL_ERROR "This CMakeLists.txt is for Emscripten builds only. Use emcmake cmake.")
endif()

# Include parent source files
set(NEAM_ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../..)

# Fetch minimal dependencies for WASM build
include(FetchContent)

FetchContent_Declare(
  nlohmann_json
  URL https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(nlohmann_json)

FetchContent_Declare(
  tree_sitter
  URL https://github.com/tree-sitter/tree-sitter/archive/refs/tags/v0.22.6.tar.gz
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_Populate(tree_sitter)

# Tree-sitter library
add_library(tree_sitter_lib STATIC
  ${tree_sitter_SOURCE_DIR}/lib/src/lib.c
)
target_include_directories(tree_sitter_lib PUBLIC ${tree_sitter_SOURCE_DIR}/lib/include)

# Core Neam sources (minimal set for WASM)
set(NEAMC_WASM_SOURCES
  ${NEAM_ROOT}/NeamC/src/parser.cpp
  ${NEAM_ROOT}/NeamC/src/tree_sitter_parser.cpp
  ${NEAM_ROOT}/NeamC/src/compiler.cpp
  ${NEAM_ROOT}/NeamC/src/pipeline.cpp
  ${NEAM_ROOT}/NeamC/src/vm/memory.cpp
  ${NEAM_ROOT}/NeamC/src/vm/object.cpp
  ${NEAM_ROOT}/NeamC/src/vm/table.cpp
  ${NEAM_ROOT}/NeamC/src/vm/native.cpp
  ${NEAM_ROOT}/NeamC/src/vm/value.cpp
  ${NEAM_ROOT}/NeamC/src/vm/runtime_type.cpp
  ${NEAM_ROOT}/NeamC/src/vm/bytecode.cpp
  ${NEAM_ROOT}/NeamC/src/vm/vm.cpp
  ${NEAM_ROOT}/NeamC/src/vm/trace.cpp
  ${NEAM_ROOT}/NeamC/src/runtime/executor.cpp
  ${NEAM_ROOT}/NeamC/src/types/type_repr.cpp
  ${NEAM_ROOT}/NeamC/src/types/constraints.cpp
  ${NEAM_ROOT}/NeamC/src/types/unification.cpp
  ${NEAM_ROOT}/NeamC/src/types/environment.cpp
  ${NEAM_ROOT}/NeamC/src/types/inferencer.cpp
)

# WASM bindings source
set(WASM_BINDINGS_SOURCE ${CMAKE_CURRENT_SOURCE_DIR}/wasm_bindings.cpp)

# Build the WASM module
add_executable(neam
  ${NEAMC_WASM_SOURCES}
  ${WASM_BINDINGS_SOURCE}
)

target_include_directories(neam PRIVATE
  ${NEAM_ROOT}/NeamC/include
)

target_link_libraries(neam PRIVATE
  tree_sitter_lib
  nlohmann_json::nlohmann_json
)

# Disable features not available in WASM
target_compile_definitions(neam PRIVATE
  NEAM_WASM_BUILD=1
  NEAM_NO_NETWORK=1
  NEAM_NO_FILE_IO=1
  NEAM_NO_LLM=1
)

# Emscripten-specific options
set_target_properties(neam PROPERTIES
  OUTPUT_NAME "neam"
  SUFFIX ".js"
)

# Exported functions for JavaScript
set(EXPORTED_FUNCTIONS
  "_neam_init"
  "_neam_shutdown"
  "_neam_run"
  "_neam_run_bytecode"
  "_neam_compile"
  "_neam_get_output"
  "_neam_get_error"
  "_neam_value_type"
  "_neam_value_as_number"
  "_neam_value_as_string"
  "_neam_value_as_bool"
  "_neam_value_to_json"
  "_neam_free_result"
  "_neam_free_string"
  "_malloc"
  "_free"
)

# Join with commas for Emscripten
string(REPLACE ";" "," EXPORTED_FUNCTIONS_STR "${EXPORTED_FUNCTIONS}")

# Emscripten linker options
target_link_options(neam PRIVATE
  "-sWASM=1"
  "-sMODULARIZE=1"
  "-sEXPORT_NAME=NeamModule"
  "-sEXPORTED_FUNCTIONS=[${EXPORTED_FUNCTIONS_STR}]"
  "-sEXPORTED_RUNTIME_METHODS=['ccall','cwrap','UTF8ToString','stringToUTF8','lengthBytesUTF8']"
  "-sALLOW_MEMORY_GROWTH=1"
  "-sINITIAL_MEMORY=16777216"
  "-sMAXIMUM_MEMORY=536870912"
  "-sSTACK_SIZE=1048576"
  "-sNO_FILESYSTEM=1"
  "-sNO_EXIT_RUNTIME=1"
  "--no-entry"
  "-O3"
)

# Also output a .d.ts file
configure_file(
  ${CMAKE_CURRENT_SOURCE_DIR}/neam.d.ts.in
  ${CMAKE_CURRENT_BINARY_DIR}/neam.d.ts
  COPYONLY
)
