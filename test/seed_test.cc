// Tests of the thread-local seeding in random.cc, through the public API.
//
// This links `random_scripted`: random.cc built with a scripted entropy source
// (test/scripted_device.hh), so the first draw on a thread consumes a known script.
//   - seeding is lazy: nothing is drawn from the source until a thread's first draw;
//   - each thread seeds exactly once, with eight 32-bit words (256 bits), high half first;
//   - the state is the script and the draws are xoshiro256++ of that state (the reference
//     vectors for {1, 2, 3, 4}); an all-zero state is redrawn;
//   - threads do not share state: eight threads drawing at once each see their own stream;
//   - a new thread seeds again.
// random_int(0, UINT64_MAX) returns the raw 64-bit draw.
#include "scripted_device.hh"
#include "random.hh"

#include <atomic>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

using u64 = std::uint64_t;

#define CHECK(c) do { if (!(c)) { std::fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); std::abort(); } } while (0)

// The reference outputs for state {1, 2, 3, 4} (xoshiro256plusplus.c, Blackman and Vigna).
static const u64 kVectors[8] = {41943041ull, 58720359ull, 3588806011781223ull, 3591011842654386ull,
                                9228616714210784205ull, 9973669472204895162ull, 14011001112246962877ull, 12406186145184390807ull};

static void push_state(const u64 (&w)[4]) {
	for (u64 x : w) {
		scripted::script.push_back(static_cast<unsigned>(x >> 32));
		scripted::script.push_back(static_cast<unsigned>(x));
	}
}

// The xoshiro256++ step, written out here so the per-thread streams have an expected value.
struct Ref {
	u64 s[4];
	static u64 rotl(u64 x, int k) { return (x << k) | (x >> (64 - k)); }
	u64 next() {
		const u64 r = rotl(s[0] + s[3], 23) + s[0];
		const u64 t = s[1] << 17;
		s[2] ^= s[0]; s[3] ^= s[1]; s[1] ^= s[2]; s[0] ^= s[3]; s[2] ^= t; s[3] = rotl(s[3], 45);
		return r;
	}
};

int main() {
	// Lazy, once, eight words, the reference stream; an all-zero seed is redrawn first.
	{
		std::thread t([] {
			const u64 zero[4] = {0, 0, 0, 0}, one[4] = {1, 2, 3, 4};
			push_state(zero);
			push_state(one);
			CHECK(scripted::calls.load() == 0);  // lazy: the thread has not drawn yet
			for (u64 want : kVectors) CHECK(random_int(0, ~0ull) == want);
			CHECK(scripted::used == 16);  // the zero state, then eight words
			Ref ref{{1, 2, 3, 4}};
			for (int i = 0; i < 8; ++i) ref.next();  // the eight vectors above
			for (int i = 0; i < 100000; ++i) CHECK(random_int(0, ~0ull) == ref.next());
			CHECK(scripted::used == 16);  // seeded once
			// The other functions draw from the same stream.
			CHECK(random_string(10).size() == 10 && scripted::used == 16);
		});
		t.join();
	}
	// Eight threads at once, each with its own seed: every stream is its own, in order.
	{
		const int T = 8, N = 200000;
		std::vector<std::vector<u64>> got(T, std::vector<u64>(N));
		std::vector<std::thread> ts;
		std::atomic<int> go{0};
		auto seed_for = [](int t, u64 (&w)[4]) {
			u64 x = 0x9e3779b97f4a7c15ull * (t + 1);
			for (u64& v : w) {
				x ^= x >> 30; x *= 0xbf58476d1ce4e5b9ull; x ^= x >> 27;
				v = x ? x : 1;
				x += 0x9e3779b97f4a7c15ull;
			}
		};
		for (int t = 0; t < T; ++t) {
			ts.emplace_back([t, N, &got, &go, &seed_for] {
				u64 w[4];
				seed_for(t, w);
				push_state(w);
				while (!go.load()) {}
				for (int i = 0; i < N; ++i) got[t][i] = random_int(0, ~0ull);
				CHECK(scripted::used == 8);
			});
		}
		go = 1;
		for (auto& t : ts) t.join();
		for (int t = 0; t < T; ++t) {
			u64 w[4];
			seed_for(t, w);
			Ref ref{{w[0], w[1], w[2], w[3]}};
			for (int i = 0; i < N; ++i) CHECK(got[t][i] == ref.next());
		}
	}
	// A new thread seeds again: eight more calls.
	{
		const long before = scripted::calls.load();
		std::thread t([] {
			const u64 w[4] = {5, 6, 7, 8};
			push_state(w);
			Ref ref{{5, 6, 7, 8}};
			CHECK(random_int(0, ~0ull) == ref.next());
		});
		t.join();
		CHECK(scripted::calls.load() == before + 8);
	}
	std::puts("seed OK: lazy, once per thread, eight 32-bit words, per-thread streams");
	return 0;
}
