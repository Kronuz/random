// Deterministic tests of the engine and the range reductions behind random.hh.
//
// random.cc runs the templates in random_engine.hh over a thread-local xoshiro256++. Here the
// same templates run over scripted engines, so every result is checked against an oracle that
// does not share the implementation's code:
//   - xoshiro256++: the authors' reference vectors (xoshiro256plusplus.c, public domain);
//   - 64x64 -> 128 multiply: a schoolbook product on 16-bit limbs;
//   - Lemire's rejection: exact counts over a whole 16-bit word space (every word, many
//     ranges: each result must be hit by exactly floor(2^16 / range) accepted words), plus
//     the rule written out with a different threshold expression on 64-bit words;
//   - strings: literal expected text for known fields, and a field-by-field reference.
// The build also runs this file with -DRANDOM_NO_INT128 so the portable multiply is the one
// `bounded` uses.
#include "random_engine.hh"

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <utility>
#include <vector>

namespace rd = random_detail;
using u64 = std::uint64_t;

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); std::abort(); } } while (0)


// A pseudo-random word stream for building test inputs (splitmix64); not the engine under test.
struct Mix {
	u64 x;
	u64 operator()() {
		u64 z = (x += 0x9e3779b97f4a7c15ull);
		z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ull;
		z = (z ^ (z >> 27)) * 0x94d049bb133111ebull;
		return z ^ (z >> 31);
	}
};

// Replays a list of words; drawing past the end fails the test, and `drawn` counts the draws.
template <class W>
struct Script {
	using result_type = W;
	std::vector<W> words;
	std::size_t drawn = 0;
	W next() {
		CHECK(drawn < words.size());
		return words[drawn++];
	}
};

// A 32-bit entropy source that replays a list and counts its calls.
struct Words32 {
	using result_type = unsigned;
	std::vector<unsigned> words;
	std::size_t calls = 0;
	unsigned operator()() {
		CHECK(calls < words.size());
		return words[calls++];
	}
};


// ---------------------------------------------------------------------------
// xoshiro256++ against the authors' reference output.
// ---------------------------------------------------------------------------

static void test_xoshiro_vectors() {
	// State {1, 2, 3, 4}; the first 16 outputs and the 1,000,000th.
	static const u64 A[16] = {
		41943041ull, 58720359ull, 3588806011781223ull, 3591011842654386ull,
		9228616714210784205ull, 9973669472204895162ull, 14011001112246962877ull, 12406186145184390807ull,
		15849039046786891736ull, 10450023813501588000ull, 7590656733288168551ull, 16724090360747261412ull,
		2865451489662947825ull, 10196951337984037005ull, 665890913787221918ull, 2800994817798129956ull};
	rd::Xoshiro256pp g{{1, 2, 3, 4}, true};
	for (u64 want : A) CHECK(g.next() == want);
	g = {{1, 2, 3, 4}, true};
	u64 x = 0;
	for (int i = 0; i < 1000000; ++i) x = g.next();
	CHECK(x == 6247625096812398776ull);

	// A state with every word dense (the authors' splitmix64 seeding of 1234567): 8 outputs,
	// the 1,000,000th, and the state after 1,000,000 draws.
	static const u64 B[8] = {
		0x0610e053dd55ab68ull, 0x70c979e26e27fbacull, 0xfb95f99f9f6bb2deull, 0x03890aaecd9fa80aull,
		0x536acabd892d9406ull, 0x1f1a58ff404b8898ull, 0x3cd1d20808d011c6ull, 0x53705606df265061ull};
	const u64 b0[4] = {0x599ed017fb08fc85ull, 0x2c73f08458540fa5ull, 0x883ebce5a3f27c77ull, 0x3fbef740e9177b3full};
	g = {{b0[0], b0[1], b0[2], b0[3]}, true};
	for (u64 want : B) CHECK(g.next() == want);
	g = {{b0[0], b0[1], b0[2], b0[3]}, true};
	for (int i = 0; i < 1000000; ++i) x = g.next();
	CHECK(x == 0x7c7cbb1628969fafull);
	CHECK(g.s[0] == 0x30d9914be1dcc482ull && g.s[1] == 0x356a7149dea82a86ull);
	CHECK(g.s[2] == 0x1c4f84ad59c4bcb2ull && g.s[3] == 0x2fe9fae15b010cb3ull);
	std::puts("xoshiro256++ OK: reference vectors");
}


// ---------------------------------------------------------------------------
// Seeding: all 256 bits from the source, 32 per call, high half first.
// ---------------------------------------------------------------------------

// A 64-bit entropy source whose every value has bits above 32 set.
struct Wide64 {
	using result_type = u64;
	u64 operator()() { return 0xffffffff00000001ull; }
};

static void test_seed() {
	// Eight calls fill four words, two calls per word, the first call the high half.
	{
		Words32 src{{0x01234567u, 0x89abcdefu, 0xfedcba98u, 0x76543210u, 0x0f1e2d3cu, 0x4b5a6978u, 0xdeadbeefu, 0xcafef00du}};
		rd::Xoshiro256pp g{};
		CHECK(!g.seeded);
		g.seed(src);
		CHECK(g.seeded);
		CHECK(src.calls == 8);
		CHECK(g.s[0] == 0x0123456789abcdefull && g.s[1] == 0xfedcba9876543210ull);
		CHECK(g.s[2] == 0x0f1e2d3c4b5a6978ull && g.s[3] == 0xdeadbeefcafef00dull);
	}
	// The all-zero state is a fixed point: it is drawn again, whole.
	{
		Words32 src{{0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 2, 0, 3, 0, 4, 0}};
		rd::Xoshiro256pp g{};
		g.seed(src);
		CHECK(src.calls == 16);
		CHECK(g.s[0] == (1ull << 32) && g.s[1] == (2ull << 32) && g.s[2] == (3ull << 32) && g.s[3] == (4ull << 32));
	}
	// A state with one non-zero word is accepted as drawn, whichever word it is.
	for (int k = 0; k < 4; ++k) {
		Words32 src{{0, 0, 0, 0, 0, 0, 0, 0}};
		src.words[2 * k + 1] = 1;
		rd::Xoshiro256pp g{};
		g.seed(src);
		CHECK(src.calls == 8);
		for (int i = 0; i < 4; ++i) CHECK(g.s[i] == (i == k ? 1ull : 0ull));
	}
	// A source wider than 32 bits contributes only its low 32 bits.
	{
		Wide64 src;
		rd::Xoshiro256pp g{};
		g.seed(src);
		for (u64 w : g.s) CHECK(w == 0x0000000100000001ull);
	}
	std::puts("seed OK: eight 32-bit draws, high half first, all-zero state redrawn");
}


// ---------------------------------------------------------------------------
// The 64x64 -> 128 multiply.
// ---------------------------------------------------------------------------

// Schoolbook product on 16-bit limbs.
static u64 ref_mulhi(u64 a, u64 b, u64& lo) {
	std::uint32_t x[4], y[4], r[8] = {};
	for (int i = 0; i < 4; ++i) {
		x[i] = static_cast<std::uint32_t>(a >> (16 * i)) & 0xffffu;
		y[i] = static_cast<std::uint32_t>(b >> (16 * i)) & 0xffffu;
	}
	for (int i = 0; i < 4; ++i) {
		std::uint32_t carry = 0;
		for (int j = 0; j < 4; ++j) {
			const std::uint32_t t = x[i] * y[j] + r[i + j] + carry;
			r[i + j] = t & 0xffffu;
			carry = t >> 16;
		}
		r[i + 4] = carry;
	}
	u64 l = 0, h = 0;
	for (int i = 3; i >= 0; --i) {
		l = (l << 16) | r[i];
		h = (h << 16) | r[i + 4];
	}
	lo = l;
	return h;
}

static void check_mul(u64 a, u64 b) {
	u64 want_lo, lo;
	const u64 want_hi = ref_mulhi(a, b, want_lo);
	CHECK(rd::mulhi_portable(a, b, lo) == want_hi && lo == want_lo);
	CHECK(rd::mulhi(a, b, lo) == want_hi && lo == want_lo);
}

static void test_mulhi() {
	// Known products.
	u64 lo;
	CHECK(rd::mulhi_portable(~0ull, ~0ull, lo) == ~0ull - 1 && lo == 1);
	CHECK(rd::mulhi_portable(1ull << 63, 2, lo) == 1 && lo == 0);
	CHECK(rd::mulhi_portable(0x100000001ull, 0x100000001ull, lo) == 1 && lo == 0x200000001ull);
	CHECK(rd::mulhi_portable(0xffffffffull, 0xffffffffull, lo) == 0 && lo == 0xfffffffe00000001ull);
	CHECK(rd::mulhi_portable(0, ~0ull, lo) == 0 && lo == 0);
	// Edges of every limb boundary against the reference, then random pairs.
	static const u64 edge[] = {0, 1, 2, 0xffffull, 0x10000ull, 0xffffffffull, 0x100000000ull, 0x100000001ull,
	                           0x7fffffffffffffffull, 0x8000000000000000ull, 0x8000000000000001ull,
	                           0xffffffff00000000ull, 0x00000000ffffffffull, ~0ull - 1, ~0ull};
	for (u64 a : edge) for (u64 b : edge) check_mul(a, b);
	Mix m{42};
	for (int i = 0; i < 200000; ++i) {
		const u64 a = m(), b = m();  // declarators are sequenced; function arguments are not
		check_mul(a, b);
	}
	// Narrow words use the same rule: a spread of 16-bit multipliers against every 16-bit multiplicand.
	for (unsigned a = 0; a < 65536; a += 251) {
		for (unsigned b = 0; b < 65536; ++b) {
			std::uint16_t l;
			const std::uint16_t h = rd::mulhi(static_cast<std::uint16_t>(a), static_cast<std::uint16_t>(b), l);
			CHECK((static_cast<std::uint32_t>(h) << 16 | l) == a * b);
		}
	}
	std::puts("mulhi OK: portable and native products match a 16-bit-limb schoolbook product");
}


// ---------------------------------------------------------------------------
// Lemire's bounded draw: exactly unbiased, over a whole word space.
// ---------------------------------------------------------------------------

// One draw, then a fixed filler: reports what `bounded` made of the first word.
struct First16 {
	using result_type = std::uint16_t;
	std::uint16_t first;
	std::size_t drawn = 0;
	std::uint16_t next() { return drawn++ == 0 ? first : static_cast<std::uint16_t>(first + drawn); }
};

static void check_range16(unsigned range) {
	const unsigned t = 65536u % range;  // 2^16 mod range: the words below it in `lo` are rejected
	std::vector<unsigned> hits(range);
	for (unsigned w = 0; w < 65536; ++w) {
		First16 e{static_cast<std::uint16_t>(w)};
		const unsigned got = rd::bounded(e, static_cast<std::uint16_t>(range));
		const unsigned product = w * range;
		const bool accepted = (product & 0xffffu) >= t;
		// The first word decides: accepted words are used as they are and rejected ones draw again.
		CHECK(accepted ? e.drawn == 1 : e.drawn >= 2);
		if (accepted) {
			CHECK(got == product >> 16);
			++hits[got];
		}
		CHECK(got < range);
	}
	// Every result is hit by exactly floor(2^16 / range) accepted words: no bias.
	for (unsigned v = 0; v < range; ++v) CHECK(hits[v] == 65536u / range);
}

static void test_bounded_exact() {
	for (unsigned range = 1; range <= 300; ++range) check_range16(range);
	for (unsigned range : {341u, 1000u, 4097u, 21845u, 21846u, 32767u, 32768u, 32769u, 43691u, 65534u, 65535u}) check_range16(range);

	// A run of rejected words is skipped, then the next word is used (word space 2^16, range 3:
	// only word 0 is rejected, so a rejected word is followed by anything).
	{
		Script<std::uint16_t> s{{0, 0, 0, 40000}};
		CHECK(rd::bounded(s, std::uint16_t(3)) == 40000u * 3 >> 16);
		CHECK(s.drawn == 4);
	}

	// inclusive: whole 16-bit word space for every pair on a small domain; the full word range
	// returns the raw word.
	for (unsigned initial = 0; initial < 12; ++initial) {
		for (unsigned last = initial; last < 14; ++last) {
			const unsigned range = last - initial + 1;
			std::vector<unsigned> hits(range);
			for (unsigned w = 0; w < 65536; ++w) {
				First16 e{static_cast<std::uint16_t>(w)};
				const unsigned got = rd::inclusive(e, static_cast<std::uint16_t>(initial), static_cast<std::uint16_t>(last));
				CHECK(got >= initial && got <= last);
				if (e.drawn == 1) ++hits[got - initial];
			}
			// Accepted first words: each result is hit by floor(2^16 / range) of them.
			for (unsigned v = 0; v < range; ++v) CHECK(hits[v] == 65536u / range);
		}
	}
	for (unsigned w : {0u, 1u, 40000u, 65535u}) {
		First16 e{static_cast<std::uint16_t>(w)};
		CHECK(rd::inclusive(e, std::uint16_t(0), std::uint16_t(65535)) == w);
		CHECK(e.drawn == 1);
	}
	std::puts("bounded OK: exact counts over every 16-bit word, ranges 1..300 and edge ranges");
}


// The 64-bit rule written out independently: accept a word when the low half of word * range is
// at least 2^64 mod range (computed from (2^64 - 1) mod range, a different expression from the
// implementation's (0 - range) % range), and use the high half.
static u64 ref_bounded(const std::vector<u64>& words, u64 range, std::size_t& drawn) {
	const u64 t = range == 1 ? 0 : ((~0ull % range) + 1) % range;
	for (std::size_t i = 0;; ++i) {
		CHECK(i < words.size());
		u64 lo;
		const u64 hi = ref_mulhi(words[i], range, lo);
		if (lo >= t) {
			drawn = i + 1;
			return hi;
		}
	}
}

static u64 inverse_odd(u64 a) {  // a^-1 mod 2^64 by Newton's iteration
	u64 x = a;
	for (int i = 0; i < 6; ++i) x *= 2 - a * x;
	return x;
}

static void check_bounded64(const std::vector<u64>& words, u64 range) {
	std::size_t want_drawn = 0;
	const u64 want = ref_bounded(words, range, want_drawn);
	Script<u64> s{words};
	CHECK(rd::bounded(s, range) == want);
	CHECK(s.drawn == want_drawn);
}

static void test_bounded64() {
	Mix m{7};
	std::vector<u64> ranges = {1, 2, 3, 5, 6, 7, 10, 62, 100, 1000, 1000000, 0x100000000ull, 0x100000001ull, 3ull << 62,
	                           0xaaaaaaaaaaaaaaabull /* ceil(2^65 / 3) */, 1ull << 63, (1ull << 63) + 1, ~0ull - 1, ~0ull};
	for (int i = 0; i < 40; ++i) ranges.push_back(m() | 1);          // random odd ranges
	for (int i = 0; i < 40; ++i) {  // random sizes, 1 bit to 64 bits wide
		const u64 bits = m() % 64, r = m() >> bits;
		ranges.push_back(r == ~0ull ? r : r + 1);
	}
	for (u64 range : ranges) {
		// Random words (rejected with probability up to 1/2 at the large ranges).
		for (int i = 0; i < 300; ++i) {
			std::vector<u64> words(64);
			for (u64& w : words) w = m();
			check_bounded64(words, range);
		}
		// The words at both ends and a power-of-two boundary.
		const u64 specials[] = {0, 1, 2, range - 1, range, 1ull << 32, (1ull << 63) - 1, 1ull << 63, ~0ull - 1, ~0ull};
		for (u64 w : specials) {
			std::vector<u64> words = {w};
			for (int k = 0; k < 63; ++k) words.push_back(m());
			check_bounded64(words, range);
		}
		// Rejected words, constructed: for an odd range word * range = j (mod 2^64) has the single
		// solution j * range^-1, and j < 2^64 mod range makes it a rejected word. Chain several.
		if (range & 1) {
			const u64 t = range == 1 ? 0 : ((~0ull % range) + 1) % range;
			const u64 inv = inverse_odd(range);
			for (u64 j = 0; j < t && j < 20; ++j) {
				std::vector<u64> words = {j * inv, (j + 1) * inv, (j + 2) * inv};
				for (int k = 0; k < 61; ++k) words.push_back(m());
				check_bounded64(words, range);
				u64 lo;
				ref_mulhi(words[0], range, lo);
				CHECK(lo == j);
			}
		}
	}
	// Range 3: word 0 is the one rejected word (2^64 mod 3 = 1, and 3 is invertible): a script
	// [0, 0, w] is two rejections and then w.
	{
		Script<u64> s{{0, 0, 0xaaaaaaaaaaaaaaabull}};
		CHECK(rd::bounded(s, 3) == 2);
		CHECK(s.drawn == 3);
	}
	// Range 3 * 2^62: a quarter of the words (every multiple of 4) is rejected.
	{
		Script<u64> s{{0, 4, 8, 0x8000000000000001ull}};
		CHECK(rd::bounded(s, 3ull << 62) == 3ull << 61);  // (2^63 + 1) * 3 * 2^62 >> 64
		CHECK(s.drawn == 4);
	}

	// inclusive: the full range returns the raw word and consumes one draw; offsets add on.
	{
		Script<u64> s{{0x123456789abcdef0ull}};
		CHECK(rd::inclusive(s, 0, ~0ull) == 0x123456789abcdef0ull && s.drawn == 1);
	}
	{
		Script<u64> s{{~0ull}};
		CHECK(rd::inclusive(s, 7, 7) == 7 && s.drawn == 1);
	}
	{
		// range 10: words 0 and 2^63 are rejected (2^64 mod 10 = 6, and their low half is 0)
		Script<u64> s{{~0ull, 1, 0x8000000000000001ull}};
		CHECK(rd::inclusive(s, 10, 19) == 19);
		CHECK(rd::inclusive(s, 10, 19) == 10);
		CHECK(rd::inclusive(s, 10, 19) == 15);
		CHECK(s.drawn == 3);
	}
	{  // top of the word range: initial + result must not wrap
		Script<u64> s{{~0ull, 1}};  // range 6: word 0 would be rejected (2^64 mod 6 = 4)
		CHECK(rd::inclusive(s, ~0ull - 5, ~0ull) == ~0ull);
		CHECK(rd::inclusive(s, ~0ull - 5, ~0ull) == ~0ull - 5);
	}
	std::puts("bounded OK: 64-bit draws and rejections match the written-out rule");
}


// ---------------------------------------------------------------------------
// random_real, random_time and random_string over scripted draws.
// ---------------------------------------------------------------------------

static void test_real() {
	// 53 bits: the low 11 bits of the draw are dropped.
	for (u64 w : {0ull, 0x7ffull, 0x800ull, 0xfffull, 1ull << 63, ~0ull, 0x123456789abcdef0ull}) {
		Script<u64> s{{w}};
		const double want = static_cast<double>(w >> 11) / 9007199254740992.0;  // / 2^53
		CHECK(rd::real_between(s, 0.0, 1.0) == want);
	}
	{
		Script<u64> s{{0}};
		CHECK(rd::real_between(s, 3.0, 7.0) == 3.0);
	}
	{
		Script<u64> s{{~0ull}};
		CHECK(rd::real_between(s, -2.0, 6.0) == -2.0 + (1.0 - 0x1p-53) * 8.0);
	}
	{
		Script<u64> s{{1ull << 63}};
		CHECK(rd::real_between(s, 10.0, 20.0) == 15.0);
	}
	std::puts("real OK: 53 bits scaled onto the range");
}

static void test_time() {
	using ms = std::chrono::milliseconds;
	// (initial, last, word) -> the unsigned-offset rule: initial + floor(word * (last - initial + 1) / 2^64).
	struct Case { std::int64_t a, b; u64 w; };
	Mix m{99};
	std::vector<Case> cases = {
		{100, 500, 0}, {100, 500, ~0ull}, {100, 500, 1ull << 63}, {-3, 3, 0}, {-3, 3, ~0ull}, {-3, 3, 1ull << 63},
		{-500, -100, 0x4000000000000000ull}, {42, 42, 12345},
		{INT64_MIN, INT64_MAX, 0}, {INT64_MIN, INT64_MAX, 1}, {INT64_MIN, INT64_MAX, ~0ull}, {INT64_MIN, INT64_MAX, 1ull << 63},
		{INT64_MIN, 0, 0}, {INT64_MIN, 0, ~0ull}, {0, INT64_MAX, ~0ull}, {-1, 0, 1ull << 63}, {INT64_MAX - 1, INT64_MAX, ~0ull}};
	for (int i = 0; i < 2000; ++i) {
		std::int64_t a = static_cast<std::int64_t>(m()), b = static_cast<std::int64_t>(m());
		if (b < a) std::swap(a, b);
		cases.push_back({a >> (m() % 64), b >> (m() % 64), m()});
		if (cases.back().b < cases.back().a) std::swap(cases.back().a, cases.back().b);
	}
	for (const Case& c : cases) {
		const u64 ua = static_cast<u64>(c.a), ub = static_cast<u64>(c.b);
		const u64 span = ub - ua;
		u64 want, lo;
		if (span == ~0ull) {
			want = ua + c.w;  // the whole 64-bit range: the raw word, offset by the start
		} else {
			want = ua + ref_mulhi(c.w, span + 1, lo);  // first word accepted? checked below
			const u64 range = span + 1;
			const u64 t = range == 1 ? 0 : ((~0ull % range) + 1) % range;
			if (lo < t) continue;  // a rejected first word draws again; covered by the bounded tests
		}
		Script<u64> s{{c.w}};
		const ms got = rd::time_between(s, ms(c.a), ms(c.b));
		CHECK(static_cast<u64>(got.count()) == want);
		CHECK(got.count() >= c.a && got.count() <= c.b);
	}
	std::puts("time OK: unsigned-offset arithmetic, negative and full-width ranges");
}

static const char kAlphabet[] = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";

// A word whose ten 6-bit fields are f[0..9], field 0 in the low bits.
static u64 fields(const unsigned (&f)[10]) {
	u64 w = 0;
	for (int i = 9; i >= 0; --i) w = (w << 6) | f[i];
	return w;
}

// The string rule field by field: stop as soon as `length` symbols are out.
static std::string ref_string(const std::vector<u64>& words, std::size_t length, std::size_t& drawn) {
	std::string out;
	std::size_t i = 0;
	while (out.size() < length) {
		CHECK(i < words.size());
		u64 w = words[i++];
		for (int k = 0; k < 10 && out.size() < length; ++k, w >>= 6) {
			const unsigned v = static_cast<unsigned>(w & 63);
			if (v < 62) out.push_back(kAlphabet[v]);
		}
	}
	drawn = i;
	return out;
}

static void test_string() {
	// Known fields give known text: the alphabet table is checked against literals.
	{
		const unsigned f0[10] = {0, 1, 2, 3, 4, 5, 6, 7, 8, 9};
		const unsigned f1[10] = {10, 11, 12, 13, 14, 15, 16, 17, 18, 19};
		const unsigned f2[10] = {20, 21, 22, 23, 24, 25, 26, 27, 28, 29};
		const unsigned f3[10] = {30, 31, 32, 33, 34, 35, 36, 37, 38, 39};
		const unsigned f4[10] = {40, 41, 42, 43, 44, 45, 46, 47, 48, 49};
		const unsigned f5[10] = {50, 51, 52, 53, 54, 55, 56, 57, 58, 59};
		const unsigned f6[10] = {60, 61, 62, 63, 0, 62, 63, 1, 61, 60};
		Script<u64> s{{fields(f0), fields(f1), fields(f2), fields(f3), fields(f4), fields(f5), fields(f6)}};
		CHECK(rd::alnum_string(s, 66) == "0123456789" "abcdefghij" "klmnopqrst" "uvwxyzABCD" "EFGHIJKLMN" "OPQRSTUVWX" "YZ01ZY");
		CHECK(s.drawn == 7);
	}
	// Fields 62 and 63 are dropped wherever they sit, and the surplus of the last draw is unused.
	{
		const unsigned f[10] = {62, 5, 63, 62, 6, 63, 7, 62, 8, 63};
		Script<u64> s{{fields(f)}};
		CHECK(rd::alnum_string(s, 4) == "5678");
		CHECK(s.drawn == 1);
		Script<u64> t{{fields(f), fields(f)}};
		CHECK(rd::alnum_string(t, 5) == "56785");
		CHECK(t.drawn == 2);
	}
	{
		Script<u64> none{};
		CHECK(rd::alnum_string(none, 0).empty() && none.drawn == 0);
	}

	// Against the field-by-field rule: lengths 0..300 over words that accept every field, most
	// fields, and few fields (so the bulk loop and the tail both take rejections).
	Mix m{5};
	for (int density : {0, 6, 90}) {  // percent of fields forced to 62/63; 0 = uniform
		for (std::size_t length = 0; length <= 300; ++length) {
			std::vector<u64> words(length * 20 + 10);
			for (u64& w : words) {
				w = m();
				if (density == 0) {
					unsigned f[10];
					for (unsigned& x : f) x = static_cast<unsigned>(m() % 62);  // accept-all
					w = fields(f);
				} else if (density == 6) {
					for (int k = 0; k < 10; ++k) if (m() % 100 < 6) w |= 62ull << (6 * k);
				} else {
					for (int k = 0; k < 10; ++k) if (m() % 100 < 90) w = (w & ~(63ull << (6 * k))) | ((62ull + m() % 2) << (6 * k));
				}
			}
			std::size_t want_drawn = 0;
			const std::string want = ref_string(words, length, want_drawn);
			Script<u64> s{words};
			const std::string got = rd::alnum_string(s, length);
			CHECK(got == want);
			CHECK(s.drawn == want_drawn);
			CHECK(got.find('\0') == std::string::npos);
		}
	}
	std::puts("string OK: alphabet literals, field rejection and draw counts match the rule");
}


int main() {
	test_xoshiro_vectors();
	test_seed();
	test_mulhi();
	test_bounded_exact();
	test_bounded64();
	test_real();
	test_time();
	test_string();
	std::puts("all random engine tests passed");
	return 0;
}
