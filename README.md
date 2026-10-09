# Limit Order Book

A limit order book and matching engine written in C++17, built as a learning
project for low-latency C++.

## What it does

- Keeps buy and sell orders sorted by price, and by arrival time within the
  same price (price-time priority).
- Matches incoming orders against the book: an order that crosses the spread
  trades immediately, possibly across several price levels, and any remaining
  quantity rests in the book.
- Supports cancelling orders and changing an order's price or quantity.
- Reports every trade (buyer, seller, price, quantity).
- Can run the book on its own thread, receiving orders through a lock-free
  queue.

## Build and run

Requires CMake 3.20+ and a C++17 compiler.

```bash
cmake --preset debug && cmake --build --preset debug && ctest --preset debug
```

```bash
cmake --preset release && cmake --build --preset release && ./build/release/bench/replay_bench
```

## Benchmark

The benchmark replays 10 million generated orders and cancels through the
book. Profiling showed that over half of the time was spent allocating and
freeing memory, so the book was run a second time with a memory pool that
reuses freed memory:

| version | time for 10M events |
|---|---|
| standard memory allocation | 767 ms |
| memory pool | 384 ms |

The memory pool made the book about 2x faster, with identical results.
How to run and profile the benchmark: [bench/README.md](bench/README.md).

## Further improvements

**Pool order book**: a second implementation that allocates all its memory
up front and never allocates while processing orders. Price levels would be
stored in an array indexed by price, and orders in a preallocated array whose
free slots are reused. This removes the remaining overhead of the standard
containers, at the cost of a fixed price range and a fixed maximum number of
orders.
