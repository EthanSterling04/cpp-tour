cmake_minimum_required(VERSION 3.25)
project(cpp_tour LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)   # clangd reads this

set(CMAKE_CXX_FLAGS_DEBUG   "-O1 -g")
set(CMAKE_CXX_FLAGS_RELEASE "-O3 -DNDEBUG -g")

option(TOUR_NATIVE "tune Release builds for this CPU" ON)
if(TOUR_NATIVE)
  include(CheckCXXCompilerFlag)
  check_cxx_compiler_flag(-march=native HAS_MARCH_NATIVE)
  if(HAS_MARCH_NATIVE)
    string(APPEND CMAKE_CXX_FLAGS_RELEASE " -march=native")
  else()
    string(APPEND CMAKE_CXX_FLAGS_RELEASE " -mcpu=native")
  endif()
endif()

# address for everything, thread for ch. 18 -- they cannot coexist in one binary.
set(TOUR_SANITIZER "address" CACHE STRING "address | thread | none")

add_library(tour_warnings INTERFACE)
target_compile_options(tour_warnings INTERFACE
  -Wall -Wextra -Wpedantic -Werror -fno-omit-frame-pointer)

add_library(tour_sanitize INTERFACE)
if(TOUR_SANITIZER STREQUAL "address")
  target_compile_options(tour_sanitize INTERFACE -fsanitize=address,undefined)
  target_link_options(tour_sanitize    INTERFACE -fsanitize=address,undefined)
elseif(TOUR_SANITIZER STREQUAL "thread")
  target_compile_options(tour_sanitize INTERFACE -fsanitize=thread)
  target_link_options(tour_sanitize    INTERFACE -fsanitize=thread)
endif()

include(FetchContent)
FetchContent_Declare(googletest
  GIT_REPOSITORY https://github.com/google/googletest.git
  GIT_TAG        v1.15.2
  GIT_SHALLOW    TRUE)
set(BENCHMARK_ENABLE_TESTING     OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL     OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
FetchContent_Declare(benchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG        v1.9.0
  GIT_SHALLOW    TRUE)
FetchContent_MakeAvailable(googletest benchmark)

enable_testing()
include(GoogleTest)

function(add_tour_test target)
  add_executable(${target} ${ARGN})
  target_link_libraries(${target}
    PRIVATE tour_warnings tour_sanitize GTest::gtest_main)
  gtest_discover_tests(${target})
endfunction()

function(add_tour_bench target)   # no sanitizers: they distort timings
  add_executable(${target} ${ARGN})
  target_link_libraries(${target}
    PRIVATE tour_warnings benchmark::benchmark_main)
endfunction()

foreach(dir 01 02 03 04 05 06 07 08 10 11 12 13 14 15 16 17 18)
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/ch${dir}/CMakeLists.txt")
    add_subdirectory(ch${dir})
  endif()
endforeach()