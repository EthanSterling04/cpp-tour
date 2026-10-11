#pragma once

#include "lob/types.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace lob {

struct RestingOrder {
    OrderId id{};
    Qty     qty{};  // remaining; always > 0 while the order rests

    bool operator==(const RestingOrder&) const = default;
};

struct Allocation {
    OrderId id{};
    Qty     qty{};

    bool operator==(const Allocation&) const = default;
};

struct Fill {
    Qty                     filled{0};
    std::vector<Allocation> allocations;

    bool operator==(const Fill&) const = default;
};

class PriceLevel {
public:
    explicit PriceLevel(Price px) noexcept : px_{px} {}

    Price price() const noexcept { return px_;  }
    std::uint64_t total_qty() const noexcept { return total_qty_; }
    std::size_t order_count() const noexcept { return orders_.size(); }
    bool empty() const noexcept { return orders_.empty(); }

    // Read-only, in time priority. Policies read this to decide shares.
    const std::vector<RestingOrder>& orders() const noexcept { return orders_; }

    // Joins the back of the queue. Precondition: qty > 0.
    void add(OrderId id, Qty qty);

    // Applies a policy's decision: shares[i] is taken from orders()[i].
    // Fully filled orders leave the level; the rest keep their places.
    // Preconditions: shares.size() == order_count(), shares[i] <= orders()[i].qty
    Fill execute(std::span<const Qty> shares);

    bool operator==(const PriceLevel&) const = default;

private:
    Price px_;
    std::uint64_t total_qty_{0};
    std::vector<RestingOrder> orders_;
};

}  // namespace lob