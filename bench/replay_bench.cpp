// Replays a fixed, seeded stream of order events through the book and
// measures throughput. Run the Release build, pinned to one core on Linux:
//
//   taskset -c 2 ./build/release/bench/replay_bench

#include <benchmark/benchmark.h>

#include <cstdint>
#include <memory_resource>
#include <random>
#include <variant>
#include <vector>
#include "map_order_book.h"

namespace {

constexpr std::size_t kEventCount = 10'000'000;
constexpr std::uint64_t kSeed = 42;

// Synthetic order flow: ~55% passive adds near the mid, ~40% cancels,
// ~5% aggressive adds that cross the spread. Prices are fixed around one mid,
// so passive buys (mid-4..mid) and sells (mid+1..mid+5) never cross each other.
std::vector<Command> makeEvents(std::uint64_t seed, std::size_t count) {
    std::mt19937_64 rng(seed);  // raw engine output is identical on every platform
    auto random = [&rng](std::uint64_t n) { return rng() % n; };  // [0, n)
    constexpr Price kMid = 100'000;
    constexpr std::size_t kMaxLive = 10'000;  // keeps the book from growing over the run

    std::vector<OrderId> live;  // added and not yet cancelled; some get filled meanwhile
    OrderId nextId = 0;
    std::vector<Command> events;
    events.reserve(count);

    while (events.size() < count) {
        const auto roll = random(100);
        if ((roll >= 55 && roll < 95 && !live.empty()) || live.size() >= kMaxLive) {
            const std::size_t i = random(live.size());
            const OrderId id = live[i];
            live[i] = live.back();
            live.pop_back();
            events.push_back(CancelCommand{id});
            continue;
        }
        const bool aggressive = roll >= 95;
        const Side side = random(2) == 0 ? Side::Buy : Side::Sell;
        Price price;
        if (side == Side::Buy)
            price = aggressive ? kMid + 1 + random(5) : kMid - random(5);
        else
            price = aggressive ? kMid - random(5) : kMid + 1 + random(5);
        const auto quantity = static_cast<Quantity>(1 + random(aggressive ? 300 : 100));

        const Order order{nextId++, side, price, quantity};
        live.push_back(order.orderId);
        events.push_back(AddCommand{order});
    }
    return events;
}

// Built once, on first use, before any timing starts.
const std::vector<Command>& events() {
    static const std::vector<Command> e = makeEvents(kSeed, kEventCount);
    return e;
}

// Replays all events through a fresh book per iteration. With usePool, the
// book's container nodes come from a memory pool instead of new/delete; the
// pool is created per iteration too, so both variants do the same setup and
// teardown work inside the timed loop.
void replay(benchmark::State& state, bool usePool) {
    const std::vector<Command>& ev = events();
    std::size_t cancels = 0;
    std::size_t cancelMisses = 0;
    std::size_t fills = 0;
    std::size_t resting = 0;

    for (auto _ : state) {
        // A fresh book every iteration: replaying into a used book would start
        // with leftover orders and reject every add as a duplicate id.
        std::pmr::unsynchronized_pool_resource pool;  // declared before book: must outlive it
        MapOrderBook book(usePool ? static_cast<std::pmr::memory_resource*>(&pool)
                                  : std::pmr::new_delete_resource());
        cancels = 0;
        cancelMisses = 0;
        for (const Command& e : ev) {
            if (const auto* add = std::get_if<AddCommand>(&e)) {
                benchmark::DoNotOptimize(book.placeOrder(add->order));
            } else if (const auto* cancel = std::get_if<CancelCommand>(&e)) {
                ++cancels;
                cancelMisses += book.cancelOrder(cancel->orderId) ? 0 : 1;  // using the result keeps the call
            }
        }
        fills = book.fills().size();
        resting = book.orderCount();
        benchmark::ClobberMemory();
    }

    state.SetItemsProcessed(static_cast<std::int64_t>(state.iterations()) * static_cast<std::int64_t>(ev.size()));
    state.counters["fills"] = static_cast<double>(fills);
    state.counters["resting_at_end"] = static_cast<double>(resting);
    // Cancels of orders that were already filled: cheap lookups that find nothing.
    state.counters["cancel_miss_pct"] = 100.0 * static_cast<double>(cancelMisses) / static_cast<double>(cancels);
}

void BM_MapOrderBookReplay(benchmark::State& state) { replay(state, false); }
void BM_MapOrderBookReplayPool(benchmark::State& state) { replay(state, true); }

BENCHMARK(BM_MapOrderBookReplay)
    ->Unit(benchmark::kMillisecond)
    ->MinWarmUpTime(1.0)          // one untimed replay first: warms caches, branch predictors, allocator
    ->Repetitions(5)              // 5 independent runs ...
    ->ReportAggregatesOnly(true); // ... reported as mean / median / stddev / cv

BENCHMARK(BM_MapOrderBookReplayPool)
    ->Unit(benchmark::kMillisecond)
    ->MinWarmUpTime(1.0)
    ->Repetitions(5)
    ->ReportAggregatesOnly(true);

}  // namespace
