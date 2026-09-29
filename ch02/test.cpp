#include "order.h"

#include <gtest/gtest.h>

#include <array>
#include <string>
#include <type_traits>
#include <variant>
#include <vector>

namespace {

// Fine in a .cpp inside an anonymous namespace. Never in a header --
// that is chapter 3's rule, and the reason for it.
using namespace lob;

// ---------------------------------------------------------------------------
// Compile-time checks
// ---------------------------------------------------------------------------

static_assert(opposite(Side::Buy) == Side::Sell);
static_assert(opposite(opposite(Side::Buy)) == Side::Buy);
static_assert(to_string(Side::Sell) == "Sell");
static_assert(to_string(OrderType::IOC) == "IOC");

// A buy takes any ask at or below its limit.
static_assert(crosses(Side::Buy, 10100, 10099));
static_assert(crosses(Side::Buy, 10100, 10100));   // equal prices DO cross
static_assert(!crosses(Side::Buy, 10100, 10101));

// A sell takes any bid at or above its limit.
static_assert(crosses(Side::Sell, 10100, 10101));
static_assert(crosses(Side::Sell, 10100, 10100));
static_assert(!crosses(Side::Sell, 10100, 10099));

// The chapter's point, encoded so it cannot rot: a scoped enum is its own
// type. It does not decay to int, so it cannot be compared with an unrelated
// enum or with a number by accident.
static_assert(!std::is_convertible_v<Side, int>);
static_assert(!std::is_convertible_v<int, Side>);
static_assert(std::is_same_v<std::underlying_type_t<Side>, std::uint8_t>);

// Layout. Declaration order is layout order; reordering saved 8 bytes.
static_assert(sizeof(Order) == 24);
static_assert(std::is_trivially_copyable_v<Order>);   // needed in M3's binary file

// None of these compile, and that is the whole value of `enum class`:
//
//     int n = Side::Buy;                    // no implicit conversion to int
//     if (Side::Buy == OrderType::Limit)    // no common type to compare in
//     if (Side::Buy < 1)                    // no conversion, no comparison
//
// With a plain `enum Side { Buy, Sell };` all three compile, the second is
// `true` because both enumerators decay to 0, and nothing warns.

// ---------------------------------------------------------------------------
// Runtime tests
// ---------------------------------------------------------------------------

TEST(Crosses, TakesOnlyTheLevelsInsideItsLimit) {
    const std::array<Price, 3> asks{10101, 10102, 10103};
    int taken{0};
    for (const Price ask : asks) {
        if (crosses(Side::Buy, 10102, ask)) {
            ++taken;
        }
    }
    EXPECT_EQ(taken, 2);   // 10101 and 10102, not 10103
}

TEST(Message, KnowsWhichAlternativeIsAlive) {
    const Message m{Cancel{42}};
    EXPECT_EQ(m.index(), 1U);
    EXPECT_TRUE(std::holds_alternative<Cancel>(m));
    EXPECT_THROW((void)std::get<NewOrder>(m), std::bad_variant_access);
}

// A union cannot do this: it does not know which member is alive, so it
// cannot run the right destructor, and the compiler makes that your problem.
struct Tracer {
    static inline int destructions{0};
    ~Tracer() { ++destructions; }
};

TEST(Message, VariantDestroysTheActiveAlternative) {
    Tracer::destructions = 0;
    {
        const std::variant<int, Tracer> v{std::in_place_type<Tracer>};
        EXPECT_EQ(Tracer::destructions, 0);
    }
    EXPECT_EQ(Tracer::destructions, 1);
}

// Dispatch, idiom one: a visitor type with an overload per alternative.
struct Describe {
    std::string operator()(const NewOrder& n) const {
        return "new " + std::string{to_string(n.order.side)} + " " +
               std::to_string(n.order.qty) + " @ " + std::to_string(n.order.px);
    }
    std::string operator()(const Cancel& c) const {
        return "cancel " + std::to_string(c.id);
    }
    std::string operator()(const Replace& r) const {
        return "replace " + std::to_string(r.id);
    }
};

TEST(Message, VisitDispatchesOnTheActiveAlternative) {
    const std::vector<Message> feed{
        NewOrder{Order{1, 10005, 100, Side::Buy, OrderType::Limit}},
        Cancel{1},
        Replace{2, 10006, 50},
    };

    std::vector<std::string> out;
    for (const Message& m : feed) {
        out.push_back(std::visit(Describe{}, m));
    }

    ASSERT_EQ(out.size(), 3U);
    EXPECT_EQ(out[0], "new Buy 100 @ 10005");
    EXPECT_EQ(out[1], "cancel 1");
    EXPECT_EQ(out[2], "replace 2");
}

// Dispatch, idiom two: inline lambdas. In C++20 the deduction guide this used
// to need is gone -- aggregate CTAD handles it.
template <class... Ts>
struct overloaded : Ts... {
    using Ts::operator()...;
};

TEST(Message, VisitWithInlineLambdas) {
    const Message m{Replace{7, 10100, 25}};

    const Qty q = std::visit(overloaded{
        [](const NewOrder& n) { return n.order.qty; },
        [](const Cancel&)     { return Qty{0}; },
        [](const Replace& r)  { return r.new_qty; },
    }, m);

    EXPECT_EQ(q, 25U);
}

}  // namespace