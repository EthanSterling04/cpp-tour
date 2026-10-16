// Ch. 12 -- six ways to keep the price levels of one side of a book.
//
// Everything lives in lob::ch12, not lob: Ch 5 and Ch 15 already define a
// lob::Level, and two different classes with one name in one program is a
// one-definition-rule violation.
#pragma once

#include "lob/types.h"

#include <algorithm>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <map>
#include <memory_resource>
#include <optional>
#include <unordered_map>
#include <utility>
#include <vector>

namespace lob::ch12 {

// The aggregate state of one price. The bake-off measures the index, so
// individual orders are not stored; Ch 15's intrusive list hangs off this.
struct Level {
    std::uint64_t total_qty{0};
    std::uint32_t count{0};

    bool operator==(const Level&) const = default;
};

// The prices the book supports. Only the dense array is limited by it; the
// others accept any price and use the band only as a sizing hint.
struct Band {
    Price min{};
    Price max{};

    constexpr std::size_t size() const noexcept { return static_cast<std::size_t>(max - min + 1); }
    constexpr bool contains(Price p) const noexcept { return p >= min && p <= max; }
};

// The interface every index provides: one side of a book, bids, so the best
// price is the HIGHEST. The Book in Milestone 1 can take any type that
// satisfies this as a template parameter, which is what makes the index
// swappable -- and makes this bake-off a fair one.
template <class I>
concept LevelIndex = requires(I idx, const I cidx, Price p, Qty q, Band band) {
    I{band};
    { idx.add(p, q) } -> std::same_as<bool>;     // an order of q joins level p; false = price unsupported
    { idx.remove(p, q) } -> std::same_as<void>;  // an order of q leaves level p; an emptied level is erased
    { cidx.find(p) } -> std::same_as<const Level*>;
    { cidx.best() } -> std::same_as<std::optional<Price>>;
    { cidx.levels() } -> std::same_as<std::size_t>;
};

// ===========================================================================
// 1. std::map: a red-black tree. One heap node per level; ordered for free.
// ===========================================================================

class MapIndex {
public:
    explicit MapIndex(Band) {}

    bool add(Price p, Qty q) {
        Level& lv{levels_[p]};  // creates an empty level if p is new
        lv.total_qty += q;
        ++lv.count;
        return true;
    }

    void remove(Price p, Qty q) {
        const auto it{levels_.find(p)};
        assert(it != levels_.end() && "remove: no such level");
        it->second.total_qty -= q;
        if (--it->second.count == 0) {
            levels_.erase(it);
        }
    }

    const Level* find(Price p) const {
        const auto it{levels_.find(p)};
        return it == levels_.end() ? nullptr : &it->second;
    }

    // The tree keeps a pointer to its rightmost node, so this is O(1).
    std::optional<Price> best() const noexcept {
        if (levels_.empty()) {
            return std::nullopt;
        }
        return levels_.rbegin()->first;
    }

    std::size_t levels() const noexcept { return levels_.size(); }

private:
    std::map<Price, Level> levels_;
};

// ===========================================================================
// 2. Sorted std::vector + binary search. Contiguous; O(n) insert and erase.
//
// BestAtBack decides which end the best price lives at. Bids churn near the
// top of the book, so with the best at the BACK an insert or erase near the
// top shifts a few elements; with the best at the FRONT it shifts nearly all
// of them. Same structure, same complexity, very different speed.
// ===========================================================================

template <bool BestAtBack>
class BasicSortedVecIndex {
public:
    explicit BasicSortedVecIndex(Band band) { levels_.reserve(band.size()); }

    bool add(Price p, Qty q) {
        auto it{position(p)};
        if (it == levels_.end() || it->first != p) {
            it = levels_.insert(it, Entry{p, Level{}});
        }
        it->second.total_qty += q;
        ++it->second.count;
        return true;
    }

    void remove(Price p, Qty q) {
        const auto it{position(p)};
        assert(it != levels_.end() && it->first == p && "remove: no such level");
        it->second.total_qty -= q;
        if (--it->second.count == 0) {
            levels_.erase(it);
        }
    }

    const Level* find(Price p) const {
        const auto it{position(p)};
        return it == levels_.end() || it->first != p ? nullptr : &it->second;
    }

    std::optional<Price> best() const noexcept {
        if (levels_.empty()) {
            return std::nullopt;
        }
        return BestAtBack ? levels_.back().first : levels_.front().first;
    }

    std::size_t levels() const noexcept { return levels_.size(); }

private:
    using Entry = std::pair<Price, Level>;

    // "a sorts before b" in this vector's order.
    static constexpr bool before(Price a, Price b) noexcept {
        if constexpr (BestAtBack) {
            return a < b;
        } else {
            return a > b;
        }
    }

    // First entry not before p: p itself if present, else where p would go.
    auto position(Price p) {
        return std::lower_bound(levels_.begin(), levels_.end(), p,
                                [](const Entry& e, Price x) { return before(e.first, x); });
    }
    auto position(Price p) const {
        return std::lower_bound(levels_.begin(), levels_.end(), p,
                                [](const Entry& e, Price x) { return before(e.first, x); });
    }

    std::vector<Entry> levels_;
};

using SortedVecIndex         = BasicSortedVecIndex<true>;   // best at the back
using SortedVecReversedIndex = BasicSortedVecIndex<false>;  // best at the front

// ===========================================================================
// 3. std::unordered_map: O(1) find, no order at all, so the best price is
//    tracked separately and re-found by walking down when the top empties.
// ===========================================================================

// identity function for the hash
using PriceHash = std::hash<int64_t>;

template <class Map>
class BasicHashIndex {
public:
    explicit BasicHashIndex(Band band, typename Map::allocator_type alloc = {})
        : levels_(alloc) {
        levels_.reserve(band.size());  // buckets only; each level still gets a node
    }

    bool add(Price p, Qty q) {
        Level& lv{levels_.try_emplace(p).first->second};
        lv.total_qty += q;
        ++lv.count;
        if (!best_ || p > *best_) {
            best_ = p;
        }
        return true;
    }

    void remove(Price p, Qty q) {
        const auto it{levels_.find(p)};
        assert(it != levels_.end() && "remove: no such level");
        it->second.total_qty -= q;
        if (--it->second.count == 0) {
            levels_.erase(it);
            if (p == best_) {
                best_ = next_best_below(p);
            }
        }
    }

    const Level* find(Price p) const {
        const auto it{levels_.find(p)};
        return it == levels_.end() ? nullptr : &it->second;
    }

    std::optional<Price> best() const noexcept { return best_; }
    std::size_t          levels() const noexcept { return levels_.size(); }

private:
    // Every remaining level is below p, so the walk ends -- after one hash
    // lookup per empty tick in between. The cost is the gap, not the size.
    std::optional<Price> next_best_below(Price p) const {
        if (levels_.empty()) {
            return std::nullopt;
        }
        for (Price x{p - 1};; --x) {
            if (levels_.contains(x)) {
                return x;
            }
        }
    }

    Map                  levels_;
    std::optional<Price> best_;
};

using HashIndex = BasicHashIndex<std::unordered_map<Price, Level, PriceHash>>;

// 3b. The same, with every node carved from a monotonic buffer: allocation
//     is a pointer bump and freeing is a no-op -- so freed nodes are never
//     reused, and memory grows for as long as levels come and go.
using PmrHashMap = std::pmr::unordered_map<Price, Level, PriceHash>;

// Base-from-member: base classes are constructed in the order they are
// listed, so the arena exists before the map that allocates from it, and is
// destroyed after it. As two plain members, the order would depend on
// declaration order and be one careless edit away from a dangling resource.
struct MonotonicArena {
    explicit MonotonicArena(std::size_t initial_bytes)
        : buffer_(initial_bytes), resource_{buffer_.data(), buffer_.size()} {}

    std::vector<std::byte>              buffer_;
    std::pmr::monotonic_buffer_resource resource_;
};

class PmrHashIndex : private MonotonicArena, public BasicHashIndex<PmrHashMap> {
public:
    explicit PmrHashIndex(Band band)
        : MonotonicArena{std::size_t{1} << 20}, BasicHashIndex<PmrHashMap>{band, &resource_} {}
};

// ===========================================================================
// 4. Dense array indexed by (price - band.min). O(1) everything -- except
//    re-finding the best when the top empties, and prices outside the band.
// ===========================================================================

class DenseIndex {
public:
    explicit DenseIndex(Band band) : band_{band}, levels_(band.size()) {}

    bool add(Price p, Qty q) {
        if (!band_.contains(p)) {
            return false;  // what breaks it: a price it has no slot for
        }
        Level& lv{at(p)};
        if (lv.count == 0) {
            ++live_;
        }
        lv.total_qty += q;
        ++lv.count;
        if (!best_ || p > *best_) {
            best_ = p;
        }
        return true;
    }

    void remove(Price p, Qty q) {
        assert(band_.contains(p) && at(p).count > 0 && "remove: no such level");
        Level& lv{at(p)};
        lv.total_qty -= q;
        if (--lv.count == 0) {  // "erasing" is just leaving a zeroed slot
            --live_;
            if (p == best_) {
                best_ = next_best_below(p);
            }
        }
    }

    const Level* find(Price p) const {
        if (!band_.contains(p) || at(p).count == 0) {
            return nullptr;
        }
        return &at(p);
    }

    std::optional<Price> best() const noexcept { return best_; }
    std::size_t          levels() const noexcept { return live_; }

private:
    Level&       at(Price p) { return levels_[static_cast<std::size_t>(p - band_.min)]; }
    const Level& at(Price p) const { return levels_[static_cast<std::size_t>(p - band_.min)]; }

    // A linear scan of contiguous memory: fast per tick, but O(gap).
    std::optional<Price> next_best_below(Price p) const {
        if (live_ == 0) {
            return std::nullopt;
        }
        for (Price x{p - 1};; --x) {
            if (at(x).count > 0) {
                return x;
            }
        }
    }

    Band                 band_;
    std::vector<Level>   levels_;
    std::size_t          live_{0};
    std::optional<Price> best_;
};

// ===========================================================================
// 5. Dense array indexed by (price - band.min) + a bitmap of which slots 
// are occupied. ~O(1) everything, including re-finding the best when the top 
// empties.
// ===========================================================================
class OccupancyTree {
public:
    explicit OccupancyTree(std::size_t n) {
        std::size_t bits{n};
        do {
            const std::size_t words{(bits + 63) / 64};
            levels_.emplace_back(words, 0);
            bits = words;
        } while (bits > 1);
    }

    void set(std::size_t i) noexcept {
        for (auto& level : levels_) {
            std::uint64_t& w{level[i >> 6]};
            const bool was_empty{w == 0};
            w |= std::uint64_t{1} << (i & 63);
            if (!was_empty) {
                return;  // the levels above already know this word is non-empty
            }
            i >>= 6;
        }
    }

    void clear(std::size_t i) noexcept {
        for (auto& level : levels_) {
            std::uint64_t& w{level[i >> 6]};
            w &= ~(std::uint64_t{1} << (i & 63));
            if (w != 0) {
                return;  // the word still has bits: nothing changes above
            }
            i >>= 6;
        }
    }

    // The highest set index strictly below i, if any.
    std::optional<std::size_t> prev(std::size_t i) const noexcept {
        if (i == 0) {
            return std::nullopt;
        }
        std::size_t pos{i - 1};
        std::size_t l{0};
        for (;;) {  // climb until some word has a set bit at or below pos
            const std::size_t   word{pos >> 6};
            const unsigned      bit{static_cast<unsigned>(pos & 63)};
            const std::uint64_t mask{~std::uint64_t{0} >> (63 - bit)};
            const std::uint64_t w{levels_[l][word] & mask};
            if (w != 0) {
                pos = (word << 6) + static_cast<std::size_t>(std::bit_width(w) - 1);
                break;
            }
            if (word == 0 || ++l == levels_.size()) {
                return std::nullopt;
            }
            pos = word - 1;  // the previous word, as an index one level up
        }
        while (l > 0) {  // descend, taking the highest set bit each time
            --l;
            pos = (pos << 6) + static_cast<std::size_t>(std::bit_width(levels_[l][pos]) - 1);
        }
        return pos;
    }

    std::size_t depth() const noexcept { return levels_.size(); }

private:
    std::vector<std::vector<std::uint64_t>> levels_;  // levels_[0] is one bit per price
};

class DenseBitmapIndex {
public:
    explicit DenseBitmapIndex(Band band) : band_{band}, levels_(band.size()), occupied_{band.size()} {}

    bool add(Price p, Qty q) {
        if (!band_.contains(p)) {
            return false;
        }
        Level& lv{at(p)};
        if (lv.count == 0) {
            ++live_;
            occupied_.set(slot(p));
        }
        lv.total_qty += q;
        ++lv.count;
        if (!best_ || p > *best_) {
            best_ = p;
        }
        return true;
    }

    void remove(Price p, Qty q) {
        Level& lv{at(p)};
        lv.total_qty -= q;
        if (--lv.count == 0) {
            --live_;
            occupied_.clear(slot(p));
            if (p == best_) {
                const auto below{occupied_.prev(slot(p))};
                best_ = below ? std::optional<Price>{band_.min + static_cast<Price>(*below)} : std::nullopt;
            }
        }
    }

    const Level* find(Price p) const {
        if (!band_.contains(p) || at(p).count == 0) {
            return nullptr;
        }
        return &at(p);
    }

    std::optional<Price> best() const noexcept { return best_; }
    std::size_t          levels() const noexcept { return live_; }

private:
    std::size_t  slot(Price p) const noexcept { return static_cast<std::size_t>(p - band_.min); }
    Level&       at(Price p) { return levels_[slot(p)]; }
    const Level& at(Price p) const { return levels_[slot(p)]; }

    Band                 band_;
    std::vector<Level>   levels_;
    OccupancyTree        occupied_;
    std::size_t          live_{0};
    std::optional<Price> best_;
};

}  // namespace lob::ch12