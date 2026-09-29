# Ch. 3 - Modularity

## Done when: two translation units link

order.cpp defines best_of. test.cpp calls it. They are compiled separately and
the linker joins them through the lob_core static library.

The constexpr functions (opposite, to_string, crosses) stay in the header.
constexpr implies inline, and inline is the exception to the one-definition
rule: every TU may carry an identical definition. They MUST be in the header,
because compile-time evaluation needs the body visible in the calling TU.
best_of is an ordinary function, so the header has only its declaration and
order.cpp has the one definition.

Rule of thumb: declarations are shared, definitions are not, unless inline.

## Done when: the argument-passing rule

- By value: small, cheap-to-copy types. Price, Side, Qty, string_view, raw
  pointers. Copying is as cheap as passing an address and avoids aliasing.
- By const&: large or expensive-to-copy types you only read.
  std::vector<Order>, std::string.
- By &&: sinks - the function takes ownership and keeps the object, so the
  caller std::move()s it in. (For constructors, by value + std::move inside is
  the simpler equivalent.)
- Return by value. Copy elision builds the result straight into the caller's
  object. Return several values as a pair or struct and unpack with
  structured bindings instead of using out-parameters.

Order itself is 24 bytes and trivially copyable - by value or const& are both
fine. Worth knowing: on both x86-64 and ARM64, structs up to 16 bytes are
passed in registers; Order is past that line.

## Why no modules

CMake's C++20 module support needs GCC 14+ to scan module dependencies. The
container has GCC 13. Not worth a toolchain change for this project.

## Open questions for the order book

- best_of is O(n) over every order. The real book must never scan like this -
  the level index returns the best price directly, and the level caches its
  total quantity. This function is a correctness oracle, not an algorithm.
  Good candidate for the property test in M2: compare the book's best level
  against best_of over its resting orders.
- Summing uint32 quantities can overflow uint32. A level's total_qty may need
  to be uint64 even though a single order's qty is uint32. -> M0 Q5.
- {0, 0} as "no level" is a sentinel. std::optional would be better.

## Time

1 hour 30 minutes, 20 of them on the two linker errors.