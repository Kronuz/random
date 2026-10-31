/*
 * Copyright © 2026 Germán Méndez Bravo (Kronuz)
 * Copyright (c) 2015-2019 Dubalu LLC
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "random.hh"

#include <random>                // for std::random_device


namespace {

// xoshiro256++ (Blackman & Vigna, 2019): 256 bits of state, one rotate-add
// output, no multiplies. Not cryptographically secure, same as the
// std::mt19937_64 it replaces.
struct Xoshiro256pp {
	std::uint64_t s[4];
	bool seeded;

	static constexpr std::uint64_t rotl(std::uint64_t x, int k) noexcept {
		return (x << k) | (x >> (64 - k));
	}

	std::uint64_t next() noexcept {
		const std::uint64_t result = rotl(s[0] + s[3], 23) + s[0];
		const std::uint64_t t = s[1] << 17;
		s[2] ^= s[0];
		s[3] ^= s[1];
		s[1] ^= s[2];
		s[0] ^= s[3];
		s[2] ^= t;
		s[3] = rotl(s[3], 45);
		return result;
	}

	// All 256 bits come from std::random_device (32 bits per call), so the
	// state is not limited to a 32-bit seed.
	void seed() {
		std::random_device rd;
		do {
			for (auto& w : s) {
				std::uint64_t hi = rd();
				std::uint64_t lo = rd();
				w = (hi << 32) | lo;
			}
		} while (!(s[0] | s[1] | s[2] | s[3]));  // the all-zero state is a fixed point
		seeded = true;
	}
};

// Constant-initialised POD thread_local: no dynamic-init guard on access, just
// a "seeded" flag test. One independent generator per thread, no locking.
constinit thread_local Xoshiro256pp tls_rng{};

inline Xoshiro256pp& rng() {
	auto& r = tls_rng;
	if (__builtin_expect(!r.seeded, 0)) {
		r.seed();
	}
	return r;
}

inline std::uint64_t mulhi(std::uint64_t a, std::uint64_t b, std::uint64_t& lo) noexcept {
#ifdef __SIZEOF_INT128__
	unsigned __int128 m = static_cast<unsigned __int128>(a) * b;
	lo = static_cast<std::uint64_t>(m);
	return static_cast<std::uint64_t>(m >> 64);
#else
	const std::uint64_t a0 = a & 0xffffffffu, a1 = a >> 32, b0 = b & 0xffffffffu, b1 = b >> 32;
	const std::uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
	const std::uint64_t mid = (p00 >> 32) + (p01 & 0xffffffffu) + (p10 & 0xffffffffu);
	lo = (mid << 32) | (p00 & 0xffffffffu);
	return p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
#endif
}

// Uniform in [0, range) (range >= 1) by Lemire's nearly-divisionless
// multiply-shift with rejection (ACM TOMACS 2019): exactly unbiased, and the
// division only runs on the rare slow path.
inline std::uint64_t bounded(Xoshiro256pp& r, std::uint64_t range) noexcept {
	std::uint64_t lo;
	std::uint64_t hi = mulhi(r.next(), range, lo);
	if (__builtin_expect(lo < range, 0)) {
		const std::uint64_t threshold = (0 - range) % range;
		while (lo < threshold) {
			hi = mulhi(r.next(), range, lo);
		}
	}
	return hi;
}

// Uniform in [initial, last], both inclusive; initial <= last.
inline std::uint64_t inclusive(Xoshiro256pp& r, std::uint64_t initial, std::uint64_t last) noexcept {
	const std::uint64_t span = last - initial;  // range - 1, so the full 64-bit range fits
	if (span == ~std::uint64_t(0)) {
		return r.next();
	}
	return initial + bounded(r, span + 1);
}

}  // namespace


double
random_real(double initial, double last)
{
	// 53 random bits -> [0, 1), scaled onto the range.
	const double u = static_cast<double>(rng().next() >> 11) * 0x1p-53;
	return initial + u * (last - initial);
}


std::uint64_t
random_int(std::uint64_t initial, std::uint64_t last)
{
	return inclusive(rng(), initial, last);
}


std::chrono::milliseconds
random_time(std::chrono::milliseconds initial, std::chrono::milliseconds last)
{
	using rep = std::chrono::milliseconds::rep;
	// Offset arithmetic in unsigned space so negative and wide ranges are exact.
	const std::uint64_t lo = static_cast<std::uint64_t>(initial.count());
	const std::uint64_t hi = static_cast<std::uint64_t>(last.count());
	return std::chrono::milliseconds(static_cast<rep>(inclusive(rng(), 0, hi - lo) + lo));
}


std::string
random_string(size_t length)
{
	// Alphabet padded to 64 entries so a 6-bit field indexes it directly;
	// fields 62 and 63 are rejected (unbiased: each symbol has probability 1/62).
	static constexpr char chrs[64] = {
		'0','1','2','3','4','5','6','7','8','9',
		'a','b','c','d','e','f','g','h','i','j','k','l','m','n','o','p','q','r','s','t','u','v','w','x','y','z',
		'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
		'\0','\0'};

	std::string s(length, '\0');
	char* p = s.data();
	size_t remaining = length;

	auto& r = rng();

	// One 64-bit draw carries ten 6-bit fields. Write every field and advance
	// only on accepted ones (branch-free); with >= 10 chars left the writes stay in bounds.
	while (remaining >= 10) {
		std::uint64_t x = r.next();
		size_t n = 0;
		for (int i = 0; i < 10; ++i) {
			const unsigned v = static_cast<unsigned>(x) & 63u;
			x >>= 6;
			p[n] = chrs[v];
			n += (v < 62);
		}
		p += n;
		remaining -= n;
	}
	// Tail (< 10 chars): same draws, bounds-checked.
	while (remaining) {
		std::uint64_t x = r.next();
		for (int i = 0; i < 10 && remaining; ++i) {
			const unsigned v = static_cast<unsigned>(x) & 63u;
			x >>= 6;
			if (v < 62) {
				*p++ = chrs[v];
				--remaining;
			}
		}
	}

	return s;
}
