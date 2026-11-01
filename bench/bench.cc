// Benchmark: the library against a frozen copy of Xapiand's implementation
// (thread-local std::mt19937_64 + std::uniform_*_distribution, push_back string).
// The frozen copy is noinline, so both sides cost one call into a separate unit.
// Prints the median of 9 batches per row and the batch spread, (max - min) / median.
// Build: cmake -B build -DCMAKE_BUILD_TYPE=Release -DRANDOM_BUILD_BENCH=ON
#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>

#include "random.hh"

#if defined(_MSC_VER)
#define NOINLINE __declspec(noinline)
#else
#define NOINLINE __attribute__((noinline))
#endif

namespace old_impl {
static auto& rng() {
	static thread_local std::mt19937_64 rng(std::random_device{}());
	return rng;
}
NOINLINE double random_real(double a, double b) { std::uniform_real_distribution<double> d(a, b); return d(rng()); }
NOINLINE std::uint64_t random_int(std::uint64_t a, std::uint64_t b) { std::uniform_int_distribution<std::uint64_t> d(a, b); return d(rng()); }
NOINLINE std::chrono::milliseconds random_time(std::chrono::milliseconds a, std::chrono::milliseconds b) {
	std::uniform_int_distribution<std::chrono::milliseconds::rep> d(a.count(), b.count());
	return std::chrono::milliseconds(d(rng()));
}
NOINLINE std::string random_string(size_t length) {
	static auto& chrs = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
	static thread_local std::uniform_int_distribution<std::string::size_type> pick(0, sizeof(chrs) - 2);
	std::string s; s.reserve(length);
	auto& r = rng();
	while (length--) s.push_back(chrs[pick(r)]);
	return s;
}
}

struct Timing { double median, spread; };

template <class F>
static Timing ns_per(F&& f, int batch) {
	double t[9];
	for (double& ns : t) {
		auto t0 = std::chrono::steady_clock::now();
		std::uint64_t sink = 0;
		for (int i = 0; i < batch; ++i) sink += f(i);
		auto t1 = std::chrono::steady_clock::now();
#if defined(__GNUC__)
		asm volatile("" :: "r"(sink));
#endif
		ns = std::chrono::duration<double, std::nano>(t1 - t0).count() / batch;
	}
	std::sort(t, t + 9);
	return {t[4], (t[8] - t[0]) / t[4]};
}

static void row(const char* name, Timing o, Timing n) {
	std::printf("%-24s old %8.2f ns (%4.1f%%)  new %8.2f ns (%4.1f%%)  %5.2fx\n",
	            name, o.median, o.spread * 100, n.median, n.spread * 100, o.median / n.median);
}

int main() {
	using namespace std::chrono_literals;
	const int B = 2000000;
	row("random_int(0,999999)",
	    ns_per([](int) { return old_impl::random_int(0, 999999); }, B),
	    ns_per([](int) { return random_int(0, 999999); }, B));
	row("random_int(0,2^64-1)",
	    ns_per([](int) { return old_impl::random_int(0, ~0ull); }, B),
	    ns_per([](int) { return random_int(0, ~0ull); }, B));
	row("random_int(1,6)",
	    ns_per([](int) { return old_impl::random_int(1, 6); }, B),
	    ns_per([](int) { return random_int(1, 6); }, B));
	row("random_real(0,1)",
	    ns_per([](int) { return (std::uint64_t)(old_impl::random_real(0, 1) * 1e9); }, B),
	    ns_per([](int) { return (std::uint64_t)(random_real(0, 1) * 1e9); }, B));
	row("random_time(100ms,500ms)",
	    ns_per([](int) { return (std::uint64_t)old_impl::random_time(100ms, 500ms).count(); }, B),
	    ns_per([](int) { return (std::uint64_t)random_time(100ms, 500ms).count(); }, B));
	for (size_t n : {1, 8, 16, 32, 64, 256}) {
		char name[32]; std::snprintf(name, sizeof name, "random_string(%zu)", n);
		const int b = n <= 16 ? 1000000 : 200000;
		row(name,
		    ns_per([n](int) { return (std::uint64_t)old_impl::random_string(n)[n - 1]; }, b),
		    ns_per([n](int) { return (std::uint64_t)random_string(n)[n - 1]; }, b));
	}
}
