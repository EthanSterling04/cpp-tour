// Ch. 12 -- the bake-off. Run from build-bench only, pinned to one core:
//   taskset -c 2 ./build-bench/ch12/ch12_bench
//       --benchmark_repetitions=5 --benchmark_report_aggregates_only=true
// (One command; a backslash at the end of a // comment line continues the
// comment onto the next line, which -Werror rejects.)
#include "lob/level_index.h"
#include "lob/workload.h"

#include <benchmark/benchmark.h>

#include <cstdint>

namespace {

using namespace lob::ch12;
using lob::Price;

// Built once per shape, outside every timed region, shared by every index.
const Workload& workload(Shape shape) {
    static const Workload clustered{make_workload(Shape::Clustered, 1'000'000, 42)};
    static const Workload uniform{make_workload(Shape::Uniform, 1'000'000, 42)};
    return shape == Shape::Clustered ? clustered : uniform;
}

// ===========================================================================
// 1. Throughput: one million mixed operations, start to finish.
//    items_per_second is the ops/sec figure.
// ===========================================================================

template <class Index, Shape S>
void BM_Workload(benchmark::State& state) {
    const Workload& w{workload(S)};
    for (auto _ : state) {
        Index idx{w.band};  // construction is timed too: well under 1% of a run
        benchmark::DoNotOptimize(replay(idx, w.ops));
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(w.ops.size()));
}

#define WORKLOAD(Index)                                                   \
    BENCHMARK_TEMPLATE(BM_Workload, Index, Shape::Clustered)->Unit(benchmark::kMillisecond); \
    BENCHMARK_TEMPLATE(BM_Workload, Index, Shape::Uniform)->Unit(benchmark::kMillisecond)

WORKLOAD(MapIndex);
WORKLOAD(SortedVecIndex);
WORKLOAD(SortedVecReversedIndex);
WORKLOAD(HashIndex);
WORKLOAD(PmrHashIndex);
WORKLOAD(DenseIndex);
WORKLOAD(DenseBitmapIndex);

// ===========================================================================
// 2. Best-level lookup on a full book: the index as left by the clustered run.
// ===========================================================================

template <class Index>
void BM_BestLookup(benchmark::State& state) {
    const Workload& w{workload(Shape::Clustered)};
    Index idx{w.band};
    replay(idx, w.ops);
    for (auto _ : state) {
        benchmark::DoNotOptimize(idx.best());
    }
}

BENCHMARK_TEMPLATE(BM_BestLookup, MapIndex);
BENCHMARK_TEMPLATE(BM_BestLookup, SortedVecIndex);
BENCHMARK_TEMPLATE(BM_BestLookup, SortedVecReversedIndex);
BENCHMARK_TEMPLATE(BM_BestLookup, HashIndex);
BENCHMARK_TEMPLATE(BM_BestLookup, PmrHashIndex);
BENCHMARK_TEMPLATE(BM_BestLookup, DenseIndex);
BENCHMARK_TEMPLATE(BM_BestLookup, DenseBitmapIndex);

// ===========================================================================
// 3. The top level appears and empties, gap ticks above the next level.
//    This is where best-tracking costs live: the dense array and the hash
//    map must walk `gap` ticks down to find the new best.
//    256 levels sit below, so a vector with its best at the front has to
//    shift all of them on every insert and erase at the top.
// ===========================================================================

template <class Index>
void BM_TopChurn(benchmark::State& state) {
    const Band  band{100'000, 109'999};
    const Price floor_px{101'000};
    const Price top{floor_px + state.range(0)};
    Index       idx{band};
    for (Price p{floor_px - 255}; p <= floor_px; ++p) {
        (void)idx.add(p, 1);
    }
    for (auto _ : state) {
        (void)idx.add(top, 1);   // a new best level appears
        benchmark::DoNotOptimize(idx.best());
        idx.remove(top, 1);      // ...and empties: the best falls back by `gap`
        benchmark::DoNotOptimize(idx.best());
    }
}

#define CHURN(Index) BENCHMARK_TEMPLATE(BM_TopChurn, Index)->ArgName("gap")->Arg(1)->Arg(64)->Arg(4096)->Arg(8000)

CHURN(MapIndex);
CHURN(SortedVecIndex);
CHURN(SortedVecReversedIndex);
CHURN(HashIndex);
CHURN(PmrHashIndex);
CHURN(DenseIndex);
CHURN(DenseBitmapIndex);

}  // namespace