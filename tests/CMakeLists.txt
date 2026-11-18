cmake_minimum_required(VERSION 3.16)

project(BytebeatTools LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(BYTEBEAT_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../mpc/src")

function(bytebeat_warnings target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4)
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    endif()
endfunction()

add_executable(core_tests core_tests.cpp)
target_include_directories(core_tests PRIVATE "${BYTEBEAT_CORE_DIR}")
bytebeat_warnings(core_tests)

add_executable(bbtool bbtool.cpp)
target_include_directories(bbtool PRIVATE "${BYTEBEAT_CORE_DIR}")
bytebeat_warnings(bbtool)

enable_testing()
add_test(NAME core_tests COMMAND core_tests)
