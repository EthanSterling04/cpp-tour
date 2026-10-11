#include "lob/policy.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <concepts>
#include <cstdint>
#include <memory>
#include <ostream>
#include <random>
#include <string>
#include <type_traits>
#include <vector>

// Readable failure messages: GoogleTest finds PrintTo by argument-dependent
// lookup, so it must live in the same namespace as the type it prints.
namespace lob {
void PrintTo(const Allocation& a, std::ostream* os) {
    *os << "{id " << a.id << ", qty " << a.qty << "}";
}
}  // namespace lob

namespace {

using lob::Allocation;
using lob::FifoPolicy;
using lob::Fill;
using lob::MatchingPolicy;
using lob::OrderId;
using lob::PriceLevel;
using lob::ProRataPolicy;
using lob::Qty;

// ===========================================================================
// The shape of the hierarchy, checked at compile time.
// ===========================================================================

static_assert(std::is_abstract_v<MatchingPolicy>);
static_assert(std::has_virtual_destructor_v<MatchingPolicy>);
static_assert(std::is_final_v<FifoPolicy> && std::is_final_v<ProRataPolicy>);
static_assert(std::is_copy_constructible_v<FifoPolicy>);  // derived copying still works

// PriceLevel is a regular value type: copy it, compare it, like an int.
static_assert(std::copyable<PriceLevel>);
static_assert(std::equality_comparable<PriceLevel>);

// ===========================================================================
// Helpers
// ===========================================================================

// Orders get ids 1, 2, 3, ... in the order given: that is their time priority.
PriceLevel make_level(std::initializer_list<Qty> quantities) {
    PriceLevel level{10'000};
    OrderId id{1};
    for (const Qty q : quantities) {
        level.add(id++, q);
    }
    return level;
}

// Every promise PriceLevel makes, checked from the outside.
void expect_invariants(const PriceLevel& level) {
    std::uint64_t sum{0};
    for (const auto& o : level.orders()) {
        EXPECT_GT(o.qty, 0U) << "order " << o.id << " rests with zero quantity";
        sum += o.qty;
    }
    EXPECT_EQ(level.total_qty(), sum);
}

Qty allocated_to(const Fill& fill, OrderId id) {
    Qty total{0};
    for (const auto& a : fill.allocations) {
        if (a.id == id) {
            total += a.qty;
        }
    }
    return total;
}

Qty remaining_of(const PriceLevel& level, OrderId id) {
    for (const auto& o : level.orders()) {
        if (o.id == id) {
            return o.qty;
        }
    }
    return 0;
}

// ===========================================================================
// One test suite, run against every policy. Each test receives its policy
// through std::unique_ptr<MatchingPolicy>, so every call is virtual and the
// test cannot know which policy it is checking. Anything here is a promise
// EVERY policy must keep.
// ===========================================================================

using PolicyFactory = std::unique_ptr<MatchingPolicy> (*)();

std::unique_ptr<MatchingPolicy> make_fifo() { return std::make_unique<FifoPolicy>(); }
std::unique_ptr<MatchingPolicy> make_pro_rata() { return std::make_unique<ProRataPolicy>(); }

class AnyPolicy : public ::testing::TestWithParam<PolicyFactory> {
protected:
    std::unique_ptr<MatchingPolicy> policy{GetParam()()};
};

INSTANTIATE_TEST_SUITE_P(
    Policies, AnyPolicy, ::testing::Values(&make_fifo, &make_pro_rata),
    [](const ::testing::TestParamInfo<PolicyFactory>& info) {
        return info.param == &make_fifo ? std::string{"Fifo"} : std::string{"ProRata"};
    });

TEST_P(AnyPolicy, FillsExactlyTheIncomingQuantityWhenTheLevelHasEnough) {
    PriceLevel level{make_level({100, 50, 30})};
    const Fill fill{policy->allocate(60, level)};
    EXPECT_EQ(fill.filled, 60U);
    EXPECT_EQ(level.total_qty(), 120U);
    expect_invariants(level);
}

TEST_P(AnyPolicy, NeverFillsMoreThanTheLevelHolds) {
    PriceLevel level{make_level({10, 20})};
    const Fill fill{policy->allocate(1000, level)};
    EXPECT_EQ(fill.filled, 30U);
    EXPECT_TRUE(level.empty());
    EXPECT_EQ(level.total_qty(), 0U);
}

TEST_P(AnyPolicy, ZeroIncomingChangesNothing) {
    PriceLevel level{make_level({10, 20})};
    const PriceLevel before{level};
    const Fill fill{policy->allocate(0, level)};
    EXPECT_EQ(fill.filled, 0U);
    EXPECT_TRUE(fill.allocations.empty());
    EXPECT_EQ(level, before);
}

TEST_P(AnyPolicy, EmptyLevelFillsNothing) {
    PriceLevel level{10'000};
    const Fill fill{policy->allocate(50, level)};
    EXPECT_EQ(fill.filled, 0U);
    EXPECT_TRUE(fill.allocations.empty());
}

// Conservation, order by order: what an order had before is exactly what
// it was allocated plus what it still has.
TEST_P(AnyPolicy, EveryUnitIsAccountedFor) {
    PriceLevel level{make_level({7, 13, 29, 41})};
    const PriceLevel before{level};
    const Fill fill{policy->allocate(50, level)};

    Qty listed{0};
    for (const auto& a : fill.allocations) {
        EXPECT_GT(a.qty, 0U) << "zero allocations should not be listed";
        listed += a.qty;
    }
    EXPECT_EQ(listed, fill.filled);

    for (const auto& o : before.orders()) {
        EXPECT_EQ(o.qty, allocated_to(fill, o.id) + remaining_of(level, o.id))
            << "order " << o.id;
    }
}

// Whoever survives keeps their place in the queue, and allocations are
// reported in time priority.
TEST_P(AnyPolicy, TimePriorityIsPreserved) {
    PriceLevel level{make_level({5, 100, 5, 100, 5})};
    const Fill fill{policy->allocate(60, level)};

    EXPECT_TRUE(std::is_sorted(level.orders().begin(), level.orders().end(),
                               [](const auto& a, const auto& b) { return a.id < b.id; }));
    EXPECT_TRUE(std::is_sorted(fill.allocations.begin(), fill.allocations.end(),
                               [](const auto& a, const auto& b) { return a.id < b.id; }));
}

// The property test: random levels and quantities from a fixed seed, and
// the promises above must hold every time. A preview of Milestone 2.
TEST_P(AnyPolicy, PromisesHoldOnTwoThousandRandomLevels) {
    std::mt19937_64 rng{42};
    std::uniform_int_distribution<int> order_count{0, 20};
    std::uniform_int_distribution<Qty> order_qty{1, 1000};
    std::uniform_int_distribution<Qty> incoming_qty{0, 15'000};

    for (int round{0}; round < 2000; ++round) {
        SCOPED_TRACE(::testing::Message() << "round " << round);
        PriceLevel level{10'000};
        const int n{order_count(rng)};
        for (int i{0}; i < n; ++i) {
            level.add(static_cast<OrderId>(i + 1), order_qty(rng));
        }
        const PriceLevel before{level};
        const Qty incoming{incoming_qty(rng)};

        const Fill fill{policy->allocate(incoming, level)};

        ASSERT_EQ(fill.filled, std::min<std::uint64_t>(incoming, before.total_qty()));
        ASSERT_EQ(level.total_qty() + fill.filled, before.total_qty());
        for (const auto& o : before.orders()) {
            ASSERT_EQ(o.qty, allocated_to(fill, o.id) + remaining_of(level, o.id));
        }
        expect_invariants(level);
    }
}

// A level's total is a sum of 32-bit quantities, and can exceed 32 bits:
// two orders of 3 billion. Stored as Qty, the total silently wraps.
TEST(Level, TotalQuantityCanExceedTheRangeOfOneOrder) {
    PriceLevel level{make_level({3'000'000'000, 3'000'000'000})};
    EXPECT_EQ(level.total_qty(), 6'000'000'000U);
    expect_invariants(level);

    ProRataPolicy pro_rata;
    const Fill fill{pro_rata.allocate(1'000'000'000, level)};
    EXPECT_EQ(fill.allocations,
              (std::vector<Allocation>{{1, 500'000'000}, {2, 500'000'000}}));
    EXPECT_EQ(level.total_qty(), 5'000'000'000U);
}

// ===========================================================================
// Where the policies are supposed to differ.
// ===========================================================================

TEST(Fifo, FillsTheOldestOrderCompletelyFirst) {
    PriceLevel level{make_level({30, 50, 20})};
    FifoPolicy fifo;
    const Fill fill{fifo.allocate(60, level)};

    EXPECT_EQ(fill.allocations, (std::vector<Allocation>{{1, 30}, {2, 30}}));
    ASSERT_EQ(level.order_count(), 2U);
    EXPECT_EQ(level.orders()[0], (lob::RestingOrder{2, 20}));
    EXPECT_EQ(level.orders()[1], (lob::RestingOrder{3, 20}));
}

TEST(ProRata, SplitsInProportionToSize) {
    PriceLevel level{make_level({100, 300})};  // 25% and 75% of the level
    ProRataPolicy pro_rata;
    const Fill fill{pro_rata.allocate(40, level)};
    EXPECT_EQ(fill.allocations, (std::vector<Allocation>{{1, 10}, {2, 30}}));
}

TEST(ProRata, RoundingRemainderGoesToTheOldestOrders) {
    // 10 split evenly three ways: 3 + 3 + 3 = 9, one unit left over.
    PriceLevel level{make_level({50, 50, 50})};
    ProRataPolicy pro_rata;
    const Fill fill{pro_rata.allocate(10, level)};
    EXPECT_EQ(fill.allocations, (std::vector<Allocation>{{1, 4}, {2, 3}, {3, 3}}));
}

// 2e9 * 3e9 needs 63 bits. Computed in 32-bit Qty arithmetic, this wraps
// around and produces nonsense shares.
TEST(ProRata, LargeQuantitiesDoNotOverflow) {
    PriceLevel level{make_level({3'000'000'000, 1'000'000'000})};
    ProRataPolicy pro_rata;
    const Fill fill{pro_rata.allocate(2'000'000'000, level)};
    EXPECT_EQ(fill.allocations,
              (std::vector<Allocation>{{1, 1'500'000'000}, {2, 500'000'000}}));
}

// The reason to have two policies at all: same level, same order, different
// winners.
TEST(Policies, SameLevelDifferentWinners) {
    PriceLevel for_fifo{make_level({10, 90})};
    PriceLevel for_pro_rata{for_fifo};

    const std::unique_ptr<MatchingPolicy> fifo{make_fifo()};
    const std::unique_ptr<MatchingPolicy> pro_rata{make_pro_rata()};

    EXPECT_EQ(fifo->allocate(50, for_fifo).allocations,
              (std::vector<Allocation>{{1, 10}, {2, 40}}));
    EXPECT_EQ(pro_rata->allocate(50, for_pro_rata).allocations,
              (std::vector<Allocation>{{1, 5}, {2, 45}}));
}

// ===========================================================================
// The hierarchy itself.
// ===========================================================================

struct TracingPolicy final : MatchingPolicy {
    static inline int destroyed{0};
    ~TracingPolicy() override { ++destroyed; }
    Fill allocate(Qty, PriceLevel&) override { return {}; }
};

// Only true because ~MatchingPolicy is virtual. Remove the `virtual` and the
// compiler does NOT warn: the delete happens inside unique_ptr, in a system
// header where warnings are suppressed, and the sanitizers stay quiet too.
// The build still fails -- because of the static_assert at the top of this
// file, and because `override` on the destructor above has nothing left to
// override. Those two lines are the whole defence.
TEST(Hierarchy, DeletingThroughTheBaseRunsTheDerivedDestructor) {
    TracingPolicy::destroyed = 0;
    {
        const std::unique_ptr<MatchingPolicy> p{std::make_unique<TracingPolicy>()};
    }
    EXPECT_EQ(TracingPolicy::destroyed, 1);
}

// dynamic_cast asks the object what it really is, at runtime. It works --
// but needing it usually means the interface is missing a function.
TEST(Hierarchy, DynamicCastRecoversTheConcreteType) {
    const std::unique_ptr<MatchingPolicy> p{make_pro_rata()};
    EXPECT_NE(dynamic_cast<ProRataPolicy*>(p.get()), nullptr);
    EXPECT_EQ(dynamic_cast<FifoPolicy*>(p.get()), nullptr);
}

}  // namespace