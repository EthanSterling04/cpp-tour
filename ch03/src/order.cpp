#include "lob/order.h"

#include <cassert>

namespace lob {
namespace {
    
// Internal to this translation unit. Nothing outside order.cpp can see it,
// so it cannot collide with a `better` defined anywhere else.
constexpr bool better(Side side, Price candidate, Price incumbent) noexcept {
    return side == Side::Buy ? candidate > incumbent : candidate < incumbent;
}

}  // namespace

std::pair<Price, Qty> best_of(const std::vector<Order>& orders) {
    if (orders.empty()) {
        return {0, 0};
    }

    const Side side{orders.front().side};
    Price best{orders.front().px};
    Qty total{0};

    for (const Order& o : orders) {
        assert(o.side == side && "best_of: every order must be on the same side");
        if (better(side, o.px, best)) {
                best = o.px;
                total = o.qty;
        }
        else if (o.px == best) {
            total += o.qty;
        }
    }
    return {best, total};
}

}  // namespace lob