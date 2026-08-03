# Limit order book matching engine (Week 1)

A price-time priority matching engine in modern C++ — no raw pointers, no
manual memory management, just STL containers.

## What's here

- `include/order.hpp` — `Order` and `Trade` data types
- `include/order_book.hpp` / `src/order_book.cpp` — the matching engine
- `src/main.cpp` — a small demo that walks through resting orders,
  partial fills, and a market order sweeping multiple price levels
- `tests/test_order_book.cpp` — 8 unit tests covering the core behaviors

## Build and run

No CMake needed — just `make` and `g++` (C++17).

```
make test    # build and run the unit tests
make demo    # build and run the walkthrough demo
make         # both
```

## Design decisions worth knowing for an interview

- **Prices are integer ticks (`int64_t`), not `double`.** Comparing floating
  point prices for equality/ordering is a classic bug source. Real exchanges
  quote in fixed tick sizes for this exact reason — if your instrument
  trades in cents, `price=10050` means $100.50.
- **`std::map<price, std::deque<Order>>`** gives sorted price levels for
  free (map iteration order = price order), and each `deque` is a FIFO
  queue, so time priority within a price level falls out naturally from
  `push_back` / `front()` / `pop_front()`.
- **Bids and asks are different C++ types.** `bids_` sorts highest-first
  (`std::greater`), `asks_` sorts lowest-first (default `std::less`) — so
  the compiler enforces "best bid" and "best ask" both being `.begin()`.
  This bit me once while building it (see `cancelOrder`'s template helper)
  — worth understanding why, it's a good interview anecdote.
- **`cancelOrder` is O(orders at that price level), not O(1).** Flagged in
  the code as a known limitation — a production engine would use an
  intrusive linked list per order for O(1) cancel. Good "what would you
  improve" answer if asked.
- **Market orders never rest.** Unfilled remainder is dropped, matching how
  real markets treat market orders (they demand immediacy, not a resting
  position).

## C++ concepts to review if any of this reads unfamiliar

`std::map`, `std::deque`, `std::unordered_map`, `std::optional`, `std::vector`,
references vs. pointers, `enum class`, basic templates (used once, in
`cancelOrder`). That's the entire surface area of this codebase — there's no
manual `new`/`delete`, no raw pointer arithmetic, nothing that overlaps with
what a rough experience in plain C would have covered.

## Baseline benchmark (before any optimization)

Run with `make bench` for a single run, or **`make baseline` for a proper
multi-trial median** (recommended — see note below).

**Single runs are noisy on a personal machine.** A laptop has a browser,
IDE, OS background tasks, and (if running under WSL2) virtualization
overhead all competing with the benchmark for CPU time. Any one run can get
unlucky and catch a burst of unrelated activity — p50 staying roughly
stable while p99.9/max swing wildly between runs is the signature of this,
not a bug. `bench/run_baseline.sh` runs several trials and reports the
**median** of each percentile across them, which is what should actually be
recorded and compared against later ("before vs. after an optimization"),
not any single run.

500,000 events per trial, fixed RNG seed 42 for reproducibility. Recorded
baseline (median of 7 trials, `addOrder`):

| Percentile | Latency  |
|------------|----------|
| p50        | ~90-115ns|
| p90        | ~175-235ns|
| p99        | ~340-440ns|
| p99.9      | ~1000-1800ns|

Ranges reflect real run-to-run variance across two different machines
(sandbox environment and a personal laptop under WSL2) — treat this as the
expected noise floor for "before" vs "after" comparisons on similar
hardware, not a single precise number.

This baseline uses `std::map` (red-black tree, pointer-chasing, a heap
allocation per node) — the next step is replacing it with something
cache-friendly and allocation-free in the hot path, then re-running
`make baseline` on the **same machine** used for this baseline to get a
valid before/after comparison. Comparing across different machines isn't
meaningful — only "same machine, before vs after" is.

## Week 2, part 2 — cache-aware redesign: results

`include/fast_order_book.hpp` / `src/fast_order_book.cpp` replace
`std::map`+`std::deque` with a flat array indexed directly by price, plus a
pre-allocated slot pool linked by array index instead of pointers (details
in the header comments). `bench/latency_bench.cpp` now runs **both**
implementations on the identical synthetic event sequence in one process,
so the comparison is apples-to-apples.

Both implementations produce **identical final book state** on the same
input (`bidLevels=245 askLevels=216 restingOrders=8651`) — the redesign is
behaviorally equivalent, just faster. Confirmed by `make test-fast`
(9/9 passing) in addition to the original `make test`.

Results, median of 3 runs, 500K events:

| Metric              | Baseline (`std::map`) | FastOrderBook | Change        |
|---------------------|------------------------|----------------|---------------|
| addOrder p50         | ~135ns                 | ~80ns          | ~40% faster   |
| addOrder p99         | ~480ns                 | ~275ns         | ~40% faster   |
| Throughput           | ~5.0M events/s          | ~6.8M events/s | ~35% higher   |
| cancelOrder p50       | ~77ns                  | ~85ns          | ~unchanged    |
| cancelOrder p99.9    | ~820ns                 | ~480ns         | ~40% tighter  |

**Honest read of the cancel numbers:** median cancel latency barely moved.
At this book's typical size, the old linear-scan-within-a-price-level was
already cheap in practice — the redesign's real win there is the *tail*
(p99.9 tightened noticeably) and the algorithmic guarantee (true O(1)
regardless of how many orders sit at one price, vs. scan cost that grows
with it). That distinction — knowing which numbers actually moved and why —
matters more in an interview than a blanket "everything got faster" claim.

## Roadmap

- [x] **Week 1** — core matching engine + unit tests
- [x] **Week 2, part 1** — synthetic order flow generator + latency/throughput
      benchmark harness (p50/p90/p99/p99.9) with a multi-trial median script
- [x] **Week 2, part 2** — cache-aware, allocation-free `FastOrderBook`
      redesign; verified behaviorally identical to the baseline, ~40% lower
      addOrder latency, results above
- [ ] **Week 3** — finance-facing layer: backtested market-making/stat-arb
      strategy with P&L output, and/or a CUDA Monte Carlo pricing module
