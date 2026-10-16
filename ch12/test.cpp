#include "lob/level_index.h"
#include "lob/workload.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <cstdio>
#include <map>
#include <memory_resource>
#include <optional>
#include <type_traits>
#include <unordered_map>
#include <vector>

namespace {

using namespace lob::ch12;
using lob::Price;
using lob::Qty;

// ===========================================================================
// Every candidate satisfies the one interface.
// ===========================================================================

static_assert(LevelIndex<MapIndex>);
static_assert(LevelIndex<SortedVecIndex>);
static_assert(LevelIndex<SortedVecReversedIndex>);
static_assert(LevelIndex<HashIndex>);
static_assert(LevelIndex<PmrHashIndex>);
static_assert(LevelIndex<DenseIndex>);
static_assert(LevelIndex<DenseBitmapIndex>);
static_assert(sizeof(Level) == 16);

// Without noexcept, libstdc++ stores each key's hash in its node: 40 bytes, not 32.
static_assert(std::is_nothrow_invocable_v<PriceHash, Price>);

constexpr Band kBand{100'000, 109'999};

// ===========================================================================
// One test suite, run against every index.
// ===========================================================================

template <class I>
class AnyIndex : public ::testing::Test {
protected:
    I idx{kBand};
};

using Indexes = ::testing::Types<MapIndex, SortedVecIndex, SortedVecReversedIndex, HashIndex,
                                 PmrHashIndex, DenseIndex, DenseBitmapIndex>;
TYPED_TEST_SUITE(AnyIndex, Indexes);

TYPED_TEST(AnyIndex, StartsEmpty) {
    EXPECT_EQ(this->idx.best(), std::nullopt);
    EXPECT_EQ(this->idx.levels(), 0U);
    EXPECT_EQ(this->idx.find(100'500), nullptr);
}

TYPED_TEST(AnyIndex, OrdersAtOnePriceShareALevel) {
    EXPECT_TRUE(this->idx.add(100'500, 10));
    EXPECT_TRUE(this->idx.add(100'500, 5));
    ASSERT_NE(this->idx.find(100'500), nullptr);
    EXPECT_EQ(*this->idx.find(100'500), (Level{15, 2}));
    EXPECT_EQ(this->idx.levels(), 1U);
}

TYPED_TEST(AnyIndex, RemovingTheLastOrderErasesTheLevel) {
    (void)this->idx.add(100'500, 10);
    (void)this->idx.add(100'500, 5);
    this->idx.remove(100'500, 10);
    EXPECT_EQ(*this->idx.find(100'500), (Level{5, 1}));
    this->idx.remove(100'500, 5);
    EXPECT_EQ(this->idx.find(100'500), nullptr);
    EXPECT_EQ(this->idx.levels(), 0U);
    EXPECT_EQ(this->idx.best(), std::nullopt);
}

TYPED_TEST(AnyIndex, BestIsTheHighestAndFallsBackWhenTheTopEmpties) {
    (void)this->idx.add(100'100, 1);
    (void)this->idx.add(105'000, 1);  // a gap of 4,900 empty ticks between them
    (void)this->idx.add(100'200, 1);
    EXPECT_EQ(this->idx.best(), 105'000);
    this->idx.remove(105'000, 1);
    EXPECT_EQ(this->idx.best(), 100'200);
    this->idx.remove(100'200, 1);
    EXPECT_EQ(this->idx.best(), 100'100);
}

// The differential test: replay a realistic and a sparse workload, and after
// every operation compare the candidate with a reference written inline here.
TYPED_TEST(AnyIndex, AgreesWithAReferenceOnBothWorkloads) {
    for (const Shape shape : {Shape::Clustered, Shape::Uniform}) {
        SCOPED_TRACE(shape == Shape::Clustered ? "clustered" : "uniform");
        const Workload w{make_workload(shape, 100'000, 7)};
        TypeParam idx{w.band};
        std::map<Price, Level> ref;

        for (std::size_t i{0}; i < w.ops.size(); ++i) {
            const Op& op{w.ops[i]};
            switch (op.kind) {
                case OpKind::Add:
                    ASSERT_TRUE(idx.add(op.px, op.qty));
                    ref[op.px].total_qty += op.qty;
                    ++ref[op.px].count;
                    break;
                case OpKind::Cancel:
                case OpKind::Execute: {
                    if (op.kind == OpKind::Execute) {
                        ASSERT_EQ(idx.best(), op.px) << "wrong best before execute at op " << i;
                    }
                    idx.remove(op.px, op.qty);
                    Level& lv{ref[op.px]};
                    lv.total_qty -= op.qty;
                    if (--lv.count == 0) {
                        ref.erase(op.px);
                    }
                    break;
                }
            }
            const std::optional<Price> expected{ref.empty() ? std::nullopt
                                                            : std::optional{ref.rbegin()->first}};
            ASSERT_EQ(idx.best(), expected) << "best diverged at op " << i;
            ASSERT_EQ(idx.levels(), ref.size()) << "level count diverged at op " << i;
        }
        for (const auto& [px, lv] : ref) {
            ASSERT_NE(idx.find(px), nullptr) << "missing level " << px;
            EXPECT_EQ(*idx.find(px), lv) << "wrong level " << px;
        }
    }
}

// The radix tree on its own: every query against a brute-force answer, on
// sizes that make 1, 2 and 3 levels, with bits at word and level boundaries.
TEST(OccupancyTree, PrevMatchesBruteForce) {
    for (const std::size_t n : {std::size_t{1}, std::size_t{64}, std::size_t{65}, std::size_t{4096},
                                std::size_t{4097}, std::size_t{10'000}}) {
        OccupancyTree tree{n};
        std::vector<bool> set(n, false);
        const std::vector<std::size_t> picks{0, 1, 62, 63, 64, 127, 128, 4095, 4096, 4097, 9'999};
        auto check = [&] {
            for (std::size_t i{0}; i <= n; ++i) {
                std::optional<std::size_t> expect;
                for (std::size_t j{i}; j-- > 0;) {
                    if (set[j]) { expect = j; break; }
                }
                ASSERT_EQ(tree.prev(i), expect) << "n=" << n << " i=" << i;
            }
        };
        check();
        for (const std::size_t i : picks) {
            if (i < n) { tree.set(i); set[i] = true; }
        }
        check();
        for (const std::size_t i : picks) {
            if (i < n && i % 2 == 0) { tree.clear(i); set[i] = false; }
        }
        check();
    }
}

// The property the identity hash was chosen for: the whole band, one price
// per bucket. (A scattering hash can't: the multiplicative hash this
// replaced left 2,566 of the 10,000 prices sharing a bucket.)
TEST(PriceHash, PutsEveryPriceInTheBandInItsOwnBucket) {
    std::unordered_map<Price, Level, PriceHash> m;
    m.reserve(kBand.size());
    for (Price p{kBand.min}; p <= kBand.max; ++p) {
        m.try_emplace(p);
    }
    std::size_t longest{0};
    for (std::size_t b{0}; b < m.bucket_count(); ++b) {
        longest = std::max(longest, m.bucket_size(b));
    }
    EXPECT_EQ(longest, 1U);
}

// ===========================================================================
// What breaks the dense array.
// ===========================================================================

TEST(Dense, PricesOutsideTheBandAreRejected) {
    DenseIndex idx{kBand};
    EXPECT_FALSE(idx.add(kBand.min - 1, 10));
    EXPECT_FALSE(idx.add(kBand.max + 1, 10));
    EXPECT_EQ(idx.levels(), 0U);
    EXPECT_EQ(idx.best(), std::nullopt);

    MapIndex map{kBand};  // the others take any price
    EXPECT_TRUE(map.add(kBand.max + 1'000'000, 10));
}

// ===========================================================================
// What std::pmr::monotonic_buffer_resource does with freed memory: nothing.
// ===========================================================================

// An upstream resource that only counts what is asked of it.
class CountingResource : public std::pmr::memory_resource {
public:
    std::size_t bytes{0};

private:
    void* do_allocate(std::size_t n, std::size_t align) override {
        bytes += n;
        return std::pmr::new_delete_resource()->allocate(n, align);
    }
    void do_deallocate(void* p, std::size_t n, std::size_t align) override {
        std::pmr::new_delete_resource()->deallocate(p, n, align);
    }
    bool do_is_equal(const memory_resource& o) const noexcept override { return this == &o; }
};

// Create and erase the same level 100,000 times.
std::size_t churn_bytes(std::pmr::memory_resource& resource, CountingResource& upstream) {
    const std::size_t before{upstream.bytes};
    BasicHashIndex<PmrHashMap> idx{kBand, &resource};
    for (int i{0}; i < 100'000; ++i) {
        (void)idx.add(100'500, 1);
        idx.remove(100'500, 1);
    }
    return upstream.bytes - before;
}

TEST(Pmr, MonotonicNeverReusesFreedNodesButAPoolDoes) {
    CountingResource upstream_a;
    std::pmr::monotonic_buffer_resource monotonic{&upstream_a};
    const std::size_t monotonic_bytes{churn_bytes(monotonic, upstream_a)};

    CountingResource upstream_b;
    std::pmr::unsynchronized_pool_resource pool{&upstream_b};
    const std::size_t pool_bytes{churn_bytes(pool, upstream_b)};

    std::printf("    100k create/erase of one level: monotonic %zu KiB, pool %zu KiB\n",
                monotonic_bytes / 1024, pool_bytes / 1024);
    EXPECT_GT(monotonic_bytes, 100'000U * 24);  // every node ever created is still there
    EXPECT_LT(pool_bytes, monotonic_bytes / 10);
}

// ===========================================================================
// The workload itself.
// ===========================================================================

TEST(Workload, IsDeterministicAndHasTheRequestedMix) {
    const Workload a{make_workload(Shape::Clustered, 200'000, 1)};
    const Workload b{make_workload(Shape::Clustered, 200'000, 1)};
    ASSERT_EQ(a.ops.size(), b.ops.size());
    std::size_t counts[3]{};
    for (std::size_t i{0}; i < a.ops.size(); ++i) {
        ASSERT_EQ(a.ops[i].kind, b.ops[i].kind);
        ASSERT_EQ(a.ops[i].px, b.ops[i].px);
        ASSERT_TRUE(a.band.contains(a.ops[i].px));
        ++counts[static_cast<int>(a.ops[i].kind)];
    }
    EXPECT_NEAR(counts[0] / 2000.0, 70.0, 1.0);  // percent
    EXPECT_NEAR(counts[1] / 2000.0, 25.0, 1.0);
    EXPECT_NEAR(counts[2] / 2000.0, 5.0, 1.0);
}

}  // namespace