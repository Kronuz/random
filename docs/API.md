# random API reference

```cpp
double                    random_real(double initial, double last);
std::uint64_t             random_int(std::uint64_t initial, std::uint64_t last);
std::chrono::milliseconds random_time(std::chrono::milliseconds initial,
                                       std::chrono::milliseconds last);
std::string               random_string(size_t length);
```

- **`random_real`** — `std::uniform_real_distribution<double>`. It is nominally
  half-open, but rounding can return `last`, so the contract is `[initial, last]`.
- **`random_int`** — `std::uniform_int_distribution<std::uint64_t>` over
  `[initial, last]`, inclusive on both ends.
- **`random_time`** — the same uniform integer draw, applied to the millisecond
  `count()` of the two durations and rebuilt into a `std::chrono::milliseconds`.
- **`random_string`** — picks `length` characters uniformly from the 62-symbol
  alphabet `0-9a-zA-Z`.

The generator is `thread_local`, so each thread gets its own twister and there is
no shared-state contention or data race across threads. The seed comes from
`std::random_device` once per thread, so draws are not reproducible across runs.

All ranges are inclusive of both ends. A degenerate range returns its one value.
