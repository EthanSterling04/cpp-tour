// Ch. 5 -- what does a virtual call cost?
//
// Run from build-bench only:
//   ./build-bench/ch05/ch05_bench
#include "lob/policy.h"

#include <benchmark/benchmark.h>

#include <cstddef>
#include <memory>
#include <random>
#include <vector>

namespace {

using lob::Qty;

// ===========================================================================
// Part 1 -- the cost of dispatch itself.
//
// The function body is one comparison, so the call mechanism is nearly the
// only thing being measured. Each benchmark makes 4096 calls per iteration
// over the same inputs; items_per_second turns that into a per-call cost.
// ===========================================================================

struct Capper {
    virtual ~Capper() = default;
    virtual Qty cap(Qty incoming, Qty available) const = 0;
};

struct MinCapper final : Capper {
    Qty cap(Qty incoming, Qty available) const override {
        return incoming < available ? incoming : available;
    }
};

struct HalfCapper final : Capper {
    Qty cap(Qty incoming, Qty available) const override {
        return (incoming < available ? incoming : available) / 2;
    }
};

constexpr std::size_t kCalls{4096};

const std::vector<Qty>& inputs() {
    static const std::vector<Qty> data = [] {
        std::mt19937 rng{7};
        std::uniform_int_distribution<Qty> qty{1, 1000};
        std::vector<Qty> v(kCalls + 1);
        for (Qty& x : v) {
            x = qty(rng);
        }
        return v;
    }();
    return data;
}

// One object per call, so all three benchmarks touch the same amount of
// memory. `mixed` alternates the two types at random: the indirect branch
// can no longer be predicted.
std::vector<std::unique_ptr<Capper>> make_cappers(bool mixed) {
    std::vector<std::unique_ptr<Capper>> v;
    v.reserve(kCalls);
    std::mt19937 rng{11};
    for (std::size_t i{0}; i < kCalls; ++i) {
        if (mixed && (rng() & 1U) != 0) {
            v.push_back(std::make_unique<HalfCapper>());
        } else {
            v.push_back(std::make_unique<MinCapper>());
        }
    }
    return v;
}

// Direct: the compiler knows the type, inlines cap(), and can vectorise the
// whole loop. This is the baseline a virtual call takes away.
void BM_Dispatch_Direct(benchmark::State& state) {
    const auto& in = inputs();
    const MinCapper capper;
    for (auto _ : state) {
        Qty sum{0};
        for (std::size_t i{0}; i < kCalls; ++i) {
            sum += capper.cap(in[i], in[i + 1]);
        }
        benchmark::DoNotOptimize(sum);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kCalls));
}

void run_virtual(benchmark::State& state, bool mixed) {
    const auto& in = inputs();
    const auto cappers = make_cappers(mixed);
    for (auto _ : state) {
        Qty sum{0};
        for (std::size_t i{0}; i < kCalls; ++i) {
            sum += cappers[i]->cap(in[i], in[i + 1]);
        }
        benchmark::DoNotOptimize(sum);
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(kCalls));
}

// Virtual, every object the same type: the branch predictor learns the
// target, so the indirect jump is nearly free. The cost is lost inlining.
void BM_Dispatch_Virtual_OneType(benchmark::State& state) { run_virtual(state, false); }

// Virtual, types mixed at random: now the jump target is unpredictable.
void BM_Dispatch_Virtual_MixedTypes(benchmark::State& state) { run_virtual(state, true); }

BENCHMARK(BM_Dispatch_Direct);
BENCHMARK(BM_Dispatch_Virtual_OneType);
BENCHMARK(BM_Dispatch_Virtual_MixedTypes);

// ===========================================================================
// Part 2 -- the same question for the real allocate().
//
// allocate() is defined in policy.cpp, a different translation unit, so even
// the "direct" call cannot be inlined. This pair isolates exactly one thing:
// a direct call versus an indirect one, around a function doing real work.
// ===========================================================================

lob::PriceLevel sample_level() {
    std::mt19937 rng{3};
    std::uniform_int_distribution<Qty> qty{1, 500};
    lob::PriceLevel level{10'000};
    for (lob::OrderId id{1}; id <= 20; ++id) {
        level.add(id, qty(rng));
    }
    return level;
}

void BM_Allocate_Direct(benchmark::State& state) {
    const lob::PriceLevel original{sample_level()};
    lob::ProRataPolicy policy;  // static type known and final: no virtual dispatch
    for (auto _ : state) {
        lob::PriceLevel level{original};
        benchmark::DoNotOptimize(policy.allocate(2500, level));
    }
}

void BM_Allocate_Virtual(benchmark::State& state) {
    const lob::PriceLevel original{sample_level()};
    const std::unique_ptr<lob::MatchingPolicy> policy{std::make_unique<lob::ProRataPolicy>()};
    // Without this, the compiler can see the make_unique above, prove the
    // dynamic type, and turn the virtual call back into a direct one.
    benchmark::DoNotOptimize(policy.get());
    for (auto _ : state) {
        lob::PriceLevel level{original};
        benchmark::DoNotOptimize(policy->allocate(2500, level));
    }
}

BENCHMARK(BM_Allocate_Direct);
BENCHMARK(BM_Allocate_Virtual);

}  // namespace