// Ch. 6 -- instrumented element types for testing Vector<T>.
//
// These are finished; they are the measuring instruments, not the exercise.
// Read them anyway: each one is itself a small rule-of-five example, and
// knowing exactly what they count is how you read the test failures.
#pragma once

#include <stdexcept>

namespace tour::testing {

// Counts every copy and move, and how many instances are alive right now.
// `alive` catches two bugs that ASan alone may not: elements never destroyed
// (alive stays > 0) and elements destroyed twice (alive goes negative).
//
// NoexceptMove picks whether the move constructor is noexcept. That single
// keyword is what std::move_if_noexcept inspects -- Stage 3 compares both.
template <bool NoexceptMove>
struct BasicCounter {
    static inline int copies{0};
    static inline int moves{0};
    static inline int alive{0};

    static void reset_counts() noexcept { copies = 0; moves = 0; }

    int value{0};

    BasicCounter() noexcept { ++alive; }
    explicit BasicCounter(int v) noexcept : value{v} { ++alive; }

    BasicCounter(const BasicCounter& other) noexcept : value{other.value} {
        ++copies;
        ++alive;
    }
    BasicCounter(BasicCounter&& other) noexcept(NoexceptMove) : value{other.value} {
        ++moves;
        ++alive;
    }
    BasicCounter& operator=(const BasicCounter& other) noexcept {
        value = other.value;
        ++copies;
        return *this;
    }
    BasicCounter& operator=(BasicCounter&& other) noexcept(NoexceptMove) {
        value = other.value;
        ++moves;
        return *this;
    }
    ~BasicCounter() { --alive; }
};

using Counter          = BasicCounter<true>;   // move is noexcept
using MayThrowCounter  = BasicCounter<false>;  // move is NOT noexcept (it never
                                               // actually throws -- it only says
                                               // it might, and that is enough)

// A copy constructor that throws on demand.
//
// Set copies_until_throw = N: the next N copies succeed, the one after throws.
// -1 means never throw. Thrower declares a copy constructor and no move
// constructor, so it has no move constructor at all: every "move" is really
// a copy, and every copy can throw. That makes it the worst case for
// reallocation, and the right type for testing the strong guarantee.
struct Thrower {
    static inline int copies_until_throw{-1};
    static inline int alive{0};

    int value{0};

    Thrower() noexcept { ++alive; }
    explicit Thrower(int v) noexcept : value{v} { ++alive; }

    Thrower(const Thrower& other) : value{other.value} {
        if (copies_until_throw == 0) {
            throw std::runtime_error("Thrower: copy failed on purpose");
        }
        if (copies_until_throw > 0) {
            --copies_until_throw;
        }
        ++alive;
    }
    Thrower& operator=(const Thrower& other) = default;
    ~Thrower() { --alive; }
};

}  // namespace tour::testing