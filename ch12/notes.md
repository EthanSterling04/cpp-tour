# Ch. 12 - Containers

## Done when: a table of numbers

1M ops, 70/25/5, 10k-price band, -O3, taskset -c 6, median of 10.
Apple Silicon Mac, Ubuntu 24.04 dev container, 8 cores.

                       clustered        uniform          top churn (ns), gap
                       M ops/s (ns/op)  M ops/s (ns/op)  1 / 64 / 4096 / 8000
  std::map              33 (29.9)        14 (70.6)       37 / 37 / 37 / 39
  sorted vec, back      33 (30.1)        11 (87.7)       14 / 13 / 14 / 15
  sorted vec, front     31 (31.9)       1.6 (633)       309 / 257 / 256 / 255
  unordered_map        296 (3.38)        97 (10.3)       21 / 64 / 2465 / 4778
  pmr monotonic        292 (3.42)       102 (9.79)       28 / 58 / 2468 / 4731
  dense array          296 (3.38)       146 (6.86)      1.6 / 21 / 1231 / 2378
  dense + radix tree   316 (3.17)       229 (4.37)       25 / 26 / 26 / 26

Best-level lookup alone: 0.34-0.49 ns for six of them, 1.5 ns for
std::map. Not a differentiator.

Noise: CV mostly under 14%. Worst: sorted vec front gap 1 (34%),
unordered_map best lookup (24%), sorted vec back gap 8000 (22%),
dense + radix tree uniform (17%). Differences under ~20% are ties.

## Done when: when the dense array wins

When prices stay inside a band known in advance. Every operation is an
array index and the whole book is 160 KB of contiguous memory. It tied the
hash map on the clustered book (3.38 vs 3.38 ns/op) and beat it 1.5x on
the uniform one (6.86 vs 10.3). With the radix tree: 2.4x the hash map on
the uniform book.

## Done when: what breaks it

1. Gaps at the top. Re-finding the best is a scan down, O(gap), about
   0.3 ns per empty tick here: 1.6 ns at gap 1, 2.4 us at gap 8000.
   The radix tree (1.3 KB of occupancy bits) makes it a flat ~25 ns at
   any gap, but costs 23 ns more per cycle at gap 1. Crossover: a gap of
   roughly 75 ticks. Uniform book: 6.86 -> 4.37 ns/op (1.6x).
2. Prices outside the band: rejected. A drifting market needs re-centring,
   which copies the array -- schedule it, never mid-match.
3. Memory proportional to the band, not the book: a 1M-tick band is 16 MB.

## Surprises

- Two tiers, about 9x apart: O(1) structures ~290-316 M ops/s clustered,
  ordered ones ~33 M. Sorted vector did not beat std::map: tied on the
  clustered book, and 24% slower on the uniform one (both CVs ~13%, so
  borderline).
- Which end of the vector holds the best: 7x on the uniform book
  (88 vs 633 ns/op), ~20x in top churn (14 vs 255-309 ns). One bool.
- The hash map's walk down costs ~0.6 ns per empty tick here (4.8 us at
  gap 8000), only 2x the dense scan. The reference sandbox measured
  ~2.8 ns per tick. The identity hash keeps the walk reading the bucket
  array in order; this machine's prefetcher and caches make that cheap.
  (Hash alternatives not re-measured here.)
- pmr monotonic tied the default allocator on both workloads, was 35%
  slower at gap-1 churn (28 vs 21 ns, CVs 6% and 1%), and 8% faster at
  gap 64. No speed reason to use it. Memory: ch12_test shows 100k
  create/erase of one level costs 4,511 KiB monotonic vs 81 KiB pool.
- Radix tree's cost is flat to the nanosecond: 24.6-25.9 ns from gap 1 to
  gap 8000, CV under 3%.
- The uniform book is not sparse overall (93% of prices live) but is sparse
  at the top: mean gap 123 ticks when the best level empties.

## Decision for M0 Q4

Dense array + radix tree, band sized generously, re-centred off the hot
path. Fastest on both workloads here (316 / 229 M ops/s), and no gap can
cost more than ~26 ns. Plain dense array only if the top of the book is
known to stay dense (gaps under ~75 ticks). Hash map (identity hash,
noexcept) + cached best if the band can't be known. Not std::map: 9x
slower clustered, 16x slower uniform than the dense + tree.