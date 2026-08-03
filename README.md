# Limit order book matching engine (In Progress)

A price-time priority matching engine in modern C++.

## What's here

- `include/order.hpp` — `Order` and `Trade` data types
- `include/order_book.hpp` / `src/order_book.cpp` — the matching engine
- `src/main.cpp` — a small demo that walks through resting orders, partial fills, and a market order sweeping multiple price levels
- `tests/test_order_book.cpp` — 8 unit tests covering the core behaviors

## Design decisions

- **Prices are integer ticks (`int64_t`), not `double`.** Comparing floating point prices for equality/ordering is a classic bug source. Real exchanges quote in fixed tick sizes for this reason — if an instrument trades in cents, `price=10050` means $100.50.
- **`std::map<price, std::deque<Order>>`** gives sorted price levels for free (map iteration order = price order), and each deque is a FIFO queue, so time priority within a price level falls out of `push_back` / `front()` / `pop_front()`.
- **Bids and asks are different C++ types.** `bids_` sorts highest-first (`std::greater`), `asks_` lowest-first (default `std::less`) — so the compiler enforces that best bid and best ask are both `.begin()`.
- **`cancelOrder` is O(orders at that price level), not O(1).** Flagged in the code as a known limitation; a production engine would use an intrusive linked list per order for O(1) cancel.
- **Market orders never rest.** Unfilled remainder is dropped, matching how real markets treat market orders — they demand immediacy, not a resting position.

## Build and run

No CMake needed — just `make` and `g++` (C++17).

```
make test    # build and run the unit tests
make demo    # build and run the walkthrough demo
make         # both
```

## Baseline benchmark (before any optimization)

Run with `make bench` for a single run, or **`make baseline` for a proper multi-trial median** (recommended — see note below).

Single runs are noisy on a personal machine. A laptop has a browser, IDE, OS background tasks, and (under WSL2) virtualization overhead competing for CPU time. Any single run can catch a burst of unrelated activity — p50 staying stable while p99.9/max swing between runs is the signature of this, not a bug. `bench/run_baseline.sh` runs several trials and reports the median of each percentile, which is what should be compared against later, not any single run.

500,000 events per trial, fixed RNG seed 42 for reproducibility. Recorded baseline (median of 7 trials, `addOrder`):

| Percentile | Latency |
|------------|---------|
| p50 | ~90–115 ns |
| p90 | ~175–235 ns |
| p99 | ~340–440 ns |
| p99.9 | ~1000–1800 ns |

Ranges reflect real run-to-run variance across two different machines (sandbox environment and a personal laptop under WSL2) — treat this as the expected noise floor for "before" vs "after" comparisons on similar hardware, not a single precise number.

This baseline uses `std::map` (red-black tree, pointer-chasing, a heap allocation per node) — the next step is replacing it with something cache-friendly and allocation-free in the hot path, then re-running `make baseline` on the **same machine** used for this baseline to get a valid before/after comparison. Comparing across different machines isn't meaningful — only "same machine, before vs after" is.

## Week 2, part 2 — cache-aware redesign (initial result, unfixed)

Replaced `std::map`+`std::deque` with a flat array (`FastOrderBook`) for
O(1) price lookups and O(1) cancel.

| Metric | Baseline | FastOrderBook | Change |
|---|---|---|---|
| addOrder p50 | 91ns | 47ns | faster |
| addOrder p99.9 | 1255ns | 925ns | faster |
| cancelOrder p50 | 43ns | 101ns | **worse** |
| cancelOrder p99.9 | 1967ns | 8131ns | **worse** |
| Throughput | 7.58M/s | 10.97M/s | higher |

addOrder improved but cancelOrder got measurably worse — next step is
figuring out why before calling this done.

## Roadmap

- [x] Week 1 — core engine + unit tests
- [x] Week 2, part 1 — benchmark harness
- [ ] Week 2, part 2 — cache-aware redesign built, cancelOrder regression
      found, debugging in progress
- [ ] Week 3 — finance-facing layer