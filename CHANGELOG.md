# Changelog

Notable changes to `random`, newest first. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## 2026-10-31

### Added

- `random_engine.hh` (internal): xoshiro256++, the 64x64 -> 128 multiply, Lemire's
  bounded draw and the real, time and string generators, as templates over the
  engine. `random.cc` calls them over its thread-local engine; the tests call the
  same code over scripted engines.
- `test/engine_test.cc` (`random_engine`, and `random_engine_portable` built with
  `-DRANDOM_NO_INT128`): the authors' reference vectors for xoshiro256++; eight
  32-bit draws per seeding, high half first, the all-zero state redrawn; the
  multiply against a 16-bit-limb product; Lemire's rejection checked exactly over
  every 16-bit word for ranges 1 to 300 and edge ranges (each result is hit by
  exactly `floor(2^16 / range)` accepted words), and the 64-bit rule against a
  written-out version, including constructed rejected words; strings against
  literal text and a field-by-field reference.
- `test/seed_test.cc` (`random_seed`): `random.cc` built over a scripted entropy
  source (`RANDOM_ENTROPY_HEADER`): seeding is lazy, happens once per thread with
  eight 32-bit words, and each thread has its own stream.
- Residue checks (`random_int(...) % 3` at 3 * 2^62 values, `% 2` at
  `ceil(2^65 / 3)`) in `test/test.cc`: they fail when the rejection step is
  missing.
- `tools/mutants.py` breaks the engine and `random.cc` in 30 ways and requires a
  test to fail on each. All 30 fail a test; before this change the repository's
  test passed on a missing rejection loop, a wrong rotation in the engine, a
  32-bit seed and a broken portable multiply.
- `.github/workflows/msvc.yml` builds and tests with MSVC on Windows.

### Changed

- `random.cc` compiles with MSVC again: `[[unlikely]]` replaces
  `__builtin_expect`, the 128-bit multiply uses `_umul128` or `__umulh` on
  MSVC, and the inlining attributes are guarded by compiler. The portable multiply
  (no `__int128`) is compiled and tested on every platform, not only where
  `__int128` is missing.
- The benchmark's frozen copy of the previous implementation is `noinline`, so
  both columns cost one call into a separate unit, and each row is the median of
  nine batches.

### Fixed

- The README footer carried Dubalu years that did not match the source headers;
  it points to `LICENSE`.

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
