# Architecture

The internals of `random`: one thread-local generator shared by four small
range functions. For usage see `README.md`; for the repo
map and invariants see `AGENTS.md`.

## Shape

A header of four declarations, an internal header of templates, and a single compiled
translation unit:

```
  random.hh          declares random_real / random_int / random_time / random_string
  random_engine.hh   internal: xoshiro256++, the 128-bit multiply, Lemire's bounded draw,
                     and the real / time / string generators, as templates over the engine
  random.cc          the thread-local engine state, its seeding, and the four function bodies
```

There is nothing behind it but the standard library: `random.cc` includes `random_engine.hh` and
`<random>` (for `std::random_device`), `random.hh` only `<chrono>`, `<cstdint>`,
and `<string>`. No external dependency, no FetchContent.

Every generator in `random_engine.hh` takes the engine as a template argument, so the tests run
the code `random.cc` runs over a scripted engine, and over 16-bit words to enumerate a whole
word space. `random.cc` can also be built over another entropy source
(`RANDOM_ENTROPY_HEADER`), which is how the per-thread seeding is tested.

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
library makes no claim that it is. The API has no seed, so the engine is not part
of the contract and can change.

## The four wrappers

`random_int(a, b)` draws `x` and takes `(x * range) >> 64` (a 128-bit multiply:
`unsigned __int128`, `_umul128` / `__umulh` on MSVC, or 32-bit limbs elsewhere),
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
synchronization entirely. The library owns the seeding policy, the unbiased range
reduction and the alphabet, which is exactly the part you'd otherwise copy-paste
into every project.
