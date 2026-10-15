# Ch. 7 - Templates

## Done when: works for int and std::string

12 tests, both types through push/pop/wrap and both branches of push_bulk.
std::string must be long enough (> 15 chars) to leave the small-string
buffer, or leaks and double destruction cost nothing ASan can see.

## Done when: what CTAD deduces

Class template argument deduction deduces a class template's arguments
from its constructor arguments, as function templates do. RingBuffer has
nothing to deduce from its default constructor, so `RingBuffer rb;` is an
error. From `RingBuffer rb{std::array{1, 2, 3, 4}};` it deduces T = int,
N = 4, because the constructor takes const std::array<T, N>&. The inner
std::array{...} is CTAD too. Where the parameters can't be read off a
constructor, a deduction guide tells the compiler what to deduce.

## Done when: when if constexpr discards a branch

Only inside a template, when the condition depends on a template
parameter: the untaken branch is never instantiated, so it can contain
code that would not compile for that T (memcpy of std::string objects).
Outside a template, the discarded branch is still fully checked; only
its code generation is skipped.

## Decisions

- Monotonic counters, masked on access. No wasted slot, size() is one
  subtraction, and Ch 18 makes them two atomics.
- Raw std::byte storage, alignas(T). launder only on live objects.
- push returns [[nodiscard]] bool; full means rejected, never overwritten.
- Not copyable or movable.

## Lambdas

- [=] / [&] capture by value / reference. A by-value capture is a member
  of the lambda object; `mutable` lets operator() change it.
- Generic lambda: auto parameters, i.e. a templated operator().
- [this] captures the pointer, not the object: it dangles if the lambda
  outlives *this. [*this] copies the object.

## Open questions for the order book

- The M3 replay tool and the Ch 18 pipeline both want this: a fixed-size
  queue between a producer and a consumer, with no allocation.
- Order is trivially copyable (Ch 2), so bulk pushes of Orders get the
  memcpy path.
- for_each with F&& is how the book should call its Listener: template
  on the callable, call it as an lvalue, never copy it.