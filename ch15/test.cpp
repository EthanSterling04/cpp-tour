#include "lob/book.h"

#include <gtest/gtest.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <new>
#include <type_traits>
#include <unordered_map>
#include <vector>

// ===========================================================================
// Count every heap allocation in the program. Replacing the global
// operator new/delete is allowed by the standard, and every `new`, every
// std::vector growth, every std::function and node-based container in the
// process now goes through here.
// ===========================================================================

namespace {
std::atomic<std::size_t> g_allocations{0};
}  // namespace

// Replace the whole family, not just the one you care about: the array
// forms and the nothrow forms are separate functions. Replace only some, and
// memory can come from the library's version and go back through yours --
// AddressSanitizer reports that as alloc-dealloc-mismatch.
//
// noinline: if GCC inlines these, it sees free() called on memory from
// operator new and reports a false -Wmismatched-new-delete, which -Werror
// turns into a failed build.
namespace {
void* counted_alloc(std::size_t n) noexcept {
    g_allocations.fetch_add(1, std::memory_order_relaxed);
    return std::malloc(n == 0 ? 1 : n);
}
}  // namespace

[[gnu::noinline]] void* operator new(std::size_t n) {
    if (void* p = counted_alloc(n)) {
        return p;
    }
    throw std::bad_alloc{};
}
[[gnu::noinline]] void* operator new[](std::size_t n) { return ::operator new(n); }
[[gnu::noinline]] void* operator new(std::size_t n, const std::nothrow_t&) noexcept { return counted_alloc(n); }
[[gnu::noinline]] void* operator new[](std::size_t n, const std::nothrow_t&) noexcept { return counted_alloc(n); }

[[gnu::noinline]] void operator delete(void* p) noexcept { std::free(p); }
[[gnu::noinline]] void operator delete[](void* p) noexcept { std::free(p); }
[[gnu::noinline]] void operator delete(void* p, std::size_t) noexcept { std::free(p); }
[[gnu::noinline]] void operator delete[](void* p, std::size_t) noexcept { std::free(p); }
[[gnu::noinline]] void operator delete(void* p, const std::nothrow_t&) noexcept { std::free(p); }
[[gnu::noinline]] void operator delete[](void* p, const std::nothrow_t&) noexcept { std::free(p); }

namespace {

using lob::Book;
using lob::Fill;
using lob::Index;
using lob::kNull;
using lob::Level;
using lob::Order;
using lob::Price;
using lob::Side;

// ===========================================================================
// Layout
// ===========================================================================

static_assert(sizeof(Order) == 32, "8 + 8 + 4 + 4 + 4 + 1, padded to 32: two per cache line");
static_assert(std::is_trivially_copyable_v<Order>);
static_assert(std::is_same_v<decltype(std::declval<const Book&>().arena()), std::span<const Order>>);

// ===========================================================================
// Helpers
// ===========================================================================

Book::Config small_config() { return {9'900, 10'100, 64, 1'000}; }

// The queue at one level, read from the outside through the arena.
std::vector<lob::OrderId> queue(const Book& book, Side side, Price px) {
    std::vector<lob::OrderId> ids;
    const auto arena{book.arena()};
    Index prev{kNull};
    for (Index i{book.level(side, px).head}; i != kNull; i = arena[i].next) {
        EXPECT_EQ(arena[i].prev, prev) << "broken back link at slot " << i;
        ids.push_back(arena[i].id);
        prev = i;
    }
    EXPECT_EQ(book.level(side, px).tail, prev) << "tail is not the last node";
    return ids;
}

// Every promise the book makes, checked by walking the real structure.
void expect_invariants(const Book& book, Price lo, Price hi) {
    const auto arena{book.arena()};
    std::size_t linked{0};
    for (const Side s : {Side::Buy, Side::Sell}) {
        for (Price p{lo}; p <= hi; ++p) {
            const Level& lv{book.level(s, p)};
            std::uint64_t sum{0};
            std::uint32_t n{0};
            for (Index i{lv.head}; i != kNull; i = arena[i].next) {
                EXPECT_EQ(arena[i].side, s);
                EXPECT_EQ(arena[i].px, p);
                EXPECT_GT(arena[i].qty, 0U);
                sum += arena[i].qty;
                ++n;
            }
            EXPECT_EQ(lv.count, n) << "count wrong at " << p;
            EXPECT_EQ(lv.total_qty, sum) << "total wrong at " << p;
            linked += n;
        }
    }
    EXPECT_EQ(linked, book.live_orders());

    std::size_t free_slots{0};
    for (Index i{book.free_head()}; i != kNull; i = arena[i].next) {
        EXPECT_EQ(arena[i].qty, 0U) << "live order on the free list";
        ++free_slots;
    }
    EXPECT_EQ(linked + free_slots, arena.size()) << "a slot is neither live nor free";

    if (book.best_bid() && book.best_ask()) {
        EXPECT_LT(*book.best_bid(), *book.best_ask()) << "crossed book";
    }
}

// ===========================================================================
// The intrusive list: every way to remove a node.
// ===========================================================================

TEST(Intrusive, NewOrdersJoinTheBack) {
    Book book{small_config()};
    for (lob::OrderId id{1}; id <= 3; ++id) {
        EXPECT_EQ(book.add(id, Side::Buy, 10'000, 10), std::nullopt);
    }
    EXPECT_EQ(queue(book, Side::Buy, 10'000), (std::vector<lob::OrderId>{1, 2, 3}));
    expect_invariants(book, 9'900, 10'100);
}

TEST(Intrusive, RemoveHeadMiddleTailAndOnly) {
    Book book{small_config()};
    for (lob::OrderId id{1}; id <= 5; ++id) {
        (void)book.add(id, Side::Sell, 10'050, 10);
    }
    ASSERT_TRUE(book.cancel(3));  // middle
    EXPECT_EQ(queue(book, Side::Sell, 10'050), (std::vector<lob::OrderId>{1, 2, 4, 5}));
    ASSERT_TRUE(book.cancel(1));  // head
    EXPECT_EQ(queue(book, Side::Sell, 10'050), (std::vector<lob::OrderId>{2, 4, 5}));
    ASSERT_TRUE(book.cancel(5));  // tail
    EXPECT_EQ(queue(book, Side::Sell, 10'050), (std::vector<lob::OrderId>{2, 4}));
    ASSERT_TRUE(book.cancel(2));
    ASSERT_TRUE(book.cancel(4));  // the only one
    EXPECT_EQ(book.level(Side::Sell, 10'050).head, kNull);
    EXPECT_EQ(book.level(Side::Sell, 10'050).tail, kNull);
    EXPECT_EQ(book.best_ask(), std::nullopt);
    expect_invariants(book, 9'900, 10'100);
}

TEST(Intrusive, CancelOfAnUnknownOrderIsRefused) {
    Book book{small_config()};
    EXPECT_FALSE(book.cancel(42));
    (void)book.add(42, Side::Buy, 10'000, 5);
    EXPECT_TRUE(book.cancel(42));
    EXPECT_FALSE(book.cancel(42));  // twice
}

// ===========================================================================
// The arena and its free list.
// ===========================================================================

TEST(Arena, FreedSlotsAreReusedBeforeNewOnes) {
    Book book{small_config()};
    (void)book.add(1, Side::Buy, 10'000, 10);
    (void)book.add(2, Side::Buy, 10'000, 10);
    const Index slot_of_1{book.level(Side::Buy, 10'000).head};
    ASSERT_TRUE(book.cancel(1));
    (void)book.add(3, Side::Buy, 9'990, 10);
    EXPECT_EQ(book.level(Side::Buy, 9'990).head, slot_of_1);
    EXPECT_EQ(book.arena().size(), 2U);  // no third slot was needed
    expect_invariants(book, 9'900, 10'100);
}

TEST(Arena, FullArenaRejectsInsteadOfGrowing) {
    Book book{{9'900, 10'100, 4, 100}};
    for (lob::OrderId id{1}; id <= 4; ++id) {
        (void)book.add(id, Side::Buy, 10'000, 1);
    }
    (void)book.add(5, Side::Buy, 10'000, 1);
    EXPECT_EQ(book.rejected(), 1U);
    EXPECT_EQ(book.live_orders(), 4U);
    EXPECT_EQ(book.arena().size(), 4U);
}

TEST(Arena, BadInputIsRejected) {
    Book book{small_config()};
    (void)book.add(1, Side::Buy, 10'000, 0);       // zero quantity
    (void)book.add(2, Side::Buy, 20'000, 5);       // outside the band
    (void)book.add(5'000, Side::Buy, 10'000, 5);   // id out of range
    (void)book.add(3, Side::Buy, 10'000, 5);
    (void)book.add(3, Side::Buy, 10'001, 5);       // duplicate id
    EXPECT_EQ(book.rejected(), 4U);
    EXPECT_EQ(book.live_orders(), 1U);
}

// ===========================================================================
// Matching returns std::optional<Fill>.
// ===========================================================================

TEST(Match, NoCrossMeansNullopt) {
    Book book{small_config()};
    EXPECT_EQ(book.add(1, Side::Sell, 10'010, 10), std::nullopt);
    EXPECT_EQ(book.add(2, Side::Buy, 10'000, 10), std::nullopt);
    EXPECT_EQ(book.best_bid(), 10'000);
    EXPECT_EQ(book.best_ask(), 10'010);
}

TEST(Match, FifoWithinALevelThenWalksToTheNext) {
    Book book{small_config()};
    (void)book.add(1, Side::Sell, 10'010, 10);
    (void)book.add(2, Side::Sell, 10'010, 10);
    (void)book.add(3, Side::Sell, 10'020, 10);

    const auto fill{book.add(4, Side::Buy, 10'020, 25)};
    ASSERT_TRUE(fill.has_value());
    EXPECT_EQ(*fill, (Fill{4, 25, 10'020, 3}));
    EXPECT_EQ(queue(book, Side::Sell, 10'020), (std::vector<lob::OrderId>{3}));
    EXPECT_EQ(book.arena()[book.level(Side::Sell, 10'020).head].qty, 5U);
    EXPECT_EQ(book.best_ask(), 10'020);
    expect_invariants(book, 9'900, 10'100);
}

TEST(Match, RemainderRestsAtTheLimitPrice) {
    Book book{small_config()};
    (void)book.add(1, Side::Sell, 10'010, 10);
    const auto fill{book.add(2, Side::Buy, 10'015, 30)};
    ASSERT_TRUE(fill.has_value());
    EXPECT_EQ(fill->filled, 10U);
    EXPECT_EQ(book.best_ask(), std::nullopt);
    EXPECT_EQ(book.best_bid(), 10'015);
    EXPECT_EQ(book.level(Side::Buy, 10'015).total_qty, 20U);
    expect_invariants(book, 9'900, 10'100);
}

// A filled order is gone: its id must stop pointing at its old slot, or a
// later cancel unlinks whatever order (or free slot) lives there now.
TEST(Match, FilledOrdersCanNoLongerBeCancelled) {
    Book book{small_config()};
    (void)book.add(1, Side::Sell, 10'010, 10);
    ASSERT_TRUE(book.add(2, Side::Buy, 10'010, 10).has_value());  // fills order 1
    EXPECT_FALSE(book.cancel(1));
    (void)book.add(3, Side::Sell, 10'020, 10);  // reuses order 1's slot
    EXPECT_FALSE(book.cancel(1));
    EXPECT_EQ(queue(book, Side::Sell, 10'020), (std::vector<lob::OrderId>{3}));
    expect_invariants(book, 9'900, 10'100);
}

// ===========================================================================
// Dispatch: std::visit with an overloaded set, halts in a bitset.
// ===========================================================================

TEST(Gateway, HaltBlocksNewOrdersForThatSymbolOnly) {
    Book book{small_config()};
    lob::Gateway gw{book};

    (void)gw.handle(lob::Halt{7, true});
    EXPECT_TRUE(gw.is_halted(7));
    EXPECT_FALSE(gw.is_halted(8));

    (void)gw.handle(lob::NewOrder{7, 1, Side::Buy, 10'000, 10});
    EXPECT_EQ(book.live_orders(), 0U);
    (void)gw.handle(lob::NewOrder{8, 2, Side::Buy, 10'000, 10});
    EXPECT_EQ(book.live_orders(), 1U);

    (void)gw.handle(lob::Halt{7, false});
    (void)gw.handle(lob::NewOrder{7, 3, Side::Buy, 10'000, 10});
    EXPECT_EQ(book.live_orders(), 2U);
}

TEST(Gateway, MessagesRouteThroughVisit) {
    Book book{small_config()};
    lob::Gateway gw{book};
    const std::vector<lob::Message> feed{
        lob::NewOrder{1, 1, Side::Sell, 10'010, 10},
        lob::NewOrder{1, 2, Side::Buy, 10'010, 4},
        lob::Cancel{1, 1},
    };
    EXPECT_EQ(gw.handle(feed[0]), std::nullopt);
    EXPECT_EQ(gw.handle(feed[1]), (Fill{2, 4, 10'010, 1}));
    EXPECT_EQ(gw.handle(feed[2]), std::nullopt);
    EXPECT_EQ(book.live_orders(), 0U);
}

// ===========================================================================
// The "Done when": zero heap allocations after construction.
// ===========================================================================

TEST(ZeroAlloc, AMillionOrdersAfterTheInitialReserve) {
    constexpr Index kOrders{1'000'000};
    Book book{{9'000, 11'000, kOrders, 2 * kOrders}};
    lob::Gateway gw{book};

    // No EXPECT inside the measured region: a failing assertion allocates
    // its message, which would be counted.
    const std::size_t before{g_allocations.load()};
    for (lob::OrderId id{0}; id < kOrders; ++id) {  // bids 9000-9999, asks 10001-11000:
        const bool buy{id % 2 == 0};                 // never cross
        const Price px{buy ? 9'000 + static_cast<Price>(id % 1000)
                           : 10'001 + static_cast<Price>(id % 1000)};
        (void)gw.handle(lob::NewOrder{1, id, buy ? Side::Buy : Side::Sell, px, 100});
    }
    for (lob::OrderId id{0}; id < kOrders; id += 2) {  // cancel every bid...
        (void)gw.handle(lob::Cancel{1, id});
    }
    for (lob::OrderId id{kOrders}; id < kOrders + kOrders / 2; ++id) {  // ...and reuse the slots
        (void)gw.handle(lob::NewOrder{1, id, Side::Buy, 10'000, 100});
    }
    (void)gw.handle(lob::NewOrder{1, 2 * kOrders - 1, Side::Sell, 10'000, 50'000});  // and a big match
    const std::size_t allocations{g_allocations.load() - before};

    EXPECT_EQ(allocations, 0U);
    EXPECT_EQ(book.rejected(), 0U);
    EXPECT_EQ(book.arena().size(), kOrders);
    EXPECT_EQ(book.live_orders(), kOrders - 500U);  // the match filled 500 bids
}

// Why the id lookup is a vector, not an unordered_map: reserve() pre-sizes
// the bucket array, but every insert still allocates its own node.
TEST(ZeroAlloc, UnorderedMapAllocatesPerInsertEvenAfterReserve) {
    std::unordered_map<lob::OrderId, Index> index;
    index.reserve(100'000);
    const std::size_t before{g_allocations.load()};
    for (lob::OrderId id{0}; id < 100'000; ++id) {
        index.emplace(id, static_cast<Index>(id));
    }
    const std::size_t n{g_allocations.load() - before};
    std::printf("    unordered_map: %zu allocations for 100000 inserts after reserve\n", n);
    EXPECT_GE(n, 100'000U);
}

// ===========================================================================
// The other "Done when": shared_ptr copies versus unique_ptr moves.
// ===========================================================================

TEST(SmartPointers, CopyingASharedPtrSharesOneControlBlock) {
    auto a{std::make_shared<Order>()};
    {
        const auto b{a};  // increments the shared count (atomically, once
                          // the process has ever had a second thread)
        EXPECT_EQ(a.use_count(), 2);
        EXPECT_EQ(a.get(), b.get());
    }                     // and decrements it
    EXPECT_EQ(a.use_count(), 1);
}

TEST(SmartPointers, MovingAUniquePtrIsAPointerCopyAndANull) {
    auto a{std::make_unique<Order>()};
    const Order* raw{a.get()};
    const auto b{std::move(a)};
    EXPECT_EQ(b.get(), raw);
    EXPECT_EQ(a, nullptr);  // NOLINT: reading a moved-from unique_ptr is defined
    static_assert(sizeof(std::unique_ptr<Order>) == sizeof(Order*));
    static_assert(sizeof(std::shared_ptr<Order>) == 2 * sizeof(Order*));
}

}  // namespace