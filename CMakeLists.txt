cmake_minimum_required(VERSION 3.16)

project(NeamC LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_library(neamc_vm
    NeamC/src/vm/value.cpp
    NeamC/src/vm/bytecode.cpp
    NeamC/src/vm/vm.cpp
)

target_include_directories(neamc_vm
    PUBLIC
        ${CMAKE_CURRENT_SOURCE_DIR}/NeamC/include
)

add_executable(neamc
    NeamC/src/neamc/main.cpp
)
target_link_libraries(neamc PRIVATE neamc_vm)

enable_testing()

add_executable(vm_test
    tests/vm_test.cpp
)
target_link_libraries(vm_test PRIVATE neamc_vm)
add_test(NAME vm_test COMMAND vm_test)
