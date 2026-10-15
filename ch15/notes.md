# Ch. 15 - Pointers and Containers

## Done when: zero heap allocations after the reserve

ZeroAlloc.AMillionOrdersAfterTheInitialReserve: 1M adds, 500k cancels,
500k re-adds into freed slots, one 500-maker match -> 0 allocations.
Everything the book needs is allocated in the constructor: the arena
(reserved, 32 B x 1M), the id -> slot vector, both level arrays.

The counter had to replace the WHOLE operator new/delete family (plain,
array, nothrow), and be [[gnu::noinline]] to stop a false
-Wmismatched-new-delete. std::stable_sort in GoogleTest uses nothrow new.

## Done when: copying a shared_ptr vs moving a unique_ptr

unique_ptr move: copy one pointer, null the other. Two plain stores.
  1.39 ns here. sizeof == one pointer.
shared_ptr copy: increment the use count in the control block, and
  decrement it when the copy dies. Atomic read-modify-writes on a cache
  line every copy shares. 11.6 ns here; 23.3 ns with two threads copying the
  same pointer. sizeof == two pointers (object + control block).

Trap: libstdc++ skips the atomics until the process has ever had a
second thread (glibc's __libc_single_threaded). A single-threaded
benchmark measured 2 ns and would have said shared_ptr was free.

## Decisions

- Indices (uint32, kNull = max) not pointers: survive reallocation,
  half the size. Order is 32 bytes, two per cache line.
- Free list threaded through `next` of dead slots; qty == 0 marks free.
- Full arena rejects. Never grow mid-match.
- Id lookup: dense vector, not unordered_map (1 alloc per insert,
  measured). Costs 4 B per possible id.
- SymbolId is 8 bits, so the 256-flag bitset covers every value.

## Raw pointers mean non-owning

Gateway holds Book* : it uses the book, never deletes it. A raw pointer
in a modern codebase should mean exactly that. Ownership is unique_ptr
(one owner) or shared_ptr (shared, and paid for).

## Open questions for the order book

- This IS the M1 storage layer. Lift it, then add the event listener and
  replace optional<Fill> with Status + events (M0 Q7, Q9).
- Dense id vector needs bounded ids. Real feeds don't promise that:
  a flat hash map with open addressing, or ids assigned by the book.
- refresh_best is O(band) when a level empties. Ch 12 decides.
- check_invariants above is M1's invariant checker almost unchanged.