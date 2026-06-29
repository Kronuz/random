# Architecture

The internals of `random`: one thread-local generator shared by four small
wrappers over the standard distributions. For usage see `README.md`; for the repo
map and invariants see `AGENTS.md`.

## Shape

A header of four declarations and a single compiled translation unit:

```
  random.hh   declares random_real / random_int / random_time / random_string
  random.cc   the shared rng() generator and the four function bodies
```

There is nothing behind it but the standard library: `random.cc` includes only
`<random>` and `<algorithm>`, `random.hh` only `<chrono>`, `<cstdint>`, and
`<string>`. No external dependency, no FetchContent.

## The shared generator

Every function draws from one generator, defined in `random.cc`:

```cpp
auto& rng() {
	// Mersenne twister seeded once per thread from std::random_device.
	static thread_local std::mt19937_64 rng(std::random_device{}());
	return rng;
}
```

Two things are load-bearing here. It is `static`, so the twister is constructed
once and reused across calls rather than reseeded each time (reseeding per call
would be both slow and statistically worse). And it is `thread_local`, so each
thread gets its own independent twister: there is no shared mutable state across
threads, which means no locking and no data race, at the cost of one generator
per thread. The seed is pulled from `std::random_device` (usually `/dev/random`
or an OS CSPRNG on UNIX-like systems) exactly once per thread when that thread
first touches `rng()`.

`std::mt19937_64` is the 64-bit Mersenne twister. It is fast and has a long
period, which is what you want for jitter, ids, and timers. It is not
cryptographically secure, and this library makes no claim that it is.

## The four wrappers

Each public function is a thin adapter: build the matching standard distribution
for the requested range, then draw once from the shared generator.

`random_real` uses `std::uniform_real_distribution<double>` over `[initial,
last]`. `random_int` uses `std::uniform_int_distribution<std::uint64_t>`, which
is inclusive on both endpoints. `random_time` is `random_int` in disguise: it
runs a `std::uniform_int_distribution` over the millisecond `count()` of the two
durations (using `std::chrono::milliseconds::rep` as the integer type) and wraps
the result back into a `std::chrono::milliseconds`. So a duration range is just an
integer range over the tick counts.

`random_string` is the one with its own loop. It holds a `static` 62-character
alphabet (`0-9`, then `a-z`, then `A-Z`) and a `thread_local`
`std::uniform_int_distribution` indexing into it. The distribution's upper bound
is `sizeof(chrs) - 2`: `sizeof` on the literal counts the trailing NUL, so `- 1`
would include it and `- 2` lands on the last real character. The function
reserves `length` bytes up front and pushes one randomly-picked character at a
time. A request of `0` yields the empty string.

## Why this shape

The point is to make "give me a random X in this range" a one-liner at the call
site, without each caller standing up its own engine and distribution. Folding
the engine into a shared `thread_local` keeps the call sites trivial and keeps the
generator off the hot path of construction, while the per-thread split sidesteps
synchronization entirely. The distributions themselves are the standard library's;
this library only owns the seeding policy and the alphabet, which is exactly the
part you'd otherwise copy-paste into every project.
