# Benchmark and profiling

`replay_bench.cpp` replays a fixed, seeded stream of 10M order events
(~55% passive adds, ~40% cancels, ~5% aggressive adds) through a fresh
`MapOrderBook` and reports throughput. It runs twice: `BM_MapOrderBookReplay`
(container nodes from ordinary new/delete) and `BM_MapOrderBookReplayPool`
(the same book, nodes from a `std::pmr::unsynchronized_pool_resource`). It uses Google Benchmark: one warm-up
second, then 5 repetitions reported as mean / median / stddev / cv.

## Run the benchmark

Always use the Release build (the Debug build has sanitizers and is several
times slower).

```bash
cmake --preset release
cmake --build --preset release
./build/release/bench/replay_bench
```

Read the `_median` row: `Time` is one full replay of 10M events,
`items_per_second` is the throughput. `cv` is the run-to-run spread; above
~5% the measurement is too noisy to trust.

Counters printed with each result (sanity checks):

| counter           | meaning                                                   |
|-------------------|-----------------------------------------------------------|
| `fills`           | trades produced in one replay                             |
| `resting_at_end`  | orders left in the book after the replay                  |
| `cancel_miss_pct` | cancels of orders that were already filled (~32%: the generator does not know which orders got filled) |

Tips for stable numbers:

- Close heavy applications first. Other processes compete for the CPU and its
  caches; a busy machine made the same binary ~20% slower.
- Only compare results from the same session. To compare two versions, run
  them back to back (or interleaved), not on different days.
- Linux: pin to one core, e.g. `taskset -c 2 ./build/release/bench/replay_bench`.
  (macOS has no way to pin a thread to a core.)

## Profile: where does the time go?

A profiler samples the program's call stack many times per second; the
functions that show up most often are where the time is spent.

### macOS (`sample`, built in)

Start the benchmark, wait ~3 seconds so the 10M events have been generated,
then sample it from a second terminal for 3 seconds:

```bash
./build/release/bench/replay_bench
```

```bash
sample replay_bench 3 -file profile.txt
```

Open `profile.txt` and look for the section **"Sort by top of stack"**: it
lists functions by *self time* (samples where that function itself was
running). The call tree above it shows who called whom.

For readable function names, profile the `relwithdebinfo` preset instead
(same optimization, plus symbols and frame pointers):

```bash
cmake --preset relwithdebinfo
cmake --build --preset relwithdebinfo
./build/relwithdebinfo/bench/replay_bench
```

### Linux (`perf`)

Where the time goes (sampling, with call stacks):

```bash
perf record -g ./build/relwithdebinfo/bench/replay_bench
perf report
```

Hardware counters (not available on macOS without Xcode's Instruments):

```bash
perf stat -e cycles,instructions,cache-misses,branch-misses ./build/release/bench/replay_bench
```

IPC = instructions / cycles. Below ~1 means the CPU is mostly waiting
(usually on memory); 2-4 means it is busy computing.

Note: both commands measure the whole program, including generating the
events and Google Benchmark itself. The replay dominates, so the conclusions
hold, but the numbers are not "replay only".

## What the profile showed (macOS, `sample`)

Over half of the replay time is spent in the memory allocator
(`malloc`/`free`): every resting order allocates a `std::list` node and an
`unordered_map` node, and frees them on fill or cancel. `cancelOrder` is the
most expensive function in our own code (hash lookup + `std::map` search +
list erase + hash erase, each touching separately allocated nodes).

## Experiment: memory pool (measured)

Hypothesis from the profile: allocation dominates, so reusing freed nodes from
a pool instead of calling malloc/free should help a lot. The book code is
identical in both runs; only the memory resource passed to `MapOrderBook`
differs.

Interleaved runs in one session (Apple Silicon Mac, Release, not pinned,
other applications running), median of 5:

| variant                              | time per 10M events | throughput    | cv    |
|--------------------------------------|---------------------|---------------|-------|
| `MapOrderBook` (new/delete)          | 817 ms              | 12.3M events/s | 1.1% |
| `MapOrderBook` + pool                | 389 ms              | 25.8M events/s | 0.5% |

About 2.1x faster, with identical fills and resting orders. Profiling the
pooled run: malloc/free went from ~54% of samples to 0%; the pool's own
allocate/deallocate takes ~14%. `cancelOrder` is now the largest cost: the
remaining time is the containers' own work (hashing, tree search, following
pointers between separately allocated nodes).

