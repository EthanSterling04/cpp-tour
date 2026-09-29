#pragma once

#include "lob/types.h"

#include <cstdint>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace lob {

enum class Side      : std::uint8_t { Buy, Sell };
enum class OrderType : std::uint8_t { Limit, Market, IOC };

struct Order { 
    OrderId id{}; 
    Price px{}; 
    Qty qty{}; 
    Side side{}; 
    OrderType type{};
};

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

// Would a taker priced at taker_px trade against a resting order at
// resting_px? Equal prices DO cross: an order at the touch is executable.
constexpr bool crosses(Side taker, Price taker_px, Price resting_px) noexcept {
    return taker == Side::Buy ? taker_px >= resting_px
                              : taker_px <= resting_px;
}

// --- inbound messages -----------------------------------------------------

struct NewOrder { Order order{}; };
struct Cancel { OrderId id{}; };
struct Replace { OrderId id{}; Price new_px{}; Qty new_qty{}; };

using Message = std::variant<NewOrder, Cancel, Replace>;

// Best price among `orders`, and the total quantity resting at that price.
// Bids: highest price wins. Asks: lowest. Precondition: all on one side.
// Empty input returns {0, 0}; a zero quantity means "no level".
[[nodiscard]] std::pair<Price, Qty> best_of(const std::vector<Order>& orders);

}  // namespace lob