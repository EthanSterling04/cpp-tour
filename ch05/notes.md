# Ch. 5 - Classes

## Done when: both policies pass identical tests

Seven shared tests run twice through TEST_P, with the policy as a
std::unique_ptr<MatchingPolicy>. They test promises every policy keeps:
exact fill of min(incoming, total), no over-allocation, conservation per
order, time priority, level invariants -- including 2000 random levels from
a fixed seed. The numbers that differ (who gets what) are in separate,
policy-specific tests. Shared tests cannot assert specific allocations,
because the policies are SUPPOSED to disagree.

## Done when: a number for the virtual-call overhead

Claude sandbox (x86-64, 2.1 GHz), per call:
- direct, inlined and vectorised:  0.08 ns
- virtual, one type (predicted):   1.4 ns   (~17x)
- virtual, mixed types:            7.0 ns   (~85x, mispredicted)
- allocate(), direct vs virtual:   ~260 ns both, inside the noise (CV ~8%)

The 17x is not the jump. It is the inlining (and then vectorisation) that a
virtual call prevents. Next to real work, dispatch is unmeasurable.

Ran on: ARM64 dev container under Docker Desktop, pinned with taskset -c 6,
-O3, medians of 5 repetitions, two runs agreeing to within ~1%.

direct, inlined + vectorised:  0.15 ns/call
virtual, one type (predicted): 0.93 ns/call  (~6x)
virtual, mixed (~50% mispred): 4.3  ns/call  (~29x; ~7 ns per mispredict)
allocate(), direct vs virtual: 151 ns both -- difference flips sign between runs

Ratios are smaller than on x86 (17x / 85x): NEON is 128-bit, so the
inlined baseline gains less from vectorisation.

## Done when: what a vtable is, and why no inlining

Each class with virtual functions has one table of function pointers, one
slot per virtual function. Each object carries a hidden pointer to its
class's table (the vptr). A virtual call loads the vptr, loads the slot,
and jumps to whatever address it finds.

The compiler cannot inline that, because it does not know at compile time
which function the slot points to; the answer depends on the object. The
exceptions are when it can prove the dynamic type (the class is final and
the static type is known, or it can see the object being created), or when
it guesses the likely target and inlines it behind a check (speculative
devirtualisation). That is why the benchmark had to hide the type.

## When to avoid the hierarchy

The set of policies here is closed and known at compile time. A virtual
hierarchy is for open sets -- plugins, things added without recompiling.
For a closed set, std::variant<Fifo, ProRata> with std::visit, or a
template parameter Book<Policy>, gives the same flexibility without the
heap allocation per policy or the lost inlining. Same question as M0 Q7.

## Decisions

- PriceLevel owns its orders; the three numbers are its observable state.
- Only PriceLevel::execute mutates. Policies are pure: orders -> shares.
- total_qty is uint64; pro-rata's product is uint64.
- Pro-rata remainder: one unit each, oldest first.
- Copy is protected on the base, to prevent slicing (C.67).

## Open questions for the order book

- allocate() makes 2-3 heap allocations per call (shares, the Fill's
  vector, plus the level copy in the benchmark). On the hot path, that is
  the real cost, not the virtual call. M1: reuse buffers, or report fills
  through the listener instead of returning a vector.
- Erasing filled orders from a std::vector is O(n) per fill. Ch 15's
  intrusive list makes removal O(1).
- One policy per book means one call site, always the same type: the
  predicted case. Dispatch will not show up in the M4 profile.