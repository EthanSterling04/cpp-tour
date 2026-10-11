#include "lob/policy.h"

#include <algorithm>
#include <cassert>
#include <cstdint>
#include <vector>

namespace lob {

Fill FifoPolicy::allocate(Qty incoming, PriceLevel& level) {
    const auto& orders = level.orders();
    std::vector<Qty> shares(orders.size(), 0);

    Qty left{incoming};
    for (std::size_t i{0}; i < orders.size() && left > 0; ++i) {
        shares[i] = std::min(left, orders[i].qty);
        left -= shares[i];
    }
    return level.execute(shares);
}

Fill ProRataPolicy::allocate(Qty incoming, PriceLevel& level) {
    const auto& orders = level.orders();
    std::vector<Qty> shares(orders.size(), 0);

    const std::uint64_t total{level.total_qty()};
    const std::uint64_t to_fill{std::min<std::uint64_t>(incoming, total)};
    if (to_fill == 0) {
        return level.execute(shares);
    }

    // Each share is floor(to_fill * qty / total). The product needs 64 bits:
    // two quantities near 2^32 multiply to nearly 2^64. The result never
    // exceeds qty, because to_fill <= total.
    std::uint64_t allocated{0};
    for (std::size_t i{0}; i < orders.size(); ++i) {
        shares[i] = static_cast<Qty>(to_fill * orders[i].qty / total);
        allocated += shares[i];
    }

    // Rounding down loses less than one unit per order, so the remainder is
    // smaller than the order count, and enough orders have room for it.
    // Rule: one unit each, in time priority.
    std::uint64_t remainder{to_fill - allocated};
    for (std::size_t i{0}; i < orders.size() && remainder > 0; ++i) {
        if (shares[i] < orders[i].qty) {
            ++shares[i];
            --remainder;
        }
    }
    assert(remainder == 0 && "ProRataPolicy: remainder left undistributed");

    return level.execute(shares);
}

}  // namespace lob