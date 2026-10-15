#include "ring_buffer.h"

#include <gtest/gtest.h>

#include <algorithm>
#include <array>
#include <memory>
#include <string>
#include <type_traits>
#include <vector>

namespace {

using tour::RingBuffer;

// ===========================================================================
// The constraints, checked at compile time.
// ===========================================================================

// Can RingBuffer<T, N> be named at all? Unsatisfied constraints make the
// template-id ill-formed, and a requires-expression turns that into false.
template <class T, std::size_t N>
concept Instantiable = requires { typename RingBuffer<T, N>; };

struct ThrowingMove {
    ThrowingMove() = default;
    ThrowingMove(ThrowingMove&&) noexcept(false) {}
};

static_assert(Instantiable<int, 8>);
static_assert(Instantiable<std::string, 4>);
static_assert(Instantiable<std::unique_ptr<int>, 2>);  // move-only is fine
static_assert(!Instantiable<int, 6>);                  // not a power of two
static_assert(!Instantiable<int, 0>);                  // zero isn't either
static_assert(!Instantiable<ThrowingMove, 8>);         // move might throw

static_assert(tour::is_power_of_two<64> && !tour::is_power_of_two<96>);
static_assert(std::is_same_v<tour::Ring64<int>, RingBuffer<int, 64>>);
static_assert(RingBuffer<int, 16>::capacity() == 16);  // static constexpr

// CTAD: both template arguments deduced from the constructor argument.
// std::array{1, 2, 3, 4} is itself CTAD: std::array<int, 4>.
static_assert(std::is_same_v<decltype(RingBuffer{std::array{1, 2, 3, 4}}), RingBuffer<int, 4>>);

// ===========================================================================
// Behaviour, for int and for std::string.
// ===========================================================================

TEST(Ring, FifoOrderForInts) {
    RingBuffer<int, 4> rb;
    EXPECT_TRUE(rb.empty());
    for (int i{1}; i <= 3; ++i) {
        ASSERT_TRUE(rb.push(i));
    }
    EXPECT_EQ(rb.size(), 3U);
    EXPECT_EQ(rb.front(), 1);
    EXPECT_EQ(rb.pop(), 1);
    EXPECT_EQ(rb.pop(), 2);
    EXPECT_EQ(rb.pop(), 3);
    EXPECT_EQ(rb.pop(), std::nullopt);
}

TEST(Ring, FifoOrderForStrings) {
    // Long enough to live on the heap, so leaks and double frees show up.
    const std::string prefix(40, 'x');
    RingBuffer<std::string, 4> rb;
    ASSERT_TRUE(rb.push(prefix + "a"));
    ASSERT_TRUE(rb.emplace(3, 'b'));  // constructs std::string(3, 'b') in place
    EXPECT_EQ(rb.pop(), prefix + "a");
    EXPECT_EQ(rb.pop(), "bbb");
}

TEST(Ring, FullBufferRejectsAndLeavesTheValueAlone) {
    RingBuffer<std::string, 2> rb;
    ASSERT_TRUE(rb.push("one"));
    ASSERT_TRUE(rb.push("two"));
    EXPECT_TRUE(rb.full());

    std::string extra{"three"};
    EXPECT_FALSE(rb.push(std::move(extra)));
    EXPECT_EQ(extra, "three");  // rejected before anything was moved
    EXPECT_EQ(rb.size(), 2U);
}

// Many times around the ring: the counters keep growing, the mask keeps
// them inside the storage, and FIFO order survives every wrap.
TEST(Ring, SurvivesWrappingAroundManyTimes) {
    RingBuffer<int, 8> rb;
    int next_in{0};
    int next_out{0};
    for (int round{0}; round < 1000; ++round) {
        while (rb.push(next_in)) {
            ++next_in;
        }
        for (int k{0}; k < 5; ++k) {  // drain only part, so the window drifts
            ASSERT_EQ(rb.pop(), next_out++);
        }
    }
    EXPECT_EQ(rb.size(), 3U);
}

struct Live {
    static inline int alive{0};
    int v{0};
    explicit Live(int x) noexcept : v{x} { ++alive; }
    Live(const Live& o) noexcept : v{o.v} { ++alive; }
    Live(Live&& o) noexcept : v{o.v} { ++alive; }
    ~Live() { --alive; }
};

// Storage is raw: nothing exists until it is pushed, and everything still
// in the buffer is destroyed with it.
TEST(Ring, OnlyLiveElementsExistAndAllAreDestroyed) {
    Live::alive = 0;
    {
        RingBuffer<Live, 8> rb;
        EXPECT_EQ(Live::alive, 0);  // a std::array<Live, 8> could not even compile
        for (int i{0}; i < 5; ++i) {
            ASSERT_TRUE(rb.emplace(i));
        }
        (void)rb.pop();
        EXPECT_EQ(Live::alive, 4);
    }
    EXPECT_EQ(Live::alive, 0);
}

TEST(Ring, CtadFromAnArray) {
    RingBuffer rb{std::array{10, 20, 30, 40}};
    EXPECT_TRUE(rb.full());
    EXPECT_EQ(rb.pop(), 10);
}

// ===========================================================================
// for_each and the stateful lambda.
// ===========================================================================

TEST(RingForEach, VisitsOldestFirst) {
    RingBuffer<int, 4> rb;
    for (int i{0}; i < 4; ++i) {
        ASSERT_TRUE(rb.push(i));
    }
    (void)rb.pop();
    ASSERT_TRUE(rb.push(4));  // wraps: storage now holds 4,1,2,3
    std::vector<int> seen;
    rb.for_each([&seen](int x) { seen.push_back(x); });
    EXPECT_EQ(seen, (std::vector<int>{1, 2, 3, 4}));
}

// The challenge's mutable lambda. Its count lives INSIDE the lambda object,
// so what it reads afterwards shows whether for_each used that object or a
// copy of it.
TEST(RingForEach, StatefulLambdaKeepsItsStateBecauseNothingCopiedIt) {
    RingBuffer<std::string, 8> rb;
    for (const char* s : {"a", "b", "c", "d", "e"}) {
        ASSERT_TRUE(rb.push(s));
    }
    auto counter = [n = 0](auto&&...) mutable { return ++n; };

    rb.for_each(counter);     // F deduces as decltype(counter)&: a reference
    EXPECT_EQ(counter(), 6);  // 5 calls inside for_each, then this one

    // std::for_each takes its function by VALUE, then returns the copy. The
    // original is never touched.
    std::vector<int> v{1, 2, 3};
    auto other = [n = 0](auto&&...) mutable { return ++n; };
    std::for_each(v.begin(), v.end(), other);
    EXPECT_EQ(other(), 1);
}

TEST(RingForEach, WorksOnAConstBuffer) {
    RingBuffer<int, 4> rb;
    ASSERT_TRUE(rb.push(7));
    const auto& view = rb;
    int sum{0};
    view.for_each([&sum](const int& x) { sum += x; });
    EXPECT_EQ(sum, 7);
}

// ===========================================================================
// push_bulk: the memcpy path and the element-by-element path must agree.
// ===========================================================================

template <class T>
std::vector<T> drain(auto& rb) {
    std::vector<T> out;
    while (auto x = rb.pop()) {
        out.push_back(std::move(*x));
    }
    return out;
}

TEST(RingBulk, MemcpyPathHandlesTheWrap) {
    RingBuffer<int, 8> rb;
    for (int i{0}; i < 6; ++i) {
        ASSERT_TRUE(rb.push(i));
    }
    for (int i{0}; i < 6; ++i) {
        (void)rb.pop();  // empty, but head and tail now sit at slot 6
    }
    const std::array<int, 5> src{10, 11, 12, 13, 14};
    EXPECT_EQ(rb.push_bulk(src), 5U);  // slots 6,7 then wraps to 0,1,2
    EXPECT_EQ(drain<int>(rb), (std::vector<int>{10, 11, 12, 13, 14}));
}

TEST(RingBulk, ElementPathHandlesTheWrap) {
    RingBuffer<std::string, 8> rb;
    for (int i{0}; i < 6; ++i) {
        ASSERT_TRUE(rb.push(std::string(30, 'z')));
    }
    for (int i{0}; i < 6; ++i) {
        (void)rb.pop();
    }
    const std::array<std::string, 5> src{"p", "q", "r", "s", "t"};
    EXPECT_EQ(rb.push_bulk(src), 5U);
    EXPECT_EQ(drain<std::string>(rb), (std::vector<std::string>{"p", "q", "r", "s", "t"}));
}

TEST(RingBulk, CopiesOnlyWhatFits) {
    RingBuffer<int, 4> rb;
    ASSERT_TRUE(rb.push(0));
    const std::array<int, 6> src{1, 2, 3, 4, 5, 6};
    EXPECT_EQ(rb.push_bulk(src), 3U);
    EXPECT_EQ(drain<int>(rb), (std::vector<int>{0, 1, 2, 3}));
}

}  // namespace