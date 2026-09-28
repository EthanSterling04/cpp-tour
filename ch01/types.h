#pragma once

#include <cstdint>

namespace lob {

// Prices are integers. A "tick" is the smallest increment the venue allows,
// and every price inside the system is a whole number of them. Doubles appear
// only at the boundary, where a human or a text feed hands us a decimal.
using Price   = std::int64_t;   // in ticks
using Qty     = std::uint32_t;  // shares or lots
using OrderId = std::uint64_t;

// constexpr, not const: this has to be usable inside a static_assert.
inline constexpr double kDefaultTick{0.01};

// Decimal price -> ticks. Rounds to nearest; halves go away from zero.
// Precondition: tick > 0.
constexpr Price to_ticks(double price, double tick) noexcept {
    const double scaled{price / tick};
    const double nudge{scaled < 0.0 ? -0.5 : 0.5};
    return static_cast<Price>(scaled + nudge);  // the cast truncates toward zero
}

// The inverse.
constexpr double to_price(Price ticks, double tick) noexcept {
    return static_cast<double>(ticks) * tick;
}

}  // namespace lob