// Ch. 6 -- tests for Vector<T>.
//
// HOW TO USE THIS FILE
//   1. Every test below compiles from day one, so `bt` is green immediately:
//      the Stage 0 tests pass, and everything else is skipped.
//   2. Implement Stage 1 in vector.h, set kStage = 1, run `bt`. Repeat.
//   3. Read failures top-down. A test that says "TODO: reserve" is calling a
//      stub you have not written yet. A test that fails an assertion is a
//      real bug -- or a test you should argue with in NOTES.md.
//   4. Run one stage on its own while you work on it:
//        ctest --test-dir build-asan -R VecStorage --output-on-failure
//
// Do not edit the tests to make them pass. If you think one is wrong, write
// down why in NOTES.md first -- you may well be right, and that is worth
// knowing -- then change it.

#include "Vector.h"
#include "literals.h"
#include "test_types.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>

namespace {

// Raise as you finish each stage: 1 storage, 2 copy, 3 move,
// 4 strong guarantee, 5 comparison and literal.
constexpr int kStage = 5;

#define STAGE(n) \
    if (kStage < (n)) GTEST_SKIP() << "stage " #n ": raise kStage in test.cpp to run this"

using tour::Vector;
using tour::testing::Counter;
using tour::testing::MayThrowCounter;
using tour::testing::Thrower;

// ===========================================================================
// Stage 0 -- the interface. These pass against the starter file. They pin
// down the declarations, so if you change a signature in a way that matters
// (dropping a noexcept, say) the build tells you immediately.
// ===========================================================================

static_assert(std::is_nothrow_default_constructible_v<Vector<int>>);
static_assert(std::is_copy_constructible_v<Vector<int>>);
static_assert(std::is_copy_assignable_v<Vector<int>>);
static_assert(std::is_nothrow_move_constructible_v<Vector<int>>);
static_assert(std::is_nothrow_move_assignable_v<Vector<int>>);
static_assert(std::is_nothrow_destructible_v<Vector<int>>);

// Comparison is constrained: a Vector of comparable things is comparable,
// and a Vector of non-comparable things is still a perfectly good type.
static_assert(std::three_way_comparable<Vector<int>>);
static_assert(!std::three_way_comparable<Vector<Counter>>);

TEST(VecInterface, DefaultConstructedIsEmpty) {
    const Vector<int> v;
    EXPECT_EQ(v.size(), 0U);
    EXPECT_EQ(v.capacity(), 0U);
    EXPECT_TRUE(v.empty());
    EXPECT_EQ(v.begin(), v.end());
}

// ===========================================================================
// Stage 1 -- storage and lifetime: reserve, push_back, the initializer_list
// constructor, and the destructor.
// ===========================================================================

TEST(VecStorage, PushBackStoresValuesInOrder) {
    STAGE(1);
    Vector<int> v;
    for (int i{0}; i < 5; ++i) {
        v.push_back(i * 10);
    }
    ASSERT_EQ(v.size(), 5U);
    EXPECT_GE(v.capacity(), 5U);
    for (std::size_t i{0}; i < v.size(); ++i) {
        EXPECT_EQ(v[i], static_cast<int>(i) * 10);
    }
}

TEST(VecStorage, PushBackTakesLvaluesAndRvalues) {
    STAGE(1);
    Vector<int> v;
    const int lvalue{7};
    v.push_back(lvalue);  // push_back(const T&)
    v.push_back(8);       // push_back(T&&)
    ASSERT_EQ(v.size(), 2U);
    EXPECT_EQ(v[0], 7);
    EXPECT_EQ(v[1], 8);
}

TEST(VecStorage, InitializerListAndRangeFor) {
    STAGE(1);
    const Vector<int> v{3, 1, 4, 1, 5};
    ASSERT_EQ(v.size(), 5U);
    EXPECT_EQ(v[2], 4);
    int sum{0};
    for (const int x : v) {  // begin()/end() as raw pointers
        sum += x;
    }
    EXPECT_EQ(sum, 14);
}

TEST(VecStorage, ReserveGrowsCapacityButNeverSizeOrContents) {
    STAGE(1);
    Vector<int> v{1, 2, 3};
    v.reserve(100);
    EXPECT_GE(v.capacity(), 100U);
    ASSERT_EQ(v.size(), 3U);
    EXPECT_EQ(v[0], 1);
    EXPECT_EQ(v[2], 3);

    const std::size_t capacity{v.capacity()};
    v.reserve(10);  // asking for less is a no-op: reserve never shrinks
    EXPECT_EQ(v.capacity(), capacity);
}

// Allocation is not construction. If this fails with alive == capacity()
// rather than 100, you built objects in the spare capacity -- `new T[n]`
// does exactly that. Spare capacity must be raw, unconstructed memory.
TEST(VecStorage, OnlyLiveElementsExistAndAllAreDestroyed) {
    STAGE(1);
    ASSERT_EQ(Counter::alive, 0);
    {
        Vector<Counter> v;
        for (int i{0}; i < 100; ++i) {
            v.push_back(Counter{i});
        }
        EXPECT_EQ(Counter::alive, 100);
    }
    EXPECT_EQ(Counter::alive, 0);
}

// The "Done when" number. Run this test on its own to see it:
//   ./build-asan/ch06/ch06_test --gtest_filter=VecStorage.GrowsGeometrically
TEST(VecStorage, GrowsGeometrically) {
    STAGE(1);
    Vector<int> v;
    int reallocations{0};
    std::size_t last_capacity{v.capacity()};
    for (int i{0}; i < 1'000'000; ++i) {
        v.push_back(i);
        if (v.capacity() != last_capacity) {
            ++reallocations;
            last_capacity = v.capacity();
        }
    }
    ASSERT_EQ(v.size(), 1'000'000U);
    EXPECT_EQ(v[999'999], 999'999);

    std::cout << "    reallocations for 1M push_backs: " << reallocations << '\n';
    // Doubling gives about 21; growing by 1.5x gives about 35. Growing by a
    // constant gives tens of thousands, and by one gives a million.
    EXPECT_LE(reallocations, 50) << "growth is not geometric";
}

// ===========================================================================
// Stage 2 -- copying: the copy constructor and copy assignment.
// ===========================================================================

TEST(VecCopy, CopyConstructorMakesAnIndependentDeepCopy) {
    STAGE(2);
    Vector<int> a{1, 2, 3};
    Vector<int> b = a;
    ASSERT_EQ(b.size(), 3U);
    EXPECT_NE(a.begin(), b.begin());  // different storage

    b[0] = 99;
    EXPECT_EQ(a[0], 1);  // changing the copy leaves the original alone
}

TEST(VecCopy, CopyingCopiesEachElementExactlyOnce) {
    STAGE(2);
    Vector<Counter> a;
    for (int i{0}; i < 10; ++i) {
        a.push_back(Counter{i});
    }
    Counter::reset_counts();

    const Vector<Counter> b = a;
    EXPECT_EQ(Counter::copies, 10);
    EXPECT_EQ(Counter::moves, 0);
    ASSERT_EQ(b.size(), 10U);
    EXPECT_EQ(b[9].value, 9);
}

TEST(VecCopy, CopyAssignmentReplacesContents) {
    STAGE(2);
    Vector<int> a{1, 2, 3};
    Vector<int> b{7, 8};
    b = a;
    ASSERT_EQ(b.size(), 3U);
    EXPECT_EQ(b[2], 3);

    a[0] = 100;
    EXPECT_EQ(b[0], 1);
}

TEST(VecCopy, CopyAssignmentDestroysTheOldElements) {
    STAGE(2);
    ASSERT_EQ(Counter::alive, 0);
    {
        Vector<Counter> a;
        a.push_back(Counter{1});
        Vector<Counter> b;
        for (int i{0}; i < 5; ++i) {
            b.push_back(Counter{i});
        }
        b = a;
        EXPECT_EQ(Counter::alive, 2);  // a's one element, and b's copy of it
    }
    EXPECT_EQ(Counter::alive, 0);
}

TEST(VecCopy, SelfAssignmentIsHarmless) {
    STAGE(2);
    Vector<int> v{1, 2, 3};
    const Vector<int>& same = v;  // spelled this way so the compiler doesn't
    v = same;                     // warn about the obvious `v = v`
    ASSERT_EQ(v.size(), 3U);
    EXPECT_EQ(v[0], 1);
    EXPECT_EQ(v[2], 3);
}

// std::move is a cast to an rvalue reference; it moves nothing by itself.
// Cast a const object and you get `const Vector&&`, which cannot bind to the
// move constructor's `Vector&&` -- so the copy constructor runs instead.
TEST(VecCopy, StdMoveOfAConstVectorCopies) {
    STAGE(2);
    const Vector<int> a{1, 2, 3};
    const Vector<int> b = std::move(a);
    EXPECT_EQ(a.size(), 3U);  // a was copied from, not moved from
    EXPECT_EQ(b.size(), 3U);
}

TEST(VecCopy, CopyAssignmentWorksForElementsConstructibleFromAnything) {
    STAGE(2);
    Vector<std::any> a;
    a.push_back(1);
    a.push_back(2);
    a.push_back(3);
    Vector<std::any> b;
    b = a;
    EXPECT_EQ(b.size(), 3U);
}

// ===========================================================================
// Stage 3 -- moving, and what noexcept buys you during growth.
// ===========================================================================

TEST(VecMove, MoveConstructorStealsTheBuffer) {
    STAGE(3);
    Vector<int> a{1, 2, 3};
    const int* buffer{a.begin()};

    Vector<int> b = std::move(a);
    EXPECT_EQ(b.begin(), buffer);  // the same storage changed hands
    ASSERT_EQ(b.size(), 3U);
    EXPECT_EQ(b[1], 2);

    EXPECT_EQ(a.size(), 0U);  // moved-from: empty, and still usable
    EXPECT_EQ(a.capacity(), 0U);
    a.push_back(4);
    EXPECT_EQ(a.size(), 1U);
}

TEST(VecMove, MovingAVectorTouchesNoElements) {
    STAGE(3);
    Vector<Counter> a;
    for (int i{0}; i < 10; ++i) {
        a.push_back(Counter{i});
    }
    Counter::reset_counts();

    const Vector<Counter> b = std::move(a);
    EXPECT_EQ(Counter::copies, 0);
    EXPECT_EQ(Counter::moves, 0);  // three members change hands, not ten elements
    EXPECT_EQ(b.size(), 10U);
}

TEST(VecMove, MoveAssignmentReleasesTheOldContents) {
    STAGE(3);
    ASSERT_EQ(Counter::alive, 0);
    {
        Vector<Counter> a;
        a.push_back(Counter{1});
        a.push_back(Counter{2});
        Vector<Counter> b;
        b.push_back(Counter{9});

        b = std::move(a);
        EXPECT_EQ(Counter::alive, 2);  // b's old element is gone
        ASSERT_EQ(b.size(), 2U);
        EXPECT_EQ(b[1].value, 2);
    }
    EXPECT_EQ(Counter::alive, 0);
}

// The heart of the chapter. When growth relocates elements, it may move them
// only if the move cannot throw: a move that throws halfway through leaves
// the old buffer half-gutted, with no way back. std::move_if_noexcept makes
// exactly this choice. The next two tests differ in one keyword.
TEST(VecMove, GrowthMovesWhenTheMoveIsNoexcept) {
    STAGE(3);
    Vector<Counter> v;
    Counter::reset_counts();
    for (int i{0}; i < 1000; ++i) {
        v.push_back(Counter{i});
    }
    EXPECT_EQ(Counter::copies, 0);
    EXPECT_GT(Counter::moves, 1000);  // 1000 moves in, plus every relocation
}

TEST(VecMove, GrowthCopiesWhenTheMoveMightThrow) {
    STAGE(3);
    Vector<MayThrowCounter> v;
    MayThrowCounter::reset_counts();
    for (int i{0}; i < 1000; ++i) {
        v.push_back(MayThrowCounter{i});
    }
    // Relocation had to copy, even though this move never actually throws.
    // Declaring that it might is enough.
    EXPECT_GT(MayThrowCounter::copies, 0);
}

TEST(VecMove, RvaluePushBackMovesEvenWhenTheMoveMightThrow) {
    STAGE(3);
    Vector<MayThrowCounter> v;
    v.reserve(1000);  // no growth, so only the push itself is counted
    MayThrowCounter::reset_counts();
    for (int i{0}; i < 1000; ++i) {
        v.push_back(MayThrowCounter{i});
    }
    EXPECT_EQ(MayThrowCounter::moves, 1000);
    EXPECT_EQ(MayThrowCounter::copies, 0);
}

// ===========================================================================
// Stage 4 -- the strong exception guarantee, and the aliasing trap.
// "Strong" means: if an operation throws, the object is exactly as it was
// before the call. Not "valid". Not "no leaks". Unchanged.
// ===========================================================================

TEST(VecStrong, ThrowDuringGrowthLeavesTheVectorUnchanged) {
    STAGE(4);
    Thrower::copies_until_throw = -1;
    ASSERT_EQ(Thrower::alive, 0);
    {
        Vector<Thrower> v;
        v.reserve(4);
        while (v.size() < v.capacity()) {
            v.push_back(Thrower{static_cast<int>(v.size())});
        }
        // Full: the next push_back has to reallocate.
        const std::size_t size_before{v.size()};
        const std::size_t capacity_before{v.capacity()};
        const Thrower* buffer_before{v.begin()};

        Thrower::copies_until_throw = 2;  // two copies succeed, the third throws
        EXPECT_THROW(v.push_back(Thrower{99}), std::runtime_error);
        Thrower::copies_until_throw = -1;

        EXPECT_EQ(v.size(), size_before);
        EXPECT_EQ(v.capacity(), capacity_before);
        EXPECT_EQ(v.begin(), buffer_before);  // still the original storage
        for (std::size_t i{0}; i < v.size(); ++i) {
            EXPECT_EQ(v[i].value, static_cast<int>(i));
        }
        // The copies that did succeed, into the new buffer, were destroyed.
        EXPECT_EQ(Thrower::alive, static_cast<int>(size_before));
    }
    EXPECT_EQ(Thrower::alive, 0);
}

TEST(VecStrong, ThrowWithoutGrowthLeavesTheSizeAlone) {
    STAGE(4);
    Thrower::copies_until_throw = -1;
    Vector<Thrower> v;
    v.reserve(8);
    v.push_back(Thrower{1});
    v.push_back(Thrower{2});
    const Thrower extra{3};

    Thrower::copies_until_throw = 0;  // the very next copy throws
    EXPECT_THROW(v.push_back(extra), std::runtime_error);
    Thrower::copies_until_throw = -1;

    // If this says 3, size_ was incremented before the element was built.
    ASSERT_EQ(v.size(), 2U);
    EXPECT_EQ(v[1].value, 2);
}

TEST(VecStrong, CopyConstructorCleansUpWhenAnElementCopyThrows) {
    STAGE(4);
    Thrower::copies_until_throw = -1;
    ASSERT_EQ(Thrower::alive, 0);
    {
        Vector<Thrower> source;
        for (int i{0}; i < 5; ++i) {
            source.push_back(Thrower{i});
        }
        Thrower::copies_until_throw = 3;  // three copies succeed, the fourth throws
        EXPECT_THROW((void)Vector<Thrower>(source), std::runtime_error);
        Thrower::copies_until_throw = -1;

        // Only source's five remain. The three that were built were destroyed;
        // LeakSanitizer separately checks the buffer itself was released.
        EXPECT_EQ(Thrower::alive, 5);
    }
    EXPECT_EQ(Thrower::alive, 0);
}

// When v is full, v.push_back(v[0]) passes a reference INTO the buffer that
// growth is about to destroy. Do things in the wrong order and you copy from
// freed memory: AddressSanitizer reports heap-use-after-free. The strings are
// long enough to live on the heap, which makes the failure unmissable.
TEST(VecStrong, PushBackOfOwnElementSurvivesReallocation) {
    STAGE(4);
    Vector<std::string> v;
    v.push_back("the first element, long enough to need a heap allocation");
    while (v.size() < v.capacity()) {
        v.push_back("filler");
    }
    const std::string expected{v[0]};

    v.push_back(v[0]);  // full: this push reallocates, and v[0] is the source

    ASSERT_GE(v.size(), 2U);
    EXPECT_EQ(v[v.size() - 1], expected);
    EXPECT_EQ(v[0], expected);
}

TEST(VecStrong, ThrowAtAnyPointDuringGrowthLeavesTheVectorUnchanged) {
    STAGE(4);
    for (int successful_copies{0}; successful_copies <= 4; ++successful_copies) {
        SCOPED_TRACE(::testing::Message() << "copies before the throw: " << successful_copies);
        Thrower::copies_until_throw = -1;
        ASSERT_EQ(Thrower::alive, 0);
        {
            Vector<Thrower> v;
            v.reserve(4);
            while (v.size() < 4) {
                v.push_back(Thrower{static_cast<int>(v.size())});
            }
            ASSERT_EQ(v.size(), v.capacity()) << "test assumes reserve(4) gives exactly 4";
            const std::size_t capacity_before{v.capacity()};
            const Thrower* buffer_before{v.begin()};

            // A growth makes 5 copies: 4 relocated elements + 1 new one.
            Thrower::copies_until_throw = successful_copies;
            EXPECT_THROW(v.push_back(Thrower{99}), std::runtime_error);
            Thrower::copies_until_throw = -1;

            EXPECT_EQ(v.size(), 4U);
            EXPECT_EQ(v.capacity(), capacity_before);
            EXPECT_EQ(v.begin(), buffer_before);
            EXPECT_EQ(Thrower::alive, 4);
        }
        EXPECT_EQ(Thrower::alive, 0);
    }
}

// ===========================================================================
// Stage 5 -- comparison, and the user-defined literal.
// ===========================================================================

TEST(VecCompare, EqualityIsElementwise) {
    STAGE(5);
    const Vector<int> a{1, 2, 3};
    EXPECT_TRUE(a == (Vector<int>{1, 2, 3}));
    EXPECT_TRUE(a != (Vector<int>{1, 2}));     // != is rewritten from ==
    EXPECT_TRUE(a != (Vector<int>{1, 2, 4}));
    EXPECT_TRUE(Vector<int>{} == Vector<int>{});
}

TEST(VecCompare, OrderingIsLexicographic) {
    STAGE(5);
    const Vector<int> a{1, 2};
    const Vector<int> b{1, 2, 3};
    const Vector<int> c{1, 3};
    EXPECT_TRUE(a < b);  // a is a prefix of b
    EXPECT_TRUE(b < c);  // the first difference decides: 2 < 3
    EXPECT_TRUE(c > b);  // > is rewritten from <=>
    EXPECT_TRUE((a <=> a) == 0);
    EXPECT_TRUE(Vector<double>{1.0} < Vector<double>{2.0});  // partial_ordering
}

TEST(TicksLiteral, MakesAPrice) {
    STAGE(5);
    using namespace lob::literals;
    EXPECT_EQ(10005_ticks, lob::Price{10005});
    EXPECT_EQ(0_ticks, lob::Price{0});
    // Once it works, prove it also works at compile time by uncommenting:
    // static_assert(10005_ticks == 10005);
}

}  // namespace