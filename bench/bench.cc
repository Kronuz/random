// Benchmark: the library against a frozen copy of the previous implementation
// (thread-local std::mt19937_64 + std::uniform_*_distribution, push_back string).
// Build: cmake -B build -DCMAKE_BUILD_TYPE=Release -DRANDOM_BUILD_BENCH=ON
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include <string>

#include "random.hh"

namespace old_impl {
static auto& rng() {
	static thread_local std::mt19937_64 rng(std::random_device{}());
	return rng;
}
double random_real(double a, double b) { std::uniform_real_distribution<double> d(a, b); return d(rng()); }
std::uint64_t random_int(std::uint64_t a, std::uint64_t b) { std::uniform_int_distribution<std::uint64_t> d(a, b); return d(rng()); }
std::string random_string(size_t length) {
	static auto& chrs = "0123456789abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ";
	static thread_local std::uniform_int_distribution<std::string::size_type> pick(0, sizeof(chrs) - 2);
	std::string s; s.reserve(length);
	auto& r = rng();
	while (length--) s.push_back(chrs[pick(r)]);
	return s;
}
}

template <class F>
static double ns_per(F&& f, int batch) {
	double best = 1e300;
	for (int w = 0; w < 7; ++w) {
		auto t0 = std::chrono::steady_clock::now();
		std::uint64_t sink = 0;
		for (int i = 0; i < batch; ++i) sink += f(i);
		auto t1 = std::chrono::steady_clock::now();
		asm volatile("" :: "r"(sink));
		best = std::min(best, std::chrono::duration<double, std::nano>(t1 - t0).count() / batch);
	}
	return best;
}

int main() {
	auto row = [](const char* name, double o, double n) { std::printf("%-26s old %8.2f ns  new %8.2f ns  %5.2fx\n", name, o, n, o / n); };
	const int B = 2000000;
	row("random_int(0,999999)",
	    ns_per([](int) { return old_impl::random_int(0, 999999); }, B),
	    ns_per([](int) { return random_int(0, 999999); }, B));
	row("random_int(0,2^64-1)",
	    ns_per([](int) { return old_impl::random_int(0, ~0ull); }, B),
	    ns_per([](int) { return random_int(0, ~0ull); }, B));
	row("random_real(0,1)",
	    ns_per([](int) { return (std::uint64_t)(old_impl::random_real(0, 1) * 1e9); }, B),
	    ns_per([](int) { return (std::uint64_t)(random_real(0, 1) * 1e9); }, B));
	for (size_t n : {8, 32, 256}) {
		char name[32]; std::snprintf(name, sizeof name, "random_string(%zu)", n);
		int b = 200000;
		row(name,
		    ns_per([n](int) { return (std::uint64_t)old_impl::random_string(n)[n - 1]; }, b),
		    ns_per([n](int) { return (std::uint64_t)random_string(n)[n - 1]; }, b));
	}
}
