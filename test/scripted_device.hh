// Test seam for random.cc: an entropy source that replays a script.
//
// The tests build random.cc a second time (library `random_scripted`) with
// -DRANDOM_ENTROPY_HEADER=<this file>, so each thread's first draw seeds from `script`
// instead of std::random_device.
#pragma once

#include <atomic>
#include <cstddef>
#include <cstdio>
#include <cstdlib>
#include <vector>

namespace scripted {

// The values the next Device calls return on this thread, and how many were used.
inline thread_local std::vector<unsigned> script;
inline thread_local std::size_t used = 0;
// Device calls across all threads.
inline std::atomic<long> calls{0};

struct Device {
	using result_type = unsigned;
	static constexpr result_type min() { return 0; }
	static constexpr result_type max() { return 0xffffffffu; }
	result_type operator()() {
		++calls;
		if (used >= script.size()) {
			std::fprintf(stderr, "FAIL: the entropy source was drawn past its script (%zu)\n", used);
			std::abort();
		}
		return script[used++];
	}
};

}  // namespace scripted

#define RANDOM_ENTROPY_SOURCE scripted::Device
