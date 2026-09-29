#pragma once

#include <cstdint>
#include <string_view>
#include <variant>

namespace lob {

// Prices are integers. A "tick" is the smallest increment the venue allows,
// and every price inside the system is a whole number of them. Doubles appear
// only at the boundary, where a human or a text feed hands us a decimal.
using Price   = std::int64_t;   // in ticks
using Qty     = std::uint32_t;  // shares or lots
using OrderId = std::uint64_t;

enum class Side      : std::uint8_t { Buy, Sell };
enum class OrderType : std::uint8_t { Limit, Market, IOC };

struct Order { 
    OrderId id{}; 
    Price px{}; 
    Qty qty{}; 
    Side side{}; 
    OrderType type{};
};

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

constexpr Side opposite(const Side& s) noexcept {
    using enum Side;
    return s == Buy ? Sell : Buy;
}

constexpr std::string_view to_string(Side s) noexcept {
    switch (s) {
        case Side::Buy:  return "Buy";
        case Side::Sell: return "Sell";
    }
    return "?";   // unreachable for a valid Side; silences -Wreturn-type
}

constexpr std::string_view to_string(OrderType t) noexcept {
    switch (t) {
        case OrderType::Limit:  return "Limit";
        case OrderType::Market: return "Market";
        case OrderType::IOC:    return "IOC";
    }
    return "?";
}

constexpr bool crosses(Side taker, Price taker_px, Price resting_px) noexcept {
    return taker == Side::Buy ? taker_px >= resting_px
                              : taker_px <= resting_px;
}

// inbound messages
struct NewOrder { Order order{}; };
struct Cancel { OrderId id{}; };
struct Replace { OrderId id{}; Price new_px{}; Qty new_qty{}; };

using Message = std::variant<NewOrder, Cancel, Replace>;


}  // namespace lob