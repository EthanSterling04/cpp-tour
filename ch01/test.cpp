#include "types.h"

#include <gtest/gtest.h>

#include <array>

namespace {

using lob::kDefaultTick;
using lob::Price;
using lob::to_price;
using lob::to_ticks;

// ---------------------------------------------------------------------------
// Compile-time checks. These do not run -- the compiler evaluates them while
// building. If one fails, the build stops and no test binary is produced.
// ---------------------------------------------------------------------------

static_assert(to_ticks(0.0, kDefaultTick) == 0);
static_assert(to_ticks(100.05, kDefaultTick) == 10005);
static_assert(to_ticks(-3.33, kDefaultTick) == -333);

// Rounds to nearest, rather than truncating.
static_assert(to_ticks(100.054, kDefaultTick) == 10005);
static_assert(to_ticks(100.056, kDefaultTick) == 10006);

// Halves go away from zero, matching std::round.
static_assert(to_ticks(0.005, kDefaultTick) == 1);
static_assert(to_ticks(-0.005, kDefaultTick) == -1);

// Any tick size, not just cents: quarter-point futures.
static_assert(to_ticks(1234.25, 0.25) == 4937);

// A whole number of ticks survives the round trip.
static_assert(to_ticks(to_price(10005, kDefaultTick), kDefaultTick) == 10005);

// Braced initialization rejects narrowing conversions, so this does not build:
//
//     int x{3.5};
//
// double -> int discards the fractional part, and {} forbids any conversion
// that can lose information. `int x = 3.5;` does compile: copy-initialization
// permits the implicit conversion and silently stores 3. That silence is the
// whole reason this file exists, which is why every initialization below uses
// braces.

// ---------------------------------------------------------------------------
// Runtime tests.
// ---------------------------------------------------------------------------

// Range-for over a fixed array, plus an if-statement with an initializer.
// Returns how many of the quoted prices are not tradable.
int count_rejected(const std::array<double, 5>& quotes) {
    int rejected{0};
    for (const double px : quotes) {
        // `t` exists only inside this if. The challenge tests `t < 0`; a price
        // of zero is not tradable either, so this rejects that too.
        if (const Price t = to_ticks(px, kDefaultTick); t <= 0) {
            ++rejected;
        }
    }
    return rejected;
}

TEST(Ticks, RejectsNonPositivePrices) {
    constexpr std::array<double, 5> quotes{100.05, -0.01, 0.0, 99.99, -12.5};
    EXPECT_EQ(count_rejected(quotes), 3);
}

TEST(Ticks, RoundTripsEveryTickInARange) {
    for (Price t{-1000}; t <= 1000; ++t) {
        EXPECT_EQ(to_ticks(to_price(t, kDefaultTick), kDefaultTick), t);
    }
}

TEST(Ticks, IsMonotonic) {
    EXPECT_LT(to_ticks(100.00, kDefaultTick), to_ticks(100.01, kDefaultTick));
    EXPECT_LT(to_ticks(-0.02, kDefaultTick), to_ticks(-0.01, kDefaultTick));
}

// The reason the entire system stores integers.
TEST(Ticks, EqualPricesMatchEvenWhenTheDoublesDoNot) {
    const double computed{0.1 + 0.2};   // 0.30000000000000004
    const double literal{0.3};
    EXPECT_NE(computed, literal);
    EXPECT_EQ(to_ticks(computed, kDefaultTick),
              to_ticks(literal, kDefaultTick));   // both 30
}

}  // namespace