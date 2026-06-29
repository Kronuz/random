// A runnable tour of random.
//
// Build (when this repo is the top-level project):
//   cmake -B build && cmake --build build && ./build/random_demo
//
// The one idea worth taking away: four small generators sit on top of a single
// thread-local Mersenne twister seeded from std::random_device. random_real and
// random_int give you a uniform draw in a range, random_time does the same for a
// std::chrono::milliseconds duration, and random_string hands back a random
// alphanumeric string. The values change every run; that is the point.
#include <chrono>
#include <cstdio>

#include "random.hh"

static void rule(const char* title) {
	std::printf("\n\033[1m── %s ──\033[0m\n", title);
}

int main() {
	std::puts("random demo");

	// --- 1. a real in [a, b] -------------------------------------------------
	rule("random_real(a, b): a uniform real in [a, b]");
	// Useful for probabilities and jitter factors. Here, a few draws in [0, 1).
	for (int i = 0; i < 5; ++i) {
		std::printf("  %.6f\n", random_real(0.0, 1.0));
	}

	// --- 2. an integer in [a, b] (inclusive) ---------------------------------
	rule("random_int(a, b): a uniform integer in [a, b]");
	// A dice roll: 1..6, inclusive on both ends.
	std::printf("  dice:");
	for (int i = 0; i < 10; ++i) {
		std::printf(" %llu", static_cast<unsigned long long>(random_int(1, 6)));
	}
	std::printf("\n");

	// --- 3. a duration in [a, b] ---------------------------------------------
	rule("random_time(a, b): a uniform std::chrono::milliseconds in [a, b]");
	// Handy for randomized backoff: a wait somewhere between 100ms and 500ms.
	using namespace std::chrono;
	for (int i = 0; i < 5; ++i) {
		auto d = random_time(milliseconds{100}, milliseconds{500});
		std::printf("  backoff: %lld ms\n", static_cast<long long>(d.count()));
	}

	// --- 4. a random alphanumeric string -------------------------------------
	rule("random_string(n): n characters from [0-9A-Za-z]");
	// Good for throwaway ids and tokens.
	std::printf("  id(8)  : %s\n", random_string(8).c_str());
	std::printf("  id(16) : %s\n", random_string(16).c_str());
	std::printf("  id(32) : %s\n", random_string(32).c_str());

	std::puts("\ndone.");
	return 0;
}
