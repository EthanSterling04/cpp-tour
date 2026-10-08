# Ch. 6 - Essential Operations

## Done when: the rule of five, from memory

    ~Widget();
    Widget(const Widget&);
    Widget& operator=(const Widget&);
    Widget(Widget&&) noexcept;
    Widget& operator=(Widget&&) noexcept;

Declare none: the compiler generates all five, member by member.
Declare a destructor: the copy operations are still generated (deprecated),
but the move operations are NOT -- so every "move" silently becomes a copy.
That is the trap: adding a logging destructor can quietly make a type slow.
Rule of zero: own resources through members that manage themselves, and
write none of the five.

## Done when: reallocations for 1M push_backs

21. Doubling, starting from capacity 1: 2^20 = 1,048,576 covers 1M, plus
the first allocation. libstdc++ also doubles; MSVC grows by 1.5x. 1.5x
wastes less memory and in theory lets a freed block be reused; 2x does fewer
reallocations. For a 1M-order arena it does not matter, because the arena
should never grow on the hot path at all (see the open questions below).

## Done when: the strong guarantee

1. Allocate the new buffer.            Throws -> nothing changed.
2. Construct the new element in it.    Throws -> free buffer, nothing changed.
3. Relocate the old elements into it.  Throws -> destroy what was built,
                                       free buffer. Old buffer only read.
4. Destroy old, free old, swap pointers. Cannot throw.

Everything that can fail happens before anything irreversible. That is the
whole technique, and it generalises: do the risky work on the side, then
commit with operations that cannot fail.

## Why must a move constructor be noexcept?

Because containers check. GrowthCopiesWhenTheMoveMightThrow: identical
type, move never actually throws, but without `noexcept` every relocation
copied. A move that throws halfway through relocation leaves the source
half-gutted with no way back, so the container refuses to risk it.

## std::move is a cast

std::move(x) is static_cast<T&&>(x). It moves nothing; it only makes x
eligible to bind to a move constructor. On a const object it produces
const T&&, which cannot bind to T&& but can bind to const T& -- so overload
resolution picks the copy constructor. StdMoveOfAConstVectorCopies.

## Allocation is not construction

new T[n] allocates AND constructs n objects. A vector with capacity 128
and size 100 must hold 100 objects in 128 slots of raw memory. With
new T[n], OnlyLiveElementsExistAndAllAreDestroyed sees alive == 128, and T
must be default-constructible. ::operator new gives raw bytes; placement new
constructs exactly when an element is added; ~T() destroys exactly when one
is removed.

## push_back(v[0])

When v is full, value is a reference into the buffer that growth destroys.
Construct the new element in the new buffer FIRST (step 2), while the old
buffer still exists. Verified: the reverse order is an ASan
heap-use-after-free.

## Decisions

- Copy assignment: copy-and-swap. Simple, strong, self-assignment-safe.
  Costs an allocation per assignment even when capacity would do.
  Spelled Vector copy(other): {other} picks the initializer_list
  constructor for Vector<std::any>.
- Moved-from state: empty, capacity 0, immediately reusable.
- _ticks overflow: consteval + throw, so it is a compile error.

## Tests I argued with

ThrowDuringGrowthLeavesTheVectorUnchanged only throws on the 3rd copy of a
growth, so relocate-then-construct passes it while breaking the strong
guarantee on the last copy. ThrowAtAnyPointDuringGrowth now covers every
throw point. Also, GrowthCopiesWhenTheMoveMightThrow only checks copies > 0,
not the exact count. With doubling it should be 1023 (1+2+...+512).

## Open questions for the order book

- The M1 arena is a std::vector<Order>, and growth relocates every element.
  That is why handles must be indices: every Order* dies on reallocation.
- A 1M-element relocation mid-matching is a multi-millisecond stall that
  shows up at p99.9. Reserve the arena once, up front, and treat "arena
  full" as an error (ArenaFull in M0 Q9), never as a reason to grow.
- Order is trivially copyable, so the optimizer may turn relocation into
  a block copy -- but libstdc++'s explicit memmove path needs a *trivial*
  type, and Order's {} member initializers make it non-trivial. Check the
  M4 profile rather than assume.

## Time