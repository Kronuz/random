# Changelog

Notable changes to `random`, newest first. The format follows
[Keep a Changelog](https://keepachangelog.com/en/1.1.0/).

## 2026-11-01

### Changed

- `bench/bench.cc` times the library against `std::mt19937_64` with `std::uniform_int_distribution`
  and `std::uniform_real_distribution` (a thread-local engine seeded from `std::random_device`) and
  checks that both stay inside each range. The comparator is the code Xapiand ran, renamed `std_impl`.

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

### Performance

Re-measured at `86bd0f5` with `random_bench` (`cmake -B build -DCMAKE_BUILD_TYPE=Release
-DRANDOM_BUILD_BENCH=ON`, `-O3 -DNDEBUG`, Apple clang 17), one thread, `quiet strict` on an
Apple M4 Pro and an Intel i9-9980HK. Each figure is the median of three alternating rounds
of the benchmark's median-of-nine batches. "Before" is the Xapiand
`std::mt19937_64` implementation kept in `bench/bench.cc`, now `noinline`:

| Call | M4 before | M4 after | | i9 before | i9 after | |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| `random_int(0, 999999)` | 4.43 ns | 2.50 ns | 1.8x | 18.3 ns | 5.31 ns | 3.4x |
| `random_int(1, 6)` | 8.30 ns | 2.46 ns | 3.4x | 23.6 ns | 5.24 ns | 4.5x |
| `random_int(0, 2^64 - 1)` | 3.13 ns | 2.50 ns | 1.25x | 8.34 ns | 3.85 ns | 2.2x |
| `random_real(0, 1)` | 2.91 ns | 2.53 ns | 1.15x | 11.1 ns | 6.14 ns | 1.8x |
| `random_time(100ms, 500ms)` | 7.50 ns | 2.50 ns | 3.0x | 22.3 ns | 4.84 ns | 4.6x |
| `random_string(1)` | 6.94 ns | 5.58 ns | 1.24x | 26.0 ns | 9.81 ns | 2.7x |
| `random_string(8)` | 33.8 ns | 8.22 ns | 4.1x | 151 ns | 16.7 ns | 9.1x |
| `random_string(32)` | 149 ns | 32.2 ns | 4.6x | 625 ns | 83.1 ns | 7.5x |
| `random_string(256)` | 1039 ns | 107 ns | 9.7x | 4702 ns | 297 ns | 16x |

The 2026-10-30 figures (before 29, 118 and 885 ns for 8, 32 and 256 characters and 3.9 ns for
`random_int`; after 7.9, 31 and 104 ns and 2.4 ns) were the fastest of seven batches, against
a copy of the previous code compiled into the benchmark so that the compiler inlined it.

The library against itself before this change (`94f156a`, same `bench.cc`, same flags; ratio of
medians, new over old). On the M4 every row is between 0.98 and 1.03 (the largest, 1.028 for
`random_string(8)`, is inside the 2 to 5% spread of the batches). On the i9 the machine code
of `random_int` is the same: Apple clang emits the same instructions for both libraries, and
GCC 16 differs only in register choice. The timings differ by up to 25% and the sign changes
with the build: for `random_int(0, 999999)` with clang the ratio is 1.25 in the default
build, 0.84 with every function aligned to 64 bytes, and 0.80 in a build of the integer
rows alone, because the placement of the function in the binary moves the call between
about 4.2 and 5.3 ns, for either library. The string rows, which do the work, are
between 0.94 and 1.05 in every build.

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
- The speed against the previous implementation is in the 2026-10-31 entry.

### Added

- Chi-square uniformity, wide-range and per-thread generator tests.
- `bench/bench.cc`, built with `-DRANDOM_BUILD_BENCH=ON`, timing the library
  against a frozen copy of the previous implementation.

### Fixed

- The test links against Threads, which the per-thread generator test needs on
  FreeBSD and Linux.
