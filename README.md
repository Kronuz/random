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

`random_bench` times one thread in a Release build (`-O3`, Apple clang 17) and reports the median
of nine batches:

| Call | Apple M4 Pro | Intel Core i9-9980HK |
| --- | ---: | ---: |
| `random_int(0, 999999)` | 2.50 ns | 5.3 ns |
| `random_int(1, 6)` | 2.46 ns | 5.2 ns |
| `random_real(0, 1)` | 2.53 ns | 6.1 ns |
| `random_time(100ms, 500ms)` | 2.50 ns | 4.8 ns |
| `random_string(8)` | 8.2 ns | 17 ns |
| `random_string(32)` | 32 ns | 83 ns |
| `random_string(256)` | 107 ns | 297 ns |

Most of an integer call is the call itself and the thread-local access. On the i9 the 4 to 6 ns
calls move by up to 1 ns with the placement of the code in the binary: `random_int(0, 999999)`
measures 4.2 to 5.3 ns across builds of the same source.

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release -DRANDOM_BUILD_BENCH=ON && cmake --build build && ./build/random_bench
```

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
