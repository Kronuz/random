# Changelog

Notable changes to `random`, newest first. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## 2026-10-30

### Changed

- The per-thread generator is xoshiro256++ (32 bytes of state) instead of
  `std::mt19937_64` (2.5 KiB of state, a periodic 312-word twist). The state is
  a constant-initialised `thread_local` POD seeded from 256 bits of
  `std::random_device`; the previous engine was seeded from 32 bits. The API has
  no seed, so the engine is not part of the contract.
- `random_int` and `random_time` use Lemire's unbiased multiply-shift ranges
  instead of `std::uniform_int_distribution`. `random_real` scales 53 bits onto
  the range.
- `random_string` takes ten 6-bit fields from each 64-bit draw into a pre-sized
  buffer, instead of one distribution draw and a `push_back` per character. Its
  alphabet table is padded to 64 entries and the old `sizeof(chrs) - 2` bound is
  gone.
- Speed against the previous implementation, on an Apple M4 Pro (-O3), from
  `random_bench`:

  | Call | Before | After | Speedup |
  | --- | --- | --- | --- |
  | `random_string(8)` | 29 ns | 7.9 ns | 3.6x |
  | `random_string(32)` | 118 ns | 31 ns | 3.8x |
  | `random_string(256)` | 885 ns | 104 ns | 8.5x |
  | `random_int` | 3.9 ns | 2.4 ns | 1.6x |

### Added

- Chi-square uniformity, wide-range and per-thread generator tests.
- `bench/bench.cc`, built with `-DRANDOM_BUILD_BENCH=ON`, timing the library
  against a frozen copy of the previous implementation.

### Fixed

- The test links against Threads, which the per-thread generator test needs on
  FreeBSD and Linux.
