# AGENTS.md

Working notes for agents modifying this repository. For the design read
`ARCHITECTURE.md`; for usage read `README.md`. This file covers the repo layout,
how to build and test, the invariants you must not break, and the traps that are
easy to fall into.

## Repo map

```
random.hh                    Declares random_real / random_int / random_time / random_string. Header. Verbatim from Xapiand.
random.cc                    The thread-local xoshiro256++ generator plus the four function bodies (Lemire bounded draws). Includes only <random>; no longer verbatim from Xapiand.
bench/bench.cc               Old (frozen mt19937_64) vs new timing; build with -DRANDOM_BUILD_BENCH=ON.
test/test.cc                 Runnable smoke test: range bounds, degenerate ranges, both-endpoints-reached, string length/alphabet.
examples/demo.cc             A runnable tour (not a test).
CMakeLists.txt               Compiled library `random` (+ alias random::random); no FetchContent; CTest test `random`.
LICENSE                      MIT, Copyright (c) 2015-2019 Dubalu LLC.
README.md                    What it is, install, quick start.
docs/API.md                  The API reference.
ARCHITECTURE.md              Internal design: the shared thread-local generator and the four wrappers.
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

None beyond the standard library. `random.cc` includes `<random>`; `random.hh` includes `<chrono>`, `<cstdint>`, `<string>`. There
was no `log.h` / opts / config / `THROW` coupling in the original, so nothing was
decoupled on extraction. Don't add an external dependency.

## Conventions

- **C++20.** The target requests `cxx_std_20` `PUBLIC` to stay uniform with the
  sibling libraries. Don't drop the target below it.
- **Filenames are stable.** The header and source keep their original Xapiand
  names (`random.hh`, `random.cc`) so a consumer that already `#include`s the
  header just needs this repo on the include path. Don't rename them.
- Tabs for indentation, double quotes in code, no em dashes in prose. (Note:
  `random.cc` mixes tabs and four-space indentation in `random_string`; that is
  verbatim from upstream, leave it.)
- MIT-licensed; keep the copyright header (Copyright (c) 2015-2019 Dubalu LLC) on
  source files.

## Load-bearing invariants

- **One shared generator, `constinit thread_local`.** `rng()` returns a single
  xoshiro256++ state per thread, seeded on first use with 256 bits from
  `std::random_device`. The seeded flag is what keeps it from reseeding every call;
  the `thread_local` is what makes it data-race-free without a lock. Don't move the
  state to a namespace-scope global (that would reintroduce cross-thread sharing)
  and don't reseed per call. Ranges use Lemire multiply-shift: keep the rejection
  step, it is what makes them unbiased.
- **All ranges are inclusive of both endpoints.** `random_int` /  `random_time`
  include `last`; `random_real` scales 53 bits onto `[initial, last]`. The test asserts a
  small integer range reaches both ends. Don't switch to a half-open convention.
- **`random_time` is `random_int` over tick counts.** It draws a uniform integer
  over the two durations' `.count()` and rebuilds a `std::chrono::milliseconds`.
  Keep the integer type as `std::chrono::milliseconds::rep`; narrowing it would
  cap the representable range.
- **`random_string`'s alphabet bound is `sizeof(chrs) - 2`.** `sizeof` counts the
  literal's trailing NUL, so `- 2` (not `- 1`) lands on the last real character.
  Off-by-one here either drops `Z` or lets a `\0` into the output. The alphabet is
  `0-9a-zA-Z` (62 symbols); the test checks every character falls in it.

## How to extend

- **Add a generator (e.g. `random_bool`, a typed `random_int<T>`).** Mirror the
  existing wrappers: pick the matching standard distribution, draw once from
  `rng()`, return. Don't introduce a second engine; share the one in `rng()`.
- **Always extend the smoke test.** `test/test.cc` is the only executable check,
  and the API is non-deterministic by design. Assert the contract that *is*
  deterministic — inclusive bounds, degenerate single-value ranges, string length
  and alphabet — and run enough draws (the test uses 100k per ranged check) that a
  broken bound is almost certain to trip. Never assert an exact draw.

## Traps

- **The seed is not reproducible.** `std::random_device` reseeds differently each
  run, so there is no fixed sequence to assert against and no way to reproduce a
  failing draw from a seed. Tests must check ranges and structure, not values. If
  you ever need reproducibility for a test, that is a reason to add a seedable
  entry point, not to assert exact outputs of the current API.
- **`random_real`'s upper bound.** The scaled 53-bit draw is half-open
  `[a, b)` in principle, but rounding can let it return `b`; the
  contract here is the closed interval `[a, b]` and the test asserts `<= last`.
  Don't tighten the test to `< last`.
- **Tests are statistical.** The generator is unseeded, so the chi-square checks use
  a threshold ~10 sigma out; don't tighten it. The test target is compiled with
  `-UNDEBUG` so its asserts run in Release builds.

## Standalone vs. Xapiand

This is a standalone extraction from
[Xapiand](https://github.com/Kronuz/Xapiand). `random.hh` was copied verbatim and `random.cc` started as a verbatim copy; the
engine and distributions have since been replaced (the API has no seed, so the
engine is not a contract). Keep zero local includes.
