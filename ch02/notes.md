# Ch. 2 - User-Defined Types

## Done when: what a plain enum lets you do by accident

A plain `enum` puts its enumerators in the enclosing scope and lets them
convert implicitly to int. So `Buy` is a bare name that can collide, and
`Side::Buy == OrderType::Limit` compiles and is true, because both decay to 0.
So does `if (side < 1)` and `int n = Buy;`. Nothing warns.

`enum class` makes the type distinct: no implicit conversion to int, no
comparison with a different enum type, enumerators scoped to the type name.
Converting is still possible with static_cast, which is the point - it becomes
visible.

## Done when: what variant gives over union

A union stores one member at a time but does not record which. It cannot run
the right destructor, so it refuses to hold anything non-trivial unless you
write the lifetime management yourself, and reading the wrong member is
undefined behaviour that nothing detects.

std::variant carries a discriminant. It destroys the active alternative,
throws bad_variant_access if you ask for the wrong one, and std::visit fails
to COMPILE if you add an alternative and forget to handle it. That last one
is the real win: the compiler enforces exhaustiveness.

Cost: one extra word for the index, and visit is an indirect call unless the
compiler can see through it. Measure in ch. 16 before using it on the hot path.

## Deviations from the challenge

- Reordered Order's fields (id, px, qty, side, type) instead of the spec's
  order. 24 bytes instead of 32, because declaration order is layout order and
  the spec interleaves 1-byte enums between 8-byte integers. Matters in M1,
  where the arena holds ~1M of these.
- Gave both enums an explicit uint8_t underlying type, for the same reason and
  to pin down the binary layout for M3.

## Open questions for the order book

- Market orders have no price. Right now `Order::px` is meaningless for them,
  which is a trap. Options: a sentinel price, or a separate type. -> M0 Q1.
- `crosses` takes a side and two prices, not two Orders. Keeping it free of the
  Order type means the matching loop can call it with a level's price without
  materialising anything. Keep it that way.

## Time

1 hour 10 minutes.