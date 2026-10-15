#include "lob/book.h"

#include <algorithm>
#include <cassert>
#include <stdexcept>

namespace lob {

Book::Book(const Config& cfg) : cfg_{cfg} {
    if (cfg.max_px < cfg.min_px || cfg.max_orders == 0 || cfg.max_orders == kNull) {
        throw std::invalid_argument("Book: bad config");
    }
    const auto band{static_cast<std::size_t>(cfg.max_px - cfg.min_px + 1)};
    arena_.reserve(cfg.max_orders);           // capacity, not size: no Orders built yet
    slot_of_id_.assign(cfg.max_order_id, kNull);
    bids_.assign(band, Level{});
    asks_.assign(band, Level{});
}

// ---- The arena and its free list -------------------------------------------

Index Book::allocate() noexcept {
    if (free_head_ != kNull) {                // reuse a released slot
        const Index i{free_head_};
        free_head_ = arena_[i].next;
        return i;
    }
    if (arena_.size() < cfg_.max_orders) {    // never-used slot
        const auto cap{arena_.capacity()};
        arena_.emplace_back();                // within capacity: no allocation
        assert(arena_.capacity() == cap && "arena reallocated");
        (void)cap;
        return static_cast<Index>(arena_.size() - 1);
    }
    return kNull;                             // full
}

void Book::release(Index i) noexcept {
    Order& o{arena_[i]};
    o.qty  = 0;
    o.prev = kNull;
    o.next = free_head_;                      // `next` now means "next free slot"
    free_head_ = i;
}

// ---- The intrusive list ----------------------------------------------------

void Book::link_back(Level& level, Index i) noexcept {
    Order& o{arena_[i]};
    o.prev = level.tail;
    o.next = kNull;
    if (level.tail != kNull) {
        arena_[level.tail].next = i;
    } else {
        level.head = i;                       // was empty
    }
    level.tail = i;
    ++level.count;
    level.total_qty += o.qty;
}

// O(1): the order knows its neighbours, so nothing is searched.
void Book::unlink(Level& level, Index i) noexcept {
    Order& o{arena_[i]};
    if (o.prev != kNull) {
        arena_[o.prev].next = o.next;
    } else {
        level.head = o.next;                  // removing the head
    }
    if (o.next != kNull) {
        arena_[o.next].prev = o.prev;
    } else {
        level.tail = o.prev;                  // removing the tail
    }
    o.prev = o.next = kNull;
    --level.count;
    level.total_qty -= o.qty;
}

// ---- Book operations -------------------------------------------------------

const Level& Book::level(Side side, Price px) const {
    if (px < cfg_.min_px || px > cfg_.max_px) {
        throw std::out_of_range("Book::level: price outside band");
    }
    return side_levels(side)[static_cast<std::size_t>(px - cfg_.min_px)];
}

void Book::rest(OrderId id, Side side, Price px, Qty qty, Index slot) noexcept {
    Order& o{arena_[slot]};
    o.id   = id;
    o.px   = px;
    o.qty  = qty;
    o.side = side;
    link_back(level_at(side, px), slot);
    slot_of_id_[id] = slot;
    ++live_;

    auto& best{side == Side::Buy ? best_bid_ : best_ask_};
    const bool better{side == Side::Buy ? px > best.value_or(px - 1) : px < best.value_or(px + 1)};
    if (better) {
        best = px;
    }
}

// The best level just emptied: walk away from the spread to the next
// non-empty one. O(band) worst case -- the price of a dense array.
void Book::refresh_best_after_emptying(Side s, Price emptied) noexcept {
    auto& best{s == Side::Buy ? best_bid_ : best_ask_};
    if (best != emptied) {
        return;
    }
    best.reset();
    if (s == Side::Buy) {
        for (Price p{emptied - 1}; p >= cfg_.min_px; --p) {
            if (level_at(s, p).count > 0) { best = p; return; }
        }
    } else {
        for (Price p{emptied + 1}; p <= cfg_.max_px; ++p) {
            if (level_at(s, p).count > 0) { best = p; return; }
        }
    }
}

std::optional<Fill> Book::add(OrderId id, Side side, Price px, Qty qty) {
    if (qty == 0 || px < cfg_.min_px || px > cfg_.max_px || id >= cfg_.max_order_id ||
        slot_of_id_[id] != kNull) {
        ++rejected_;
        return std::nullopt;
    }

    Fill fill{id, 0, 0, 0};
    const Side maker_side{side == Side::Buy ? Side::Sell : Side::Buy};
    auto& opposite_best{side == Side::Buy ? best_ask_ : best_bid_};

    while (qty > 0 && opposite_best &&
           (side == Side::Buy ? px >= *opposite_best : px <= *opposite_best)) {
        const Price level_px{*opposite_best};
        Level&      level{level_at(maker_side, level_px)};

        while (qty > 0 && level.head != kNull) {
            const Index m{level.head};
            Order&      maker{arena_[m]};
            const Qty   traded{std::min(qty, maker.qty)};  // cannot underflow either side

            qty           -= traded;
            maker.qty     -= traded;
            level.total_qty -= traded;
            fill.filled   += traded;
            fill.last_px   = level_px;
            ++fill.makers;

            if (maker.qty == 0) {
                // total_qty was already reduced by `traded` above; unlink
                // subtracts maker.qty, which is now 0.
                unlink(level, m);
                slot_of_id_[maker.id] = kNull;
                release(m);
                --live_;
            }
        }
        if (level.count == 0) {
            refresh_best_after_emptying(maker_side, level_px);
        }
    }

    if (qty > 0) {
        const Index slot{allocate()};
        if (slot == kNull) {
            ++rejected_;  // arena full: the remainder is dropped, not rested
        } else {
            rest(id, side, px, qty, slot);
        }
    }

    if (fill.filled == 0) {
        return std::nullopt;
    }
    return fill;
}

bool Book::cancel(OrderId id) {
    if (id >= cfg_.max_order_id || slot_of_id_[id] == kNull) {
        return false;
    }
    const Index i{slot_of_id_[id]};
    const Order o{arena_[i]};                 // copy: release() rewrites the slot
    Level&      level{level_at(o.side, o.px)};
    unlink(level, i);
    release(i);
    slot_of_id_[id] = kNull;
    --live_;
    if (level.count == 0) {
        refresh_best_after_emptying(o.side, o.px);
    }
    return true;
}

// ---- Dispatch --------------------------------------------------------------

std::optional<Fill> Gateway::handle(const Message& m) {
    return std::visit(
        overloaded{
            [this](const NewOrder& n) -> std::optional<Fill> {
                if (halted_.test(n.symbol)) {
                    return std::nullopt;
                }
                return book_->add(n.id, n.side, n.px, n.qty);
            },
            [this](const Cancel& c) -> std::optional<Fill> {
                (void)book_->cancel(c.id);  // cancels are allowed while halted
                return std::nullopt;
            },
            [this](const Halt& h) -> std::optional<Fill> {
                halted_.set(h.symbol, h.halted);
                return std::nullopt;
            },
        },
        m);
}

}  // namespace lob