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

// Internal: the engine and the range reductions behind random.hh, as templates over the
// engine so the tests can drive the exact code random.cc runs with a scripted engine. Not
// part of the API.
//
// An engine is anything with `result_type` (an unsigned integer) and `next()`.

#pragma once

#include <chrono>                // for std::chrono::milliseconds
#include <cstddef>               // for size_t
#include <cstdint>               // for std::uint64_t
#include <limits>                // for std::numeric_limits
#include <string>                // for std::string
#include <type_traits>           // for std::is_same_v

#if defined(_MSC_VER)
#include <intrin.h>              // for _umul128, __umulh
#endif

// RANDOM_NO_INT128 forces the portable 64x64 -> 128 multiply, so one build can test it on
// a compiler that has __int128.
#if defined(__SIZEOF_INT128__) && !defined(RANDOM_NO_INT128)
#define RANDOM_HAS_INT128 1
#endif


// The draw paths are inlined into the four exported functions, as they were written there;
// the seeding path is kept out of line.
#if defined(_MSC_VER)
#define RANDOM_INLINE __forceinline
#define RANDOM_NOINLINE __declspec(noinline)
#elif defined(__GNUC__)
#define RANDOM_INLINE inline __attribute__((always_inline))
#define RANDOM_NOINLINE __attribute__((noinline))
#else
#define RANDOM_INLINE inline
#define RANDOM_NOINLINE
#endif


namespace random_detail {

// xoshiro256++ (Blackman & Vigna, 2019): 256 bits of state, one rotate-add
// output, no multiplies. Not cryptographically secure.
struct Xoshiro256pp {
	using result_type = std::uint64_t;

	std::uint64_t s[4];
	bool seeded;

	static constexpr std::uint64_t rotl(std::uint64_t x, int k) noexcept {
		return (x << k) | (x >> (64 - k));
	}

	constexpr std::uint64_t next() noexcept {
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

	// Fills all 256 bits from `source`, a callable returning 32 random bits (such as
	// std::random_device): two calls per word, the first call the high half, so eight
	// calls in all. The all-zero state is a fixed point, so it is drawn again.
	template <class Source>
	void seed(Source& source) {
		static_assert(std::numeric_limits<typename Source::result_type>::digits >= 32,
		              "the entropy source must return at least 32 bits");
		do {
			for (auto& w : s) {
				const std::uint64_t hi = static_cast<std::uint32_t>(source());
				const std::uint64_t lo = static_cast<std::uint32_t>(source());
				w = (hi << 32) | lo;
			}
		} while (!(s[0] | s[1] | s[2] | s[3]));
		seeded = true;
	}
};


// The 128-bit product of a and b as {high, lo}, from 32-bit limbs.
constexpr std::uint64_t mulhi_portable(std::uint64_t a, std::uint64_t b, std::uint64_t& lo) noexcept {
	const std::uint64_t a0 = a & 0xffffffffu, a1 = a >> 32, b0 = b & 0xffffffffu, b1 = b >> 32;
	const std::uint64_t p00 = a0 * b0, p01 = a0 * b1, p10 = a1 * b0, p11 = a1 * b1;
	const std::uint64_t mid = (p00 >> 32) + (p01 & 0xffffffffu) + (p10 & 0xffffffffu);
	lo = (mid << 32) | (p00 & 0xffffffffu);
	return p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);
}

inline std::uint64_t mulhi(std::uint64_t a, std::uint64_t b, std::uint64_t& lo) noexcept {
#if defined(RANDOM_HAS_INT128)
	const unsigned __int128 m = static_cast<unsigned __int128>(a) * b;
	lo = static_cast<std::uint64_t>(m);
	return static_cast<std::uint64_t>(m >> 64);
#elif defined(_MSC_VER) && defined(_M_X64)
	return _umul128(a, b, &lo);
#elif defined(_MSC_VER) && defined(_M_ARM64)
	lo = a * b;
	return __umulh(a, b);
#else
	return mulhi_portable(a, b, lo);
#endif
}

// The same product for the narrower words the tests use to enumerate a whole word space.
template <class W, std::enable_if_t<(sizeof(W) < sizeof(std::uint64_t)), int> = 0>
constexpr W mulhi(W a, W b, W& lo) noexcept {
	const std::uint64_t m = static_cast<std::uint64_t>(a) * b;
	lo = static_cast<W>(m);
	return static_cast<W>(m >> std::numeric_limits<W>::digits);
}


// Uniform in [0, range) (range >= 1) by Lemire's nearly-divisionless multiply-shift with
// rejection (ACM TOMACS 2019): exactly unbiased, and the division only runs on the rare
// slow path. Generic in the word width W of the engine.
template <class Engine>
RANDOM_INLINE typename Engine::result_type bounded(Engine& r, typename Engine::result_type range) noexcept {
	using W = typename Engine::result_type;
	W lo;
	W hi = mulhi(r.next(), range, lo);
	if (lo < range) [[unlikely]] {
		const W threshold = static_cast<W>(static_cast<W>(0 - range) % range);
		while (lo < threshold) {
			hi = mulhi(r.next(), range, lo);
		}
	}
	return hi;
}

// Uniform in [initial, last], both inclusive; initial <= last.
template <class Engine>
RANDOM_INLINE typename Engine::result_type inclusive(Engine& r, typename Engine::result_type initial,
                                              typename Engine::result_type last) noexcept {
	using W = typename Engine::result_type;
	const W span = static_cast<W>(last - initial);  // range - 1, so the full word range fits
	if (span == std::numeric_limits<W>::max()) {
		return r.next();
	}
	return static_cast<W>(initial + bounded(r, static_cast<W>(span + 1)));
}


// 53 random bits as a double in [0, 1), scaled onto [initial, last].
template <class Engine>
RANDOM_INLINE double real_between(Engine& r, double initial, double last) noexcept {
	const double u = static_cast<double>(r.next() >> 11) * 0x1p-53;
	return initial + u * (last - initial);
}


// Offset arithmetic in unsigned space so negative and wide ranges are exact.
template <class Engine>
RANDOM_INLINE std::chrono::milliseconds time_between(Engine& r, std::chrono::milliseconds initial,
                                              std::chrono::milliseconds last) noexcept {
	using rep = std::chrono::milliseconds::rep;
	const std::uint64_t lo = static_cast<std::uint64_t>(initial.count());
	const std::uint64_t hi = static_cast<std::uint64_t>(last.count());
	return std::chrono::milliseconds(static_cast<rep>(inclusive(r, std::uint64_t(0), hi - lo) + lo));
}


// `length` characters of [0-9a-zA-Z]. One 64-bit draw carries ten 6-bit fields; a field
// below 62 selects a symbol, 62 and 63 are rejected, so each symbol has probability 1/62.
template <class Engine>
RANDOM_INLINE std::string alnum_string(Engine& r, std::size_t length) {
	static_assert(std::is_same_v<typename Engine::result_type, std::uint64_t>);

	// Padded to 64 entries so a 6-bit field indexes it directly.
	static constexpr char chrs[64] = {
		'0','1','2','3','4','5','6','7','8','9',
		'a','b','c','d','e','f','g','h','i','j','k','l','m','n','o','p','q','r','s','t','u','v','w','x','y','z',
		'A','B','C','D','E','F','G','H','I','J','K','L','M','N','O','P','Q','R','S','T','U','V','W','X','Y','Z',
		'\0','\0'};

	std::string s(length, '\0');
	char* p = s.data();
	std::size_t remaining = length;

	// With >= 10 chars left, write every field and advance only on accepted ones
	// (branch-free): the writes stay in bounds.
	while (remaining >= 10) {
		std::uint64_t x = r.next();
		std::size_t n = 0;
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

}  // namespace random_detail
