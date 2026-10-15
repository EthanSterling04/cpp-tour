// Ch. 15 -- what does copying a shared_ptr cost, versus moving a unique_ptr?
// Run from build-bench only.
#include <benchmark/benchmark.h>

#include <memory>
#include <thread>
#include <utility>

namespace {

struct Payload { long value{42}; };

// libstdc++ checks glibc's __libc_single_threaded flag before touching a
// shared_ptr's counts. Until a second thread has EVER existed, it uses plain
// increments, and a copy looks almost free. A real book is multithreaded
// (Ch 18), so start and join one thread before any benchmark runs. Delete
// these lines to see the single-threaded fast path.
const bool kProcessIsMultithreaded = [] {
    std::thread([] {}).join();
    return true;
}();

// Copy, then let the copy die: one atomic increment and one atomic
// decrement, both on the control block's counter, on one thread.
void BM_SharedPtr_CopyAndDestroy(benchmark::State& state) {
    const auto source{std::make_shared<Payload>()};
    for (auto _ : state) {
        std::shared_ptr<Payload> copy{source};
        benchmark::DoNotOptimize(copy);
    }
}

// Move there and back: copy one pointer, null another. No counter at all.
void BM_UniquePtr_MoveAndBack(benchmark::State& state) {
    auto a{std::make_unique<Payload>()};
    for (auto _ : state) {
        auto b{std::move(a)};
        benchmark::DoNotOptimize(b);
        a = std::move(b);
    }
}

// The same shared_ptr copied from two threads at once. Both atomics hit one
// cache line, which now bounces between cores on every increment.
void BM_SharedPtr_CopyContended(benchmark::State& state) {
    static std::shared_ptr<Payload> shared{std::make_shared<Payload>()};
    for (auto _ : state) {
        std::shared_ptr<Payload> copy{shared};
        benchmark::DoNotOptimize(copy);
    }
}

BENCHMARK(BM_SharedPtr_CopyAndDestroy);
BENCHMARK(BM_UniquePtr_MoveAndBack);
BENCHMARK(BM_SharedPtr_CopyContended)->Threads(1)->Threads(2);

}  // namespace