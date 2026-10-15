# random

[![CI](https://github.com/Kronuz/random/actions/workflows/ci.yml/badge.svg)](https://github.com/Kronuz/random/actions/workflows/ci.yml)

A small, dependency-free **random-number** helper library for C++20, extracted
from [Xapiand](https://github.com/Kronuz/Xapiand).

## What it is

Four convenience functions over the standard `<random>` library, sharing one
thread-local generator so you don't have to set one up at every call site:

- `random_real(a, b)` — a uniform `double` in `[a, b]`.
- `random_int(a, b)` — a uniform `std::uint64_t` in `[a, b]`, inclusive.
- `random_time(a, b)` — a uniform `std::chrono::milliseconds` in `[a, b]`,
  inclusive.
- `random_string(n)` — an `n`-character string drawn from `[0-9A-Za-z]`.

Behind all four is a single `std::mt19937_64` (a 64-bit Mersenne twister), one
per thread, seeded once from `std::random_device`. That gives you decent,
fast pseudo-randomness for jitter, backoff timers, dice rolls, throwaway ids,
and the like. It is not a cryptographic generator and isn't meant to be.

## Install

This is not header-only. It ships a compiled translation unit, `random.cc`, plus
the header `random.hh`. You build and link the `.cc`; the header alone only
declares the four functions. It requires C++20 and has no external dependencies:
`random.cc` includes nothing but the standard `<random>` and `<algorithm>`
headers.

With CMake `FetchContent`:

```cmake
include(FetchContent)
FetchContent_Declare(
  random
  GIT_REPOSITORY https://github.com/Kronuz/random.git
  GIT_TAG        main
)
FetchContent_MakeAvailable(random)

target_link_libraries(your_target PRIVATE random::random)
```

`CMakeLists.txt` requests `cxx_std_20` `PUBLIC` and puts the source dir on your
include path, so there is no extra wiring on your side. Then:

```cpp
#include "random.hh"
```

The header keeps its original filename, so a codebase that already
`#include "random.hh"` just needs this repo on its include path.

## Usage

```cpp
#include "random.hh"
#include <chrono>

// A probability or jitter factor: a uniform real in [0, 1].
double p = random_real(0.0, 1.0);

// A dice roll: an integer in [1, 6], inclusive on both ends.
std::uint64_t roll = random_int(1, 6);

// Randomized backoff: wait somewhere between 100ms and 500ms.
using namespace std::chrono;
milliseconds wait = random_time(milliseconds{100}, milliseconds{500});

// A throwaway id: 16 random alphanumeric characters.
std::string id = random_string(16);
```

All four ranges are inclusive of both endpoints. A degenerate range collapses to
its single value: `random_int(7, 7)` is always `7`, and `random_real(3.0, 3.0)`
is always `3.0`. `random_string(0)` is the empty string.

## API reference

```cpp
double                    random_real(double initial, double last);
std::uint64_t             random_int(std::uint64_t initial, std::uint64_t last);
std::chrono::milliseconds random_time(std::chrono::milliseconds initial,
                                       std::chrono::milliseconds last);
std::string               random_string(size_t length);
```

- **`random_real`** — `std::uniform_real_distribution<double>` over `[initial,
  last]`.
- **`random_int`** — `std::uniform_int_distribution<std::uint64_t>` over
  `[initial, last]`, inclusive on both ends.
- **`random_time`** — the same uniform integer draw, applied to the millisecond
  `count()` of the two durations and rebuilt into a `std::chrono::milliseconds`.
- **`random_string`** — picks `length` characters uniformly from the 62-symbol
  alphabet `0-9a-zA-Z`.

The generator is `thread_local`, so each thread gets its own twister and there is
no shared-state contention or data race across threads. The seed comes from
`std::random_device` once per thread, so draws are not reproducible across runs.

## Build & test

```sh
cmake -B build && cmake --build build && ctest --test-dir build
```

No network is needed; there are no dependencies to fetch. The test runs each
generator many thousands of times and checks the contract that is deterministic:
every draw stays inside its inclusive range, a zero-width range produces exactly
its endpoint, a small integer range reaches both ends, and `random_string`
returns the exact requested length using only `[0-9A-Za-z]`. Exact draws are
never asserted. It prints `all random tests passed` and exits 0.

## Examples

[`examples/demo.cc`](examples/demo.cc) is a runnable tour. A top-level CMake build
produces it next to the test:

```sh
cmake -B build && cmake --build build && ./build/random_demo
```

It prints a few reals in `[0, 1)`, a row of dice rolls, several backoff
durations, and a handful of random ids.

## Provenance

Extracted from [Xapiand](https://github.com/Kronuz/Xapiand). `random.hh` /
`random.cc` were copied verbatim. The original already had zero local includes
(no `log.h`, no `opts`, no `THROW`), so there was nothing to decouple. See
[ARCHITECTURE.md](ARCHITECTURE.md) for the design and [AGENTS.md](AGENTS.md) for
the repo map and invariants.

## License

MIT, Copyright (c) 2015-2019 Dubalu LLC. See [LICENSE](LICENSE).
