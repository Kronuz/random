# random

Part of [Kronuz C++ Libraries](https://github.com/Kronuz/cpp-libraries), a collection of small, reusable C++ libraries.

[![CI](https://github.com/Kronuz/random/actions/workflows/ci.yml/badge.svg)](https://github.com/Kronuz/random/actions/workflows/ci.yml)
[![License: MIT](https://img.shields.io/badge/license-MIT-blue.svg)](LICENSE)
![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg)

Random helpers for C++20: a real, an integer, a duration or an alphanumeric string from a
per-thread xoshiro256++. Each draw is one call: the thread seeds its own generator from
`std::random_device` on first use, so call sites need no setup and no lock. Reach for it for
jitter, backoff, sampling and throwaway ids.

## ✨ Highlights

- **No setup.** One xoshiro256++ generator per thread, 32 bytes of state, seeded once.
- **Inclusive ranges.** `random_int(1, 6)` returns 1 through 6. Every range includes both ends,
  and a degenerate range returns its one value.
- **Unbiased.** Integer and duration draws use Lemire's multiply-shift with rejection.
  `random_string` takes 6-bit fields and rejects 62 and 63, so each of the 62 symbols has
  probability exactly 1/62.
- **Durations directly.** `random_time(100ms, 500ms)` returns a `std::chrono::milliseconds`.
- **Thread-safe without locks.** Each thread has its own generator.
- **No dependencies** beyond the standard library.

## 🚀 Quick start

```cpp
#include "random.hh"

#include <chrono>
#include <cstdio>

using namespace std::chrono_literals;

int main() {
	double jitter = random_real(0.0, 1.0);                          // in [0, 1]
	std::uint64_t roll = random_int(1, 6);                          // 1 to 6
	std::chrono::milliseconds backoff = random_time(100ms, 500ms);  // 100 to 500 ms
	std::string id = random_string(16);                             // 16 of [0-9a-zA-Z]

	std::printf("%f %llu %lld %s\n", jitter, (unsigned long long)roll,
		(long long)backoff.count(), id.c_str());
	// e.g. 0.346343 4 372 IttaV6wzfHNJDzz2
}
```

[`examples/demo.cc`](examples/demo.cc) prints a few draws of each.

## 📦 Installation

Requires a C++20 compiler and CMake 3.14 or newer; no other dependencies. The library compiles
one source file. CI builds and tests it on Linux (x86_64 with GCC and Clang, arm64), macOS
(arm64, x86_64) and FreeBSD, plus ASan + UBSan and TSan builds.

```cmake
include(FetchContent)
FetchContent_Declare(random
  GIT_REPOSITORY https://github.com/Kronuz/random.git
  GIT_TAG        d1a4aedef66b1e25ee8c8d50105f3899c544b295)  # or main to track the tip
FetchContent_MakeAvailable(random)

target_link_libraries(my_app PRIVATE random::random)
```

```cpp
#include "random.hh"
```

## 🎯 When to use it

Timers, retries, sampling, test data, and ids that only need to be unlikely to collide.

Not for:

- Secrets, tokens, or anything an attacker might predict: xoshiro256++ is not a cryptographic
  generator.
- Reproducible runs: seeds come from `std::random_device`, and there is no call to set one.

## ⚡ Performance

[`random_bench`](bench/bench.cc) times one call on one thread, in ns, as the median of nine
batches; each cell is the median of three alternating rounds of that. The comparator is what
the standard library gives: a thread-local `std::mt19937_64` seeded from `std::random_device`,
with `std::uniform_int_distribution` and `std::uniform_real_distribution` for the ranges and a
string built by `push_back` from a uniform pick over the 62 symbols (`std_impl` in the same
file, `noinline`, so both sides cost one call into a separate unit). The two are different
generators, so they do not return the same values; the benchmark checks that both stay inside
every range and reach both ends of a small one. "× faster" is the standard library's time over
the library's, so a value below 1 is a loss.

| Call | M4 Pro `std` | M4 Pro library | M4 Pro × faster | i9 `std` | i9 library | i9 × faster |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `random_int(0, 999999)` | 4.43 | 2.50 | 1.8 | 18.3 | 5.31 | 3.4 |
| `random_int(1, 6)` | 8.30 | 2.46 | 3.4 | 23.6 | 5.24 | 4.5 |
| `random_real(0, 1)` | 2.91 | 2.53 | 1.15 | 11.1 | 6.14 | 1.8 |
| `random_time(100ms, 500ms)` | 7.50 | 2.50 | 3.0 | 22.3 | 4.84 | 4.6 |
| `random_string(8)` | 33.8 | 8.22 | 4.1 | 151 | 16.7 | 9.1 |
| `random_string(32)` | 149 | 32.2 | 4.6 | 625 | 83.1 | 7.5 |
| `random_string(256)` | 1,039 | 107 | 9.7 | 4,702 | 297 | 16 |

Apple M4 Pro (macOS 26) and Intel Core i9-9980HK (macOS 15, x86-64), Apple clang 17.0.0 with
libc++ and `-O3 -DNDEBUG` (CMake Release).

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -DRANDOM_BUILD_BENCH=ON && cmake --build build && ./build/random_bench
```

Most of an integer call is the call itself and the thread-local access. On the i9 the 4 to 6 ns
calls move by up to 1 ns with the placement of the code in the binary: `random_int(0, 999999)`
measures 4.2 to 5.3 ns across builds of the same source.

## 📚 Documentation

- [docs/API.md](docs/API.md): the four functions and their ranges.
- [ARCHITECTURE.md](ARCHITECTURE.md): the per-thread generator and the range reductions.
- [CHANGELOG.md](CHANGELOG.md): dated changes, with earlier timings.
- [AGENTS.md](AGENTS.md): repository map and traps.
- [Catalog entry](https://github.com/Kronuz/cpp-libraries/blob/main/AGENTS.md#random) in Kronuz C++
  Libraries.

## 🛠️ Building and testing

```sh
cmake -B build && cmake --build build && ctest --test-dir build
./build/random_demo
cmake -B build -DCMAKE_BUILD_TYPE=Release -DRANDOM_BUILD_BENCH=ON && cmake --build build && ./build/random_bench
```

CTest runs the statistical checks of the public API, the engine and range reductions over
scripted engines (also with the portable 128-bit multiply), and the per-thread seeding.

## 🤝 Contributing

Issues and pull requests are welcome. Read [AGENTS.md](AGENTS.md) and the catalog's
[rules of engagement](https://github.com/Kronuz/cpp-libraries/blob/main/AGENTS.md#rules-of-engagement)
first; CI must pass on every platform.

## 📄 License

MIT. See [LICENSE](LICENSE). Extracted from [Xapiand](https://github.com/Kronuz/Xapiand)
(Copyright © 2015-2020 Dubalu LLC).
