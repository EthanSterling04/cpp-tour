#pragma once

#include "lob/level_index.h"

#include <algorithm>
#include <cstdint>
#include <map>
#include <random>
#include <span>
#include <vector>

namespace lob::ch12 {

enum class OpKind : std::uint8_t { Add, Cancel, Execute };

struct Op {
    OpKind kind{};
    Price  px{};   // Add, Cancel: the order's price. Execute: the best price then, for checking.
    Qty    qty{};  // the order's quantity
};

enum class Shape : std::uint8_t {
    Clustered,  // a few ticks below a drifting mid: a realistic book
    Uniform,    // anywhere in the band: wide, and sparse at the top, where executes
                // keep eating the best levels away
};

struct Workload {
    Band            band;
    std::vector<Op> ops;
};

// 70% add, 25% cancel of a random live order, 5% execute of an order at the
// best price, on one side (bids). Deterministic for a given seed on a given
// standard library. A reference book inside the generator guarantees every
// cancel and execute names an order that really exists at that moment.
inline Workload make_workload(Shape shape, std::size_t n_ops, std::uint64_t seed) {
    const Band band{100'000, 109'999};  // 10,000 prices
    Workload   w{band, {}};
    w.ops.reserve(n_ops);

    struct Order {
        Price         px;
        Qty           qty;
        std::uint32_t pos_live;   // where it sits in `live`
        std::uint32_t pos_level;  // where it sits in its level's vector
    };
    std::vector<Order>                          orders;  // every order ever added
    std::vector<std::uint32_t>                  live;    // ids of live orders
    std::map<Price, std::vector<std::uint32_t>> book;    // price -> live order ids

    std::mt19937_64                         rng{seed};
    std::uniform_int_distribution<int>      kind{0, 99};
    std::uniform_int_distribution<Qty>      qty{1, 100};
    std::uniform_int_distribution<Price>    anywhere{band.min, band.max};
    std::geometric_distribution<Price>      offset{0.05};  // mean 19 ticks below mid
    std::uniform_int_distribution<int>      step{-1, 1};
    Price                                   mid{(band.min + band.max) / 2};

    // Swap-remove an order from both `live` and its level, in O(1).
    auto remove = [&](std::uint32_t id) {
        const Order o{orders[id]};
        const std::uint32_t moved_live{live.back()};
        live[o.pos_live]           = moved_live;
        orders[moved_live].pos_live = o.pos_live;
        live.pop_back();

        const auto level{book.find(o.px)};
        auto&      ids{level->second};
        const std::uint32_t moved_level{ids.back()};
        ids[o.pos_level]             = moved_level;
        orders[moved_level].pos_level = o.pos_level;
        ids.pop_back();
        if (ids.empty()) {
            book.erase(level);
        }
    };

    while (w.ops.size() < n_ops) {
        const int k{kind(rng)};
        if (k < 70 || live.empty()) {
            Price px{};
            if (shape == Shape::Uniform) {
                px = anywhere(rng);
            } else {
                mid = std::clamp(mid + step(rng), band.min + 1'000, band.max - 1'000);
                px  = std::max(band.min, mid - offset(rng));
            }
            const Qty  q{qty(rng)};
            const auto id{static_cast<std::uint32_t>(orders.size())};
            auto&      ids{book[px]};
            orders.push_back({px, q, static_cast<std::uint32_t>(live.size()),
                              static_cast<std::uint32_t>(ids.size())});
            live.push_back(id);
            ids.push_back(id);
            w.ops.push_back({OpKind::Add, px, q});
        } else if (k < 95) {
            std::uniform_int_distribution<std::size_t> pick{0, live.size() - 1};
            const std::uint32_t id{live[pick(rng)]};
            w.ops.push_back({OpKind::Cancel, orders[id].px, orders[id].qty});
            remove(id);
        } else {
            const auto& [best_px, ids]{*book.rbegin()};
            const std::uint32_t id{ids.back()};
            w.ops.push_back({OpKind::Execute, best_px, orders[id].qty});
            remove(id);  // may erase the level best_px and ids refer to: not used after
        }
    }
    return w;
}

// Runs a workload. An execute asks the index for its own best price and takes
// the order's quantity from it, so best() is on the measured path.
template <LevelIndex I>
std::uint64_t replay(I& idx, std::span<const Op> ops) {
    std::uint64_t checksum{0};
    for (const Op& op : ops) {
        switch (op.kind) {
            case OpKind::Add:
                (void)idx.add(op.px, op.qty);
                break;
            case OpKind::Cancel:
                idx.remove(op.px, op.qty);
                break;
            case OpKind::Execute: {
                const Price top{*idx.best()};
                idx.remove(top, op.qty);
                checksum += static_cast<std::uint64_t>(top);
                break;
            }
        }
    }
    return checksum + idx.levels();
}

}  // namespace lob::ch12