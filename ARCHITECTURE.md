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

Every function draws from one generator, defined in `random.cc`: xoshiro256++
(Blackman and Vigna), four 64-bit words of state held in a constant-initialised
`thread_local` POD. Access costs a "seeded" flag test, with no dynamic-init guard.

Two things are load-bearing. The state is `thread_local`, so each thread has an
independent generator with no shared mutable state: no locking and no data race.
And it is seeded once per thread, on first use, with all 256 bits taken from
`std::random_device` (32 bits per call, so the state is not limited to a 32-bit
seed); the all-zero state, a fixed point, is redrawn.

xoshiro256++ is fast and passes the usual statistical batteries, which is what you
want for jitter, ids, and timers. It is not cryptographically secure, and this
library makes no claim that it is. It replaced `std::mt19937_64` (2.5 KiB of state,
a periodic 312-word twist) because there is no seed API: the engine is not part of
the contract.

## The four wrappers

`random_int(a, b)` draws `x` and takes `(x * range) >> 64` (a 128-bit multiply),
Lemire's multiply-shift. The slow path (`low < range`) rejects the few draws that
would bias a result, so the result is exactly uniform; the division for the
rejection threshold only runs there. The full `[0, 2^64 - 1]` range returns the raw
draw. `random_time` is the same draw in unsigned offset space over the millisecond
`count()` of the two durations (so negative and extreme ranges do not overflow),
rebuilt into a `std::chrono::milliseconds`.

`random_real` takes 53 bits as a double in `[0, 1)` and scales it onto
`[initial, last]`.

`random_string` takes one 64-bit draw per ten characters: each draw is cut into ten
6-bit fields, and a field below 62 indexes a 64-entry alphabet table (`0-9`, `a-z`,
`A-Z`), so each symbol has probability exactly 1/62; fields 62 and 63 are dropped.
The string is sized once and written through a pointer, advancing branch-free on
accepted fields. A request of `0` yields the empty string.

## Why this shape

The point is to make "give me a random X in this range" a one-liner at the call
site, without each caller standing up its own engine and distribution. Folding
the engine into a shared `thread_local` keeps the call sites trivial and keeps the
generator off the hot path of construction, while the per-thread split sidesteps
synchronization entirely. The distributions themselves are the standard library's;
this library only owns the seeding policy and the alphabet, which is exactly the
part you'd otherwise copy-paste into every project.
