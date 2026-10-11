#include "lob/level.h"

#include <cassert>
#include <vector>

namespace lob {

void PriceLevel::add(OrderId id, Qty qty) {
    assert(qty > 0 && "PriceLevel::add: an order rests with positive quantity");
    orders_.push_back(RestingOrder{id, qty});
    total_qty_ += qty;
}

Fill PriceLevel::execute(std::span<const Qty> shares) {
    assert(shares.size() == orders_.size() && "PriceLevel::execute: one share per order");

    Fill fill;
    for (std::size_t i{0}; i < orders_.size(); ++i) {
        const Qty share{shares[i]};
        if (share == 0) {
            continue;
        }
        assert(share <= orders_[i].qty && "PriceLevel::execute: share exceeds the order");
        orders_[i].qty -= share;
        total_qty_ -= share;
        fill.filled += share;
        fill.allocations.push_back(Allocation{orders_[i].id, share});
    }
    // Remove fully filled orders in one pass, after the loop, so indices
    // stay valid while it runs. erase_if preserves the survivors' order.
    std::erase_if(orders_, [](const RestingOrder& o) { return o.qty == 0; });
    return fill;
}

}  // namespace lob