cmake_minimum_required(VERSION 3.14)
project(random LANGUAGES CXX)

# --- Library target ---
# random is not header-only: random.cc carries the four generators (a
# thread-local xoshiro256++ with Lemire multiply-shift bounded draws), so it
# must be compiled and linked. The header alone (random.hh) only declares the
# functions; random_engine.hh (internal) holds the engine and the range
# reductions as templates over the engine, for the tests. There is no external
# dependency to fetch: random.cc pulls nothing but the standard <random> header
# (for std::random_device seeding), so the target is a plain compiled library
# with no FetchContent.
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
  find_package(Threads REQUIRED)

  # Statistical checks of the public API; the test relies on assert(), so keep it
  # live even in Release builds.
  add_executable(random_test test/test.cc)
  target_link_libraries(random_test PRIVATE random Threads::Threads)
  target_compile_options(random_test PRIVATE "$<IF:$<CXX_COMPILER_ID:MSVC>,/UNDEBUG,-UNDEBUG>")
  add_test(NAME random COMMAND random_test)

  # Deterministic checks of the engine and the range reductions (random_engine.hh)
  # over scripted engines. The second build forces the portable 64x64 -> 128 multiply.
  add_executable(random_engine_test test/engine_test.cc)
  target_include_directories(random_engine_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_compile_features(random_engine_test PRIVATE cxx_std_20)
  add_test(NAME random_engine COMMAND random_engine_test)
  add_executable(random_engine_portable_test test/engine_test.cc)
  target_include_directories(random_engine_portable_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
  target_compile_features(random_engine_portable_test PRIVATE cxx_std_20)
  target_compile_definitions(random_engine_portable_test PRIVATE RANDOM_NO_INT128)
  add_test(NAME random_engine_portable COMMAND random_engine_portable_test)

  # random.cc built again over a scripted entropy source, to check the per-thread seeding.
  add_library(random_scripted STATIC random.cc)
  target_include_directories(random_scripted PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
  target_compile_features(random_scripted PUBLIC cxx_std_20)
  target_compile_definitions(random_scripted PRIVATE
    "RANDOM_ENTROPY_HEADER=\"${CMAKE_CURRENT_SOURCE_DIR}/test/scripted_device.hh\"")
  add_executable(random_seed_test test/seed_test.cc)
  target_include_directories(random_seed_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/test")
  target_link_libraries(random_seed_test PRIVATE random_scripted Threads::Threads)
  add_test(NAME random_seed COMMAND random_seed_test)

  # A runnable tour of the library (not a test): build & run ./build/random_demo
  add_executable(random_demo examples/demo.cc)
  target_link_libraries(random_demo PRIVATE random)

  # Timing against a frozen std::mt19937_64 reference kept in the file.
  option(RANDOM_BUILD_BENCH "Build the benchmark" OFF)
  if(RANDOM_BUILD_BENCH)
    add_executable(random_bench bench/bench.cc)
    target_link_libraries(random_bench PRIVATE random)
  endif()
endif()
