cmake_minimum_required(VERSION 3.14)
project(random LANGUAGES CXX)

# --- Library target ---
# random is not header-only: random.cc carries the four generators (a
# thread-local xoshiro256++ with Lemire multiply-shift bounded draws), so it
# must be compiled and linked. The header alone (random.hh) only declares the
# functions. There is no external dependency to fetch: random.cc pulls nothing
# but the standard <random> header (for std::random_device seeding), so the
# target is a plain compiled library with no FetchContent.
add_library(random random.cc)
target_include_directories(random PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_compile_features(random PUBLIC cxx_std_20)

# Alias so consumers can link `random::random` whether they add this via
# add_subdirectory or FetchContent.
add_library(random::random ALIAS random)

# Only build the test and demo when this repo is the top-level project, so
# consumers (e.g. via FetchContent) don't build them.
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_CURRENT_SOURCE_DIR)
  enable_testing()
  add_executable(random_test test/test.cc)
  target_link_libraries(random_test PRIVATE random)
  # The test relies on assert(), so keep it live even in Release builds.
  target_compile_options(random_test PRIVATE -UNDEBUG)
  add_test(NAME random COMMAND random_test)

  # A runnable tour of the library (not a test): build & run ./build/random_demo
  add_executable(random_demo examples/demo.cc)
  target_link_libraries(random_demo PRIVATE random)

  # Old (mt19937_64) vs new timing, frozen copy of the old code in the file.
  option(RANDOM_BUILD_BENCH "Build the benchmark" OFF)
  if(RANDOM_BUILD_BENCH)
    add_executable(random_bench bench/bench.cc)
    target_link_libraries(random_bench PRIVATE random)
  endif()
endif()
