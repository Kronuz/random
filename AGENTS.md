# AGENTS.md

Working notes for agents modifying this repository. For the design read
`ARCHITECTURE.md`; for usage read `README.md`. This file covers the repo layout,
how to build and test, the invariants you must not break, and the traps that are
easy to fall into.

## Repo map

```
random.hh                    Declares random_real / random_int / random_time / random_string. Header. Same as Xapiand's.
random.cc                    The thread-local xoshiro256++ state and the four function bodies, each a call into random_engine.hh. Includes only <random> and random_engine.hh.
random_engine.hh             Internal, not API: xoshiro256++, the 64x64 -> 128 multiply, Lemire's bounded draw and the real/time/string generators, as templates over the engine.
bench/bench.cc               Timing against a frozen, noinline copy of Xapiand's mt19937_64 implementation; build with -DRANDOM_BUILD_BENCH=ON.
test/test.cc                 CTest `random`: range bounds, degenerate ranges, both-endpoints-reached, string length/alphabet, chi-square uniformity (including residues at huge ranges), wide ranges, per-thread generators.
test/engine_test.cc          CTest `random_engine` and `random_engine_portable` (built with -DRANDOM_NO_INT128): random_engine.hh over scripted engines: xoshiro256++ reference vectors, seeding, the multiply against a 16-bit-limb product, Lemire's rejection exactly over every 16-bit word, the 64-bit rule, real/time/string.
test/seed_test.cc            CTest `random_seed`: random.cc built over a scripted entropy source (test/scripted_device.hh): lazy seeding, once per thread, eight 32-bit words, per-thread streams.
tools/mutants.py             Breaks the engine and random.cc in 30 ways and requires the tests to fail on each; run it after touching random_engine.hh or random.cc.
examples/demo.cc             A runnable tour (not a test).
.github/workflows/ci.yml     The shared CI (Linux, macOS, FreeBSD, ASan, UBSan, TSan).
.github/workflows/msvc.yml   Builds and tests with MSVC on Windows.
CMakeLists.txt               Compiled library `random` (+ alias random::random); no FetchContent; CTest tests `random`, `random_engine`, `random_engine_portable`, `random_seed`.
LICENSE                      MIT, Copyright (c) 2015-2019 Dubalu LLC.
README.md                    What it is, install, quick start.
docs/API.md                  The API reference.
ARCHITECTURE.md              Internal design: the shared thread-local generator and the four wrappers.
CHANGELOG.md                 Behavior and performance changes, newest first.
```

This is not header-only. `random.cc` must be compiled and linked; the header only
declares the four functions. The CMake target is a plain compiled library. Unlike
the `repr` sibling, there is **no** external dependency and **no** FetchContent:
`random.cc` pulls only the standard `<random>` header.

## Build and run the test

```sh
cmake -B build && cmake --build build && ctest --test-dir build
```

No network is needed. Expected output ends with `all random tests passed`, exit
0. The test target is `random_test`; the registered CTest name is `random`. The
test and demo are only added when this repo is the top-level project
(CMakeLists.txt), so consumers vendoring it via `FetchContent` won't build them.

## Dependencies

None beyond the standard library. `random.cc` includes `<random>` and `random_engine.hh`; `random.hh` includes `<chrono>`, `<cstdint>`, `<string>`. There is
no `log.h` / opts / config / `THROW` coupling. Don't add an external dependency.

The one seam is `RANDOM_ENTROPY_HEADER`: a header that defines `RANDOM_ENTROPY_SOURCE`, a type returning 32 random bits per call, in place of
`std::random_device`. Only `random_scripted` (the seeding test) sets it.

## Conventions

- **C++20.** The target requests `cxx_std_20` `PUBLIC` to stay uniform with the
  sibling libraries. Don't drop the target below it.
- **Filenames are stable.** The header and source keep their original Xapiand
  names (`random.hh`, `random.cc`) so a consumer that already `#include`s the
  header just needs this repo on the include path. Don't rename them.
- **Builds with GCC, Clang and MSVC.** No `__builtin_*`, no `__attribute__`, no unguarded `__int128`: use `[[likely]]` / `[[unlikely]]`,
  and keep the 64x64 -> 128 multiply behind its `RANDOM_HAS_INT128` / `_umul128` / portable ladder. The `msvc.yml` job checks it.
- Tabs for indentation, double quotes in code, no em dashes in prose.
- MIT-licensed; keep the copyright header (Copyright (c) 2015-2019 Dubalu LLC) on
  source files.

## Load-bearing invariants

- **One shared generator, `constinit thread_local`.** `rng()` returns a single
  xoshiro256++ state per thread, seeded on first use with 256 bits from
  `std::random_device`. The seeded flag is what keeps it from reseeding every call;
  the `thread_local` is what makes it data-race-free without a lock. Don't move the
  state to a namespace-scope global (that would share it across threads)
  and don't reseed per call. Ranges use Lemire multiply-shift: keep the rejection
  step, it is what makes them unbiased. `test/engine_test.cc` counts the accepted words of a whole
  16-bit word space for every range (each result must be hit exactly `floor(2^16 / range)` times) and checks
  the 64-bit rule against a written-out version; `tools/mutants.py` checks that dropping the step fails them.
- **The engine and the seed are pinned.** `test/engine_test.cc` holds the reference vectors of xoshiro256++
  (Blackman and Vigna's C code), and checks that seeding makes eight 32-bit draws, high half first, and redraws the all-zero state.
  The engine is not API, but changing it means changing those vectors on purpose.
- **All ranges are inclusive of both endpoints.** `random_int` /  `random_time`
  include `last`; `random_real` scales 53 bits onto `[initial, last]`. The test asserts a
  small integer range reaches both ends. Don't switch to a half-open convention.
- **`random_time` is `random_int` over tick counts.** It draws a uniform integer
  over the two durations' `.count()` and rebuilds a `std::chrono::milliseconds`.
  Keep the integer type as `std::chrono::milliseconds::rep`; narrowing it would
  cap the representable range.
- **`random_string`'s alphabet is a 64-entry table.** Entries 0 to 61 are
  `0-9a-zA-Z`; entries 62 and 63 are `'\0'` padding so a 6-bit field indexes the
  table directly, and fields 62 and 63 are rejected. Accepting them lets a `\0`
  into the output; the test checks every character falls in the alphabet.

## How to extend

- **Add a generator (e.g. `random_bool`, a typed `random_int<T>`).** Mirror the
  existing wrappers: draw from `rng()` and reduce with the unbiased `inclusive()`
  helper (or the 53-bit scaling for reals). Don't introduce a second engine; share
  the one in `rng()`.
- **Always extend the tests.** The public API is non-deterministic by design, so `test/test.cc`
  asserts the contract that *is* deterministic (inclusive bounds, degenerate single-value
  ranges, string length and alphabet) and runs enough draws (100k per ranged check) that a
  broken bound is almost certain to trip. Exact draws are asserted only in `test/engine_test.cc`
  and `test/seed_test.cc`, which run the same code over scripted words. Add a mutant to
  `tools/mutants.py` for each property you pin.

## Traps

- **The seed is not reproducible.** `std::random_device` reseeds differently each
  run, so the public API has no fixed sequence to assert against and no way to reproduce a
  failing draw from a seed. Tests of the public API check ranges and structure, not values;
  exact values come from driving `random_engine.hh` with a scripted engine, or `random.cc`
  with a scripted entropy source.
- **`random_real`'s upper bound.** The scaled 53-bit draw is half-open
  `[a, b)` in principle, but rounding can let it return `b`; the
  contract here is the closed interval `[a, b]` and the test asserts `<= last`.
  Don't tighten the test to `< last`.
- **Tests are statistical.** The generator is unseeded, so the chi-square checks use
  a threshold ~10 sigma out; don't tighten it. The test target is compiled with
  `-UNDEBUG` so its asserts run in Release builds.

## Standalone vs. Xapiand

This is a standalone extraction from
[Xapiand](https://github.com/Kronuz/Xapiand). `random.hh` matches upstream.
`random.cc` uses its own engine and range reduction (the API has no seed, so the
engine is not a contract). Keep the includes to `random.hh`, `random_engine.hh` and the standard library.
