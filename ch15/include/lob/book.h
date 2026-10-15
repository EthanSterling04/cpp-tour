#pragma once

#include "lob/types.h"

#include <bitset>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>
#include <span>
#include <variant>
#include <vector>

namespace lob {

enum class Side : std::uint8_t { Buy, Sell };

using Index = std::uint32_t;
inline constexpr Index kNull{std::numeric_limits<Index>::max()};

// One slot in the arena. A live order is linked into its level's queue by
// prev/next. A free slot has qty == 0 and uses `next` as its link in the
// free list.
struct Order {
    OrderId id{};
    Price   px{};
    Qty     qty{};         // remaining; 0 means the slot is free
    Index   prev{kNull};
    Index   next{kNull};
    Side    side{};
};

struct Level {
    Index         head{kNull};  // oldest order: filled first
    Index         tail{kNull};  // newest order: new arrivals join here
    std::uint32_t count{0};
    std::uint64_t total_qty{0};
};

// The result of one aggressive order; nothing traded is std::nullopt.
struct Fill {
    OrderId       taker{};
    Qty           filled{};
    Price         last_px{};
    std::uint32_t makers{};  // resting orders it traded against

    bool operator==(const Fill&) const = default;
};

class Book {
public:
    struct Config {
        Price   min_px{};        // inclusive price band, in ticks
        Price   max_px{};
        Index   max_orders{};    // arena capacity: resting orders at once
        OrderId max_order_id{};  // ids must be < this
    };

    // Every allocation the book will ever make happens here.
    explicit Book(const Config& cfg);

    // Matches against the opposite side, then rests any remainder. Returns
    // what traded, or nullopt if nothing did -- including when the order is
    // rejected (bad price, zero qty, duplicate id, arena full); see
    // rejected().
    std::optional<Fill> add(OrderId id, Side side, Price px, Qty qty);

    // False if the id is not resting.
    bool cancel(OrderId id);

    std::optional<Price> best_bid() const noexcept { return best_bid_; }
    std::optional<Price> best_ask() const noexcept { return best_ask_; }
    const Level&         level(Side side, Price px) const;

    // Read-only view of every slot, live and free. Callers can walk the
    // queues through prev/next, but cannot change anything.
    std::span<const Order> arena() const noexcept { return arena_; }

    std::size_t live_orders() const noexcept { return live_; }
    std::size_t rejected() const noexcept { return rejected_; }
    Index       free_head() const noexcept { return free_head_; }

private:
    Index allocate() noexcept;
    void  release(Index i) noexcept;
    void  link_back(Level& level, Index i) noexcept;
    void  unlink(Level& level, Index i) noexcept;

    std::vector<Level>&       side_levels(Side s) noexcept { return s == Side::Buy ? bids_ : asks_; }
    const std::vector<Level>& side_levels(Side s) const noexcept { return s == Side::Buy ? bids_ : asks_; }
    Level& level_at(Side s, Price px) noexcept { return side_levels(s)[static_cast<std::size_t>(px - cfg_.min_px)]; }

    void rest(OrderId id, Side side, Price px, Qty qty, Index slot) noexcept;
    void refresh_best_after_emptying(Side s, Price emptied) noexcept;

    Config              cfg_;
    std::vector<Order>  arena_;        // reserved once; never reallocates
    Index               free_head_{kNull};
    std::vector<Index>  slot_of_id_;   // id -> arena index, or kNull
    std::vector<Level>  bids_;         // one per price in the band
    std::vector<Level>  asks_;
    std::optional<Price> best_bid_;
    std::optional<Price> best_ask_;
    std::size_t         live_{0};
    std::size_t         rejected_{0};
};

// ---- Messages ---------------------------------------------------------------

// 8 bits and 256 flags: every possible SymbolId has a flag, so
// bitset::test() -- which throws for an out-of-range position -- never can.
using SymbolId = std::uint8_t;
inline constexpr std::size_t kMaxSymbols{std::size_t{1} << (8 * sizeof(SymbolId))};

struct NewOrder { SymbolId symbol{}; OrderId id{}; Side side{}; Price px{}; Qty qty{}; };
struct Cancel   { SymbolId symbol{}; OrderId id{}; };
struct Halt     { SymbolId symbol{}; bool halted{}; };

using Message = std::variant<NewOrder, Cancel, Halt>;

// One overload per message type, inherited into one callable. C++20's
// aggregate CTAD means no deduction guide is needed.
template <class... Fs>
struct overloaded : Fs... {
    using Fs::operator()...;
};

// Routes messages to the book, refusing new orders for halted symbols.
// (One book here; a real venue would hold one per symbol.)
class Gateway {
public:
    explicit Gateway(Book& book) noexcept : book_{&book} {}

    std::optional<Fill> handle(const Message& m);

    bool is_halted(SymbolId s) const { return halted_.test(s); }

private:
    Book*                     book_;     // non-owning: the caller owns the book
    std::bitset<kMaxSymbols> halted_;    // 256 flags in 32 bytes
};

}  // namespace lob