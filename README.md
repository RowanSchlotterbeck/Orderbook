# Orderbook

A limit order book matching engine written in modern C++23. Implements price-time priority matching with O(log n) price-level lookups and O(1) order cancellation.

## Features

- Limit order add and cancel
- Price-time priority (FIFO within each price level)
- Multi-level sweep matching for aggressive orders
- Partial fills on both incoming and resting sides
- 18 unit tests covering matching semantics and cancellation edge cases
- Google Benchmark suite measuring per-operation latency and throughput (inserts, matches, cancels, multi-level sweeps, mixed workload)
- Zero-dependency build via CMake with FetchContent


## How it works

### Order matching (price-time priority)

Incoming orders are matched immediately against the resting book before any remainder rests:

- A **buy** order matches against resting **asks** as long as its price is `>=` the best (lowest) ask price.
- A **sell** order matches against resting **bids** as long as its price is `<=` the best (highest) bid price.
- Within a price level, orders match in **FIFO order** — the order that arrived first fills first.
- If an incoming order is only partially filled by the resting book, the unfilled remainder rests on the book as a new resting order.
- If a resting order is only partially filled, it stays on the book with a reduced quantity — it does *not* lose its place in the FIFO queue.
- An aggressive order that crosses multiple price levels **sweeps** through them in price order (best price first) until it's filled or the book no longer crosses.

### Data structures

| Structure | Purpose | Complexity |
|---|---|---|
| `std::map<Price, std::list<Order>> bids_` | Buy-side book, sorted ascending (best bid = `rbegin()`) | O(log n) insert/best-price, O(1) FIFO push/pop |
| `std::map<Price, std::list<Order>> asks_` | Sell-side book, sorted ascending (best ask = `begin()`) | O(log n) insert/best-price, O(1) FIFO push/pop |
| `std::unordered_map<OrderId, OrderLocation>` | Order ID → `{price, side, list iterator}` | O(1) cancellation by ID |

Using `std::list` for each price level means the `list::iterator` stored in the lookup table stays valid even as other orders in the book are inserted, matched, or removed — no pointer/iterator invalidation to worry about across the order's lifetime, and no need to search for an order when canceling it.

### API

```cpp
Orderbook book;
book.addOrder(Order{ .id = 1, .price = 100, .quantity = 10, .side = Side::Sell });
book.addOrder(Order{ .id = 2, .price = 100, .quantity = 4,  .side = Side::Buy  });
// -> FILL 2 1 4 100   (buyer id, seller id, quantity, price)
book.deleteOrder(1);
book.printBook();
```

## Project structure

```
Orderbook/
├── CMakeLists.txt          # Build config; fetches GoogleTest + Google Benchmark
├── src/
│   ├── order.h             # Order/Price/Quantity/OrderId/Side primitives
│   ├── orderbook.h         # Orderbook class interface
│   ├── orderbook.cpp       # Matching engine + book maintenance
│   └── main.cpp            # Entry point
├── tests/
│   └── orderbook_tests.cpp # 18 GoogleTest unit tests
└── bench/
    └── orderbook_bench.cpp # Google Benchmark throughput/latency suite
```

## Building and running

Requires CMake 4.1+ and a C++23-capable compiler.

```bash
# Build the executable
cmake -S . -B build
cmake --build build --target Orderbook
./build/Orderbook

# Build and run the test suite
cmake --build build --target OrderbookTests
./build/OrderbookTests

# Build and run the benchmark suite (use a Release configure for meaningful numbers)
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target OrderbookBenchmarks
./build/OrderbookBenchmarks
```

## Test coverage

The test suite exercises the matching engine through its observable behavior (stdout from `printBook()` and the fill log), since the class intentionally exposes a minimal public surface (`addOrder`, `deleteOrder`, `printBook`) rather than internal getters:

- **Resting orders & book printing** — orders land on the correct side, at the correct price, with correct aggregated quantity/count
- **Matching correctness** — exact fills, partial fills (on both the resting and incoming side), boundary price equality (`>=`/`<=` at the touch)
- **Price-time priority** — same-price orders fill oldest-first (FIFO)
- **Multi-level sweeps** — a single aggressive order can consume several price levels in one call, best price first
- **Cancellation** — removes the correct order (and only that order) at shared price levels, prunes empty price levels, is a safe no-op on an unknown order ID, and remains correct even after the order was previously partially filled
- **Book ordering** — asks always print ascending, bids always print descending, regardless of insertion order

Run `./build/OrderbookTests` for full output; all 18 tests pass.

## Benchmarks

Throughput and per-operation latency are measured with [Google Benchmark](https://github.com/google/benchmark), fetched automatically via CMake `FetchContent`. `matchIncoming()` logs every fill to `std::cout`, which is great for the CLI demo but would make any matching benchmark mostly measure console I/O syscall cost instead of the order book's actual data-structure work — so the benchmark harness (`bench/orderbook_bench.cpp`) redirects `std::cout` to a no-op sink for the run, without modifying `orderbook.cpp`/`orderbook.h` at all, and reports Google Benchmark's own results table on `stderr` instead. Every timed region also calls `benchmark::DoNotOptimize(book)` + `benchmark::ClobberMemory()` after the measured operation so `-O3` can't elide work whose result is never observed.

Sample results (Apple Silicon, `-O3`, single-threaded; run `./build/OrderbookBenchmarks` to reproduce on your machine — absolute numbers are hardware-dependent, but the relative shape should hold):

| Benchmark | Latency | Throughput |
|---|---|---|
| Resting insert (no match) | ~437 ns/op | ~4.6M orders/sec |
| Immediate match (no book mutation) | ~125 ns/op | ~8.0M matches/sec |
| Cancel order | ~43 ns/op | ~23M cancels/sec |
| Sweep across 100,000 price levels | ~19.5 ms/sweep | ~51 sweeps/sec (100k levels/sweep) |
| Mixed realistic workload (60% add / 30% cross / 10% cancel) | ~78 ns/op | ~12.8M msgs/sec |

What each benchmark isolates:

- **Resting insert** — orders that never cross, so every call takes the "insert into `std::map` + `std::list`, register in `unordered_map`" path. This is the O(log n) price-level lookup plus O(1) list/hash insert cost.
- **Immediate match** — a single deep resting order absorbs every incoming order without ever being exhausted, isolating `matchIncoming()`'s comparison and quantity-bookkeeping cost from any insertion/removal cost.
- **Cancel order** — isolates the O(1) `unordered_map` lookup + `std::list::erase` cost that the `orderLookup_` index exists to provide.
- **Sweep across many price levels** — a book pre-seeded with 100,000 distinct resting price levels gets consumed by a single aggressive order that crosses all of them. Reported as sweeps/sec (one full 100k-level sweep is a single timed operation, since `Iterations(1)` forces exactly one sweep per repetition against a freshly-seeded book); the `levels_per_sweep` counter in the raw output makes the per-level cost recoverable.
- **Mixed realistic workload** — a deterministically-seeded (fixed RNG seed) mix of passive adds, crossing adds (sized to fully deplete and cross into a second resting price level, exercising the list/map erase paths), and cancels against a book with standing liquidity, giving one representative "messages/sec" headline number closer to a live feed than any single micro-benchmark.

## Design notes / what's intentionally out of scope

This project focuses on the matching engine core rather than a full exchange simulation. Not implemented (by design, to keep the core logic focused and testable):

- Order modification (cancel/replace) — currently cancel + re-add
- Market orders / stop orders / time-in-force flags — only standard limit orders
- Multi-threading / concurrency — single-threaded by design; the data structures are not lock-protected
- Persistence or network transport — in-memory only

## Why this design

A matching engine's core difficulty isn't the matching *loop* itself, it's picking data structures that keep both **price discovery** (find the best bid/ask fast) and **order management** (cancel an arbitrary order fast) cheap simultaneously, without one operation paying for the other. The `map` + `list` + `unordered_map` combination here is a deliberate, well-known pattern for that trade-off, and the test suite is written to pin down the matching semantics (price-time priority, partial fills, sweep behavior) that are easy to get subtly wrong.
