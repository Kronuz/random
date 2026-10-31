// Smoke test for the standalone random library.
//
// The four generators draw from a thread-local xoshiro256++ seeded from
// std::random_device, so the values aren't reproducible across runs. The test
// is written around that: it pins down the contract that *is* deterministic
// (the half-open / closed bounds of each range, the length and alphabet of a
// random string, degenerate single-value ranges) and runs each generator many
// times so a bound violation is very likely to be caught. Exact draws are never
// asserted.
//
// Build via CMake: cmake -B build && cmake --build build && ctest --test-dir build
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <thread>
#include <vector>

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


// ---------------------------------------------------------------------------
// Uniformity: chi-square goodness of fit against the exact uniform expectation.
// The generator is unseeded, so the threshold is df + 10 * sqrt(2 * df), far
// out in the tail (false-positive odds well under 1e-12 per check).
// ---------------------------------------------------------------------------

static double chi_square(const std::vector<std::uint64_t>& counts, double expected) {
	double x = 0;
	for (auto c : counts) {
		double d = static_cast<double>(c) - expected;
		x += d * d / expected;
	}
	return x;
}

static void check_chi(const std::vector<std::uint64_t>& counts, double expected, const char* what) {
	double df = static_cast<double>(counts.size() - 1);
	double x = chi_square(counts, expected);
	if (x > df + 10 * std::sqrt(2 * df)) {
		std::printf("chi-square failed for %s: %f (df %f)\n", what, x, df);
		std::abort();
	}
}

static void test_uniformity() {
	// Bounded ints at ranges that stress the multiply-shift (small, non-power
	// of two, just over a power of two).
	for (std::uint64_t n : {2u, 3u, 6u, 10u, 62u, 100u, 257u, 1000u}) {
		const std::uint64_t draws = n * 2000;
		std::vector<std::uint64_t> counts(n);
		for (std::uint64_t i = 0; i < draws; ++i) counts[random_int(5, 5 + n - 1) - 5]++;
		check_chi(counts, 2000.0, "random_int");
	}

	// Bits of a full-range draw are balanced, and the high word is as good as the low.
	{
		std::vector<std::uint64_t> counts(64);
		const int draws = 200000;
		for (int i = 0; i < draws; ++i) {
			std::uint64_t v = random_int(0, ~std::uint64_t(0));
			for (int b = 0; b < 64; ++b) counts[b] += (v >> b) & 1;
		}
		for (int b = 0; b < 64; ++b) {
			double d = static_cast<double>(counts[b]) - draws / 2.0;
			assert(std::fabs(d) < 10 * std::sqrt(draws / 4.0));
		}
	}

	// Huge range not a power of two: the top third must hold a third of the draws.
	{
		const std::uint64_t n = 3ull << 62;  // 3 * 2^62
		std::vector<std::uint64_t> counts(3);
		for (int i = 0; i < 300000; ++i) counts[random_int(0, n - 1) / (1ull << 62)]++;
		check_chi(counts, 100000.0, "random_int wide range");
	}

	// random_time over a negative-spanning range.
	{
		using namespace std::chrono;
		std::vector<std::uint64_t> counts(7);
		for (int i = 0; i < 70000; ++i) {
			auto v = random_time(milliseconds{-3}, milliseconds{3}).count();
			assert(v >= -3 && v <= 3);
			counts[v + 3]++;
		}
		check_chi(counts, 10000.0, "random_time");
	}

	// Extreme time range does not overflow.
	{
		using namespace std::chrono;
		auto lo = milliseconds::min(), hi = milliseconds::max();
		bool neg = false, pos = false;
		for (int i = 0; i < 1000; ++i) {
			auto v = random_time(lo, hi);
			(v.count() < 0 ? neg : pos) = true;
		}
		assert(neg && pos);
	}

	// random_real: 64 equal buckets over [0, 1), and scaling to [a, b].
	{
		std::vector<std::uint64_t> counts(64);
		for (int i = 0; i < 64 * 2000; ++i) counts[static_cast<size_t>(random_real(0.0, 1.0) * 64)]++;
		check_chi(counts, 2000.0, "random_real");
		std::vector<std::uint64_t> c2(10);
		for (int i = 0; i < 20000; ++i) {
			double v = random_real(-5.0, 5.0);
			assert(v >= -5.0 && v <= 5.0);
			c2[std::min<size_t>(9, static_cast<size_t>(v + 5.0))]++;
		}
		check_chi(c2, 2000.0, "random_real scaled");
	}

	// random_string: each of the 62 symbols equally likely, at every position
	// (the ten 6-bit fields of a draw must all be unbiased), at lengths that
	// hit both the bulk loop and the tail.
	for (size_t len : {1u, 9u, 10u, 11u, 20u, 33u}) {
		std::vector<std::uint64_t> total(62);
		std::vector<std::vector<std::uint64_t>> pos(len, std::vector<std::uint64_t>(62));
		const int strings = 62 * 200;
		for (int i = 0; i < strings; ++i) {
			std::string s = random_string(len);
			assert(s.size() == len);
			for (size_t k = 0; k < len; ++k) {
				char c = s[k];
				size_t idx = (c <= '9') ? c - '0' : (c >= 'a' ? c - 'a' + 10 : c - 'A' + 36);
				total[idx]++;
				pos[k][idx]++;
			}
		}
		check_chi(total, 200.0 * len, "random_string symbols");
		if (len >= 10) {
			for (size_t k = 0; k < len; ++k) check_chi(pos[k], 200.0, "random_string position");
		}
	}

	std::printf("uniformity OK: chi-square on int, time, real, string\n");
}


static void test_threads() {
	// Each thread has its own generator: concurrent use must not crash or
	// produce out-of-range values, and different threads must diverge.
	std::vector<std::thread> ts;
	std::vector<std::string> firsts(8);
	for (int t = 0; t < 8; ++t) {
		ts.emplace_back([t, &firsts] {
			for (int i = 0; i < 100000; ++i) {
				assert(random_int(0, 9) <= 9);
			}
			firsts[t] = random_string(32);
		});
	}
	for (auto& t : ts) t.join();
	for (int a = 0; a < 8; ++a) for (int b = a + 1; b < 8; ++b) assert(firsts[a] != firsts[b]);
	std::printf("threads OK: independent per-thread generators\n");
}


int main() {
	test_random_real();
	test_random_int();
	test_random_time();
	test_random_string();
	test_uniformity();
	test_threads();
	std::printf("all random tests passed\n");
	return 0;
}
