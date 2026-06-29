// Smoke test for the standalone random library.
//
// The four generators draw from a thread-local Mersenne twister seeded by
// std::random_device, so the values aren't reproducible across runs. The test
// is written around that: it pins down the contract that *is* deterministic
// (the half-open / closed bounds of each range, the length and alphabet of a
// random string, degenerate single-value ranges) and runs each generator many
// times so a bound violation is very likely to be caught. Exact draws are never
// asserted.
//
// Build via CMake: cmake -B build && cmake --build build && ctest --test-dir build
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <string>

#include "random.hh"


// How many draws per ranged check. Large enough that a broken bound is almost
// certain to trip, small enough to stay instant.
static constexpr int kDraws = 100000;


// ---------------------------------------------------------------------------
// random_real(a, b): a uniform real in [a, b]. Every draw stays within the
// closed interval, and a degenerate range collapses to its single endpoint.
// ---------------------------------------------------------------------------

static void test_random_real() {
	for (int i = 0; i < kDraws; ++i) {
		double v = random_real(0.0, 1.0);
		assert(v >= 0.0 && v <= 1.0);
	}

	// A non-trivial, negative-spanning interval is still respected.
	for (int i = 0; i < kDraws; ++i) {
		double v = random_real(-2.5, 7.5);
		assert(v >= -2.5 && v <= 7.5);
	}

	// Zero-width interval: the only value it can produce is the endpoint.
	for (int i = 0; i < 100; ++i) {
		assert(random_real(3.0, 3.0) == 3.0);
	}

	std::printf("random_real OK: stays in [a, b], degenerate range is exact\n");
}


// ---------------------------------------------------------------------------
// random_int(a, b): a uniform integer in [a, b] inclusive. Bounds hold, a
// single-value range is deterministic, and across many draws the generator
// actually reaches both endpoints of a small range (so it isn't stuck).
// ---------------------------------------------------------------------------

static void test_random_int() {
	for (int i = 0; i < kDraws; ++i) {
		std::uint64_t v = random_int(10, 20);
		assert(v >= 10 && v <= 20);
	}

	// Single-value (closed) range: 7..7 can only be 7. This is the one fully
	// deterministic draw the API offers.
	for (int i = 0; i < 100; ++i) {
		assert(random_int(7, 7) == 7);
	}

	// The range is inclusive on both ends, so over many draws of a tiny range
	// we expect to see both the low and the high endpoint at least once.
	bool saw_low = false, saw_high = false;
	for (int i = 0; i < kDraws; ++i) {
		std::uint64_t v = random_int(0, 1);
		assert(v <= 1);
		if (v == 0) saw_low = true;
		if (v == 1) saw_high = true;
	}
	assert(saw_low && saw_high);

	// Works up near the top of the 64-bit range too.
	std::uint64_t big = static_cast<std::uint64_t>(-1);
	for (int i = 0; i < kDraws; ++i) {
		std::uint64_t v = random_int(big - 5, big);
		assert(v >= big - 5 && v <= big);
	}

	std::printf("random_int OK: inclusive [a, b], reaches both ends, exact when a==b\n");
}


// ---------------------------------------------------------------------------
// random_time(a, b): a uniform duration in [a, b] inclusive, in milliseconds.
// Same shape as random_int but typed as std::chrono::milliseconds.
// ---------------------------------------------------------------------------

static void test_random_time() {
	using namespace std::chrono;

	const milliseconds lo{100};
	const milliseconds hi{500};
	for (int i = 0; i < kDraws; ++i) {
		milliseconds v = random_time(lo, hi);
		assert(v >= lo && v <= hi);
	}

	// Degenerate range collapses to the single duration.
	for (int i = 0; i < 100; ++i) {
		assert(random_time(milliseconds{42}, milliseconds{42}) == milliseconds{42});
	}

	std::printf("random_time OK: stays in [a, b] ms, degenerate range is exact\n");
}


// ---------------------------------------------------------------------------
// random_string(n): an n-character string drawn from [0-9A-Za-z]. The length
// is exact (including n == 0), every character is in the alphabet, and over a
// long string we actually see more than one distinct character.
// ---------------------------------------------------------------------------

static bool in_alphabet(char c) {
	return (c >= '0' && c <= '9') ||
	       (c >= 'a' && c <= 'z') ||
	       (c >= 'A' && c <= 'Z');
}

static void test_random_string() {
	// Empty request yields the empty string.
	assert(random_string(0).empty());

	// Exact length and alphabet for a handful of sizes.
	for (size_t n : {1u, 7u, 16u, 64u, 1000u}) {
		std::string s = random_string(n);
		assert(s.size() == n);
		for (char c : s) {
			assert(in_alphabet(c));
		}
	}

	// A long draw should contain at least two distinct characters: the odds of
	// 4096 identical picks from a 62-symbol alphabet are astronomically small.
	std::string big = random_string(4096);
	assert(big.size() == 4096);
	bool varied = false;
	for (char c : big) {
		if (c != big.front()) { varied = true; break; }
	}
	assert(varied);

	std::printf("random_string OK: exact length, alphabet [0-9A-Za-z], varied\n");
}


int main() {
	test_random_real();
	test_random_int();
	test_random_time();
	test_random_string();
	std::printf("all random tests passed\n");
	return 0;
}
