# Ch. 1 - The Basics

## Done when: why `int x{3.5}` fails but `int x = 3.5` compiles

Braced initialization forbids narrowing conversions - any conversion that can
lose information. double -> int loses the fractional part, so `int x{3.5}` is
ill-formed and the compiler rejects it. `int x = 3.5` is copy-initialization,
which allows implicit conversions, so it compiles and silently stores 3.

The practical rule: use {} everywhere, and the compiler catches the class of
bug where a value quietly loses precision on its way into a variable.

## What I chose, and why

- Rounding, not truncation. 100.05 / 0.01 evaluates to about 10004.999999999998
  in double, so a plain cast gives 10004 - one cent wrong, silently.
- Hand-rolled rounding instead of std::llround, because llround is not
  constexpr in C++20 and the static_asserts would not compile.
- Halves away from zero, matching std::round. The sign check is needed because
  static_cast truncates toward zero, so a flat +0.5 rounds negatives wrongly.
- Price = int64_t. 32 bits would hold the tick count, but notional is
  price * qty, which overflows 32 bits at ~2000 shares of a $100 stock.
- Qty = uint32_t, to keep Order small for cache reasons in M1.

## Open question this raises for the order book

Unsigned Qty can underflow: `remaining - fill` wraps to ~4 billion if fill is
larger. Plan: compute every fill as min(taker_remaining, maker_remaining) and
subtract that from both sides, so the subtraction cannot underflow by
construction. Assert it anyway in check_invariants. -> M0 question 1.

## Time

50 minutes.