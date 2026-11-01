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

#include "random_engine.hh"

// The entropy source seeds each thread's state. The tests build this file with their own
// (RANDOM_ENTROPY_HEADER names a header that defines RANDOM_ENTROPY_SOURCE) to script it.
#ifdef RANDOM_ENTROPY_HEADER
#include RANDOM_ENTROPY_HEADER
#else
#include <random>                // for std::random_device
#define RANDOM_ENTROPY_SOURCE std::random_device
#endif


namespace {

// Constant-initialised POD thread_local: no dynamic-init guard on access, just
// a "seeded" flag test. One independent generator per thread, no locking.
constinit thread_local random_detail::Xoshiro256pp tls_rng{};

// The first draw on a thread: kept out of line so the draw paths stay small.
RANDOM_NOINLINE void seed_thread(random_detail::Xoshiro256pp& r) {
	RANDOM_ENTROPY_SOURCE source;
	r.seed(source);
}

inline random_detail::Xoshiro256pp& rng() {
	auto& r = tls_rng;
	if (!r.seeded) [[unlikely]] {
		seed_thread(r);
	}
	return r;
}

}  // namespace


double
random_real(double initial, double last)
{
	return random_detail::real_between(rng(), initial, last);
}


std::uint64_t
random_int(std::uint64_t initial, std::uint64_t last)
{
	return random_detail::inclusive(rng(), initial, last);
}


std::chrono::milliseconds
random_time(std::chrono::milliseconds initial, std::chrono::milliseconds last)
{
	return random_detail::time_between(rng(), initial, last);
}


std::string
random_string(size_t length)
{
	return random_detail::alnum_string(rng(), length);
}
