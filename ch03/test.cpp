#include "lob/order.h"   // header under test first, for the same reason

#include <gtest/gtest.h>

#include <vector>

namespace {

using lob::best_of;
using lob::Order;
using lob::OrderId;
using lob::OrderType;
using lob::Price;
using lob::Qty;
using lob::Side;

// The constexpr functions had to stay in the header to be usable here at
// compile time -- and after the split, they still are.
static_assert(lob::crosses(Side::Buy, 10100, 10100));

Order bid(OrderId id, Price px, Qty qty) { return {id, px, qty, Side::Buy,  OrderType::Limit}; }
Order ask(OrderId id, Price px, Qty qty) { return {id, px, qty, Side::Sell, OrderType::Limit}; }

TEST(BestOf, BidsTakeTheHighestPriceAndSumItsQuantity) {
    const std::vector<Order> bids{bid(1, 10000, 100), bid(2, 10002, 50),
                                  bid(3, 10001, 70),  bid(4, 10002, 30)};
    const auto [px, qty] = best_of(bids);
    EXPECT_EQ(px, 10002);
    EXPECT_EQ(qty, 80U);   // two orders at 10002
}

TEST(BestOf, AsksTakeTheLowestPrice) {
    const std::vector<Order> asks{ask(1, 10005, 10), ask(2, 10003, 20),
                                  ask(3, 10004, 30), ask(4, 10003, 5)};
    const auto [px, qty] = best_of(asks);
    EXPECT_EQ(px, 10003);
    EXPECT_EQ(qty, 25U);
}

// The one that catches the real bug: a better price arriving after several
// orders at a worse one must not inherit their quantity.
TEST(BestOf, ABetterPriceResetsTheTotal) {
    const std::vector<Order> bids{bid(1, 10000, 100), bid(2, 10000, 100),
                                  bid(3, 10001, 7)};
    const auto [px, qty] = best_of(bids);
    EXPECT_EQ(px, 10001);
    EXPECT_EQ(qty, 7U);    // not 207
}

TEST(BestOf, SingleOrder) {
    const auto [px, qty] = best_of({ask(1, 10050, 12)});
    EXPECT_EQ(px, 10050);
    EXPECT_EQ(qty, 12U);
}

TEST(BestOf, EmptyInputMeansNoLevel) {
    const auto [px, qty] = best_of({});
    EXPECT_EQ(qty, 0U);
    EXPECT_EQ(px, 0);
}

// Preconditions checked with assert() only exist in builds without NDEBUG,
// so the test that proves the check fires must only exist there too.
#ifndef NDEBUG
TEST(BestOfDeathTest, MixedSidesViolateThePrecondition) {
    const std::vector<Order> mixed{bid(1, 10000, 10), ask(2, 10001, 10)};
    EXPECT_DEATH((void)best_of(mixed), "same side");
}
#endif

}  // namespace