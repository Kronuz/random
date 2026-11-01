#!/usr/bin/env python3
"""Break the engine in many ways and require the tests to notice.

Each mutant edits one line of random_engine.hh or random.cc in a scratch copy of the
repository, builds the test programs from that copy and runs them. A mutant is killed when
every program builds and at least one test fails. The script exits non-zero if any mutant
survives, stops building, or no longer matches its source line, so a test that stops
pinning a property shows up here.

usage: tools/mutants.py [--cxx c++] [--jobs N] [--filter SUBSTRING]
"""
import argparse
import concurrent.futures
import os
import shutil
import subprocess
import sys
import tempfile

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

# (id, file, old text (exactly once), new text, what it breaks)
MUTANTS = [
    ("lemire-no-reject", "random_engine.hh", "while (lo < threshold) {", "while (false && lo < threshold) {",
     "drops the rejection loop: biased"),
    ("lemire-off-by-one", "random_engine.hh", "while (lo < threshold) {", "while (lo + 1 < threshold) {",
     "accepts the word whose low half equals threshold - 1"),
    ("lemire-threshold", "random_engine.hh", "static_cast<W>(0 - range) % range", "static_cast<W>(0 - range)",
     "threshold without the modulo"),
    ("lemire-slow-path", "random_engine.hh", "if (lo < range) [[unlikely]] {", "if (lo < range / 2) [[unlikely]] {",
     "skips the rejection test for part of the slow path"),
    ("inclusive-excludes-last", "random_engine.hh", "bounded(r, static_cast<W>(span + 1))",
     "bounded(r, static_cast<W>(span ? span : 1))", "range off by one"),
    ("inclusive-full-range", "random_engine.hh", "if (span == std::numeric_limits<W>::max()) {",
     "if (span == std::numeric_limits<W>::max() - 1) {", "full-width range handled one value early"),
    ("xoshiro-rotl-23", "random_engine.hh", "rotl(s[0] + s[3], 23)", "rotl(s[0] + s[3], 22)", "output rotation"),
    ("xoshiro-rotl-45", "random_engine.hh", "s[3] = rotl(s[3], 45);", "s[3] = rotl(s[3], 44);", "state rotation"),
    ("xoshiro-shift-17", "random_engine.hh", "s[1] << 17", "s[1] << 16", "state shift"),
    ("xoshiro-xor-order", "random_engine.hh", "s[2] ^= s[0];", "s[2] ^= s[1];", "state update"),
    ("xoshiro-result", "random_engine.hh", "rotl(s[0] + s[3], 23) + s[0]", "rotl(s[0] + s[3], 23) + s[1]", "output sum"),
    ("seed-32-bits", "random_engine.hh", "const std::uint64_t hi = static_cast<std::uint32_t>(source());",
     "const std::uint64_t hi = 0;", "only 32 bits of entropy per word"),
    ("seed-half-order", "random_engine.hh", "w = (hi << 32) | lo;", "w = (lo << 32) | hi;", "halves swapped"),
    ("seed-zero-test", "random_engine.hh", "while (!(s[0] | s[1] | s[2] | s[3]));", "while (!(s[0] & s[1] & s[2] & s[3]));",
     "redraws states that are not all-zero"),
    ("seed-three-words", "random_engine.hh", "for (auto& w : s) {", "for (auto& w : s) { if (&w == &s[3]) continue;",
     "leaves the fourth word unseeded"),
    ("mulhi-p01", "random_engine.hh", "return p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);",
     "return p11 + (p10 >> 32) + (mid >> 32);", "portable multiply drops a partial product"),
    ("mulhi-carry", "random_engine.hh", "return p11 + (p01 >> 32) + (p10 >> 32) + (mid >> 32);",
     "return p11 + (p01 >> 32) + (p10 >> 32);", "portable multiply drops the carry"),
    ("mulhi-int128", "random_engine.hh", "return static_cast<std::uint64_t>(m >> 64);",
     "return static_cast<std::uint64_t>(m >> 63);", "native multiply takes the wrong half"),
    ("real-52-bits", "random_engine.hh", "r.next() >> 11", "r.next() >> 12", "52 random bits"),
    ("real-scale", "random_engine.hh", "* 0x1p-53;", "* 0x1p-52;", "scale"),
    ("time-offset", "random_engine.hh", "hi - lo) + lo)", "hi - lo))", "forgets the start of the range"),
    ("string-accept-62-bulk", "random_engine.hh", "n += (v < 62);", "n += (v < 63);", "writes a NUL into the output"),
    ("string-reject-61-bulk", "random_engine.hh", "n += (v < 62);", "n += (v < 61);", "bias against the last symbol"),
    ("string-accept-62-tail", "random_engine.hh", "if (v < 62) {", "if (v < 63) {", "tail writes a NUL"),
    ("string-reject-61-tail", "random_engine.hh", "if (v < 62) {", "if (v < 61) {", "tail bias"),
    ("string-alphabet", "random_engine.hh", "'a','b','c',", "'b','a','c',", "two symbols swapped"),
    ("string-bulk-bound", "random_engine.hh", "while (remaining >= 10) {", "while (remaining >= 9) {",
     "bulk loop writes past the end"),
    ("tls-reseed-every-call", "random.cc", "if (!r.seeded) [[unlikely]] {", "if (true) {", "seeds on every draw"),
    ("tls-shared-state", "random.cc", "constinit thread_local random_detail::Xoshiro256pp tls_rng{};",
     "constinit random_detail::Xoshiro256pp tls_rng{};", "one state shared by all threads"),
    ("tls-seed-source", "random.cc", "r.seed(source);",
     "{ source(); r.s[0] = 1; r.s[1] = 2; r.s[2] = 3; r.s[3] = 4; r.seeded = true; }", "constant seed"),
]

# test program -> (sources, extra flags)
TESTS = [
    ("random_test", ["test/test.cc", "random.cc"], ["-UNDEBUG"]),
    ("random_engine_test", ["test/engine_test.cc"], []),
    ("random_engine_portable_test", ["test/engine_test.cc"], ["-DRANDOM_NO_INT128"]),
    ("random_seed_test", ["test/seed_test.cc", "random.cc"], ['-DRANDOM_ENTROPY_HEADER="@ROOT@/test/scripted_device.hh"']),
]


def run(cmd):
    return subprocess.run(cmd, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)


def trial(cxx, work):
    """Build and run every test program in `work`; return (names of programs that failed, build error)."""
    failed = []
    for name, sources, flags in TESTS:
        exe = os.path.join(work, name)
        flags = [f.replace("@ROOT@", work) for f in flags]
        cmd = [cxx, "-std=c++20", "-O2", "-I" + work, "-I" + os.path.join(work, "test"), "-o", exe] + flags
        cmd += [os.path.join(work, s) for s in sources] + ["-pthread"]
        build = run(cmd)
        if build.returncode != 0:
            return None, build.stdout
        if run([exe]).returncode != 0:
            failed.append(name)
    return failed, ""


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--cxx", default=os.environ.get("CXX", "c++"))
    ap.add_argument("--jobs", type=int, default=3)
    ap.add_argument("--filter", default="")
    args = ap.parse_args()

    with tempfile.TemporaryDirectory(prefix="random-mutants-") as tmp:
        def fresh(name):
            work = os.path.join(tmp, name)
            shutil.copytree(ROOT, work, ignore=shutil.ignore_patterns(".git", "build*", "*.o"))
            return work

        failed, err = trial(args.cxx, fresh("clean"))
        if failed is None or failed:
            print("the unmodified sources do not pass: " + ("build failed\n" + err if failed is None else " ".join(failed)))
            return 2
        print("clean: all tests pass", flush=True)

        def one(m):
            mid, fname, old, new, what = m
            work = fresh(mid)
            path = os.path.join(work, fname)
            text = open(path).read()
            if text.count(old) != 1:
                return mid, "STALE", "%d matches of %r" % (text.count(old), old)
            open(path, "w").write(text.replace(old, new))
            failed, err = trial(args.cxx, work)
            shutil.rmtree(work)
            if failed is None:
                lines = [l for l in err.splitlines() if "error" in l]
                return mid, "BROKEN", lines[0] if lines else err[-200:]
            if failed:
                return mid, "killed", "by %s (%s)" % (", ".join(failed), what)
            return mid, "SURVIVES", what

        todo = [m for m in MUTANTS if args.filter in m[0]]
        bad = []
        with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
            for mid, status, detail in pool.map(one, todo):
                print("%-24s %-8s %s" % (mid, status, detail), flush=True)
                if status != "killed":
                    bad.append(mid)

    if bad:
        print("\n%d mutants survive, are stale or do not build: %s" % (len(bad), " ".join(bad)))
        return 1
    print("\nall %d mutants killed" % len(todo))
    return 0


if __name__ == "__main__":
    sys.exit(main())
