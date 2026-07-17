// orderbook_bench.cpp
//
// Google Benchmark suite measuring throughput and per-operation latency of
// Orderbook's hot paths: resting inserts, immediate matches, cancellation,
// a multi-level sweep, and a realistic mixed workload.
//
// NOTE ON I/O: matchIncoming() logs every fill via std::cout. That's fine
// for the CLI demo, but the syscall + formatting cost of real console I/O
// has nothing to do with the algorithmic cost of the book itself, and would
// dominate (and add large variance to) any matching benchmark. main() below
// redirects std::cout to a no-op sink for the duration of the run so these
// numbers reflect the map/list/unordered_map work, not terminal I/O. This
// does not touch orderbook.cpp/h in any way.

#include <benchmark/benchmark.h>

#include <cstdint>
#include <iostream>
#include <limits>
#include <random>
#include <streambuf>
#include <vector>

#include "orderbook.h"

// ---------------------------------------------------------------------
// 1. Pure insert path: orders that never cross, so every call takes the
//    "no match, rest on book" branch. Prices walk apart monotonically so
//    the two sides can never overlap even after tens of millions of calls.
// ---------------------------------------------------------------------
static void BM_AddOrder_RestingNoMatch(benchmark::State& state) {
    Orderbook book;
    Price buyPrice = 1'000'000'000;
    Price askPrice = 1'100'000'000;
    OrderId id = 1;

    for (auto _ : state) {
        book.addOrder(Order{id++, buyPrice--, 10, Side::Buy});
        book.addOrder(Order{id++, askPrice++, 10, Side::Sell});
        benchmark::DoNotOptimize(book);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * 2);
}
BENCHMARK(BM_AddOrder_RestingNoMatch);

// ---------------------------------------------------------------------
// 2. Pure match path: one deep resting ask absorbs every incoming buy
//    without ever being exhausted, so no map/list mutation ever happens on
//    the resting side. Isolates the cost of matchIncoming()'s comparison +
//    quantity bookkeeping from insertion/removal cost.
// ---------------------------------------------------------------------
class MatchFixture : public benchmark::Fixture {
public:
    Orderbook book;
    void SetUp(const benchmark::State&) override {
        book.addOrder(Order{0, 100, std::numeric_limits<Quantity>::max(), Side::Sell});
    }
};

BENCHMARK_F(MatchFixture, BM_AddOrder_ImmediateMatch)(benchmark::State& state) {
    OrderId id = 1;
    for (auto _ : state) {
        book.addOrder(Order{id++, 100, 1, Side::Buy});
        benchmark::DoNotOptimize(book);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations());
}

// ---------------------------------------------------------------------
// 3. Cancellation: batches of unique resting orders are built up outside
//    the timed region (PauseTiming), then deleted one by one while timed,
//    isolating the O(1) unordered_map lookup + list::erase cost.
// ---------------------------------------------------------------------
static void BM_CancelOrder(benchmark::State& state) {
    constexpr int kBatch = 1000;
    OrderId nextId = 1;

    for (auto _ : state) {
        state.PauseTiming();
        Orderbook book;
        std::vector<OrderId> ids;
        ids.reserve(kBatch);
        for (int i = 0; i < kBatch; ++i) {
            OrderId id = nextId++;
            book.addOrder(Order{id, static_cast<Price>(2'000'000'000 + i), 10, Side::Buy});
            ids.push_back(id);
        }
        state.ResumeTiming();

        for (OrderId id : ids) {
            book.deleteOrder(id);
        }
        benchmark::DoNotOptimize(book);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * kBatch);
}
BENCHMARK(BM_CancelOrder);

// ---------------------------------------------------------------------
// 4. Multi-level sweep: a book with many distinct resting price levels gets
//    consumed by a single aggressive order that crosses all of them. This
//    is a one-shot destructive operation, so Iterations(1)+Repetitions(N)
//    forces exactly one sweep per timed measurement, with SetUp/TearDown
//    rebuilding the book fresh before each repetition.
// ---------------------------------------------------------------------
class SweepFixture : public benchmark::Fixture {
public:
    static constexpr int kLevels = 100'000;
    Orderbook book;

    void SetUp(const benchmark::State&) override {
        for (int i = 0; i < kLevels; ++i) {
            book.addOrder(Order{static_cast<OrderId>(i + 1), static_cast<Price>(i + 1), 1, Side::Sell});
        }
    }
};

BENCHMARK_DEFINE_F(SweepFixture, BM_SweepManyPriceLevels)(benchmark::State& state) {
    for (auto _ : state) {
        book.addOrder(Order{
            std::numeric_limits<OrderId>::max(),
            static_cast<Price>(kLevels),
            static_cast<Quantity>(kLevels),
            Side::Buy
        });
        benchmark::DoNotOptimize(book);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations());
    state.counters["levels_per_sweep"] = kLevels;
}
BENCHMARK_REGISTER_F(SweepFixture, BM_SweepManyPriceLevels)
    ->Iterations(1)
    ->Repetitions(5)
    ->Unit(benchmark::kMillisecond);

// ---------------------------------------------------------------------
// 5. Realistic mixed workload: a deterministically-seeded mix of passive
//    adds (60%), crossing/aggressive adds (30%), and cancels (10%) against
//    a book seeded with initial resting liquidity. Gives a single
//    representative "messages/sec" headline number.
// ---------------------------------------------------------------------
static void BM_MixedRealisticWorkload(benchmark::State& state) {
    constexpr int kBatch = 2000;
    std::mt19937 rng(42);
    std::uniform_int_distribution<int> opPicker(0, 99);
    std::uniform_int_distribution<int> priceJitter(0, 20);
    OrderId nextId = 1;

    for (auto _ : state) {
        state.PauseTiming();
        Orderbook book;
        std::vector<OrderId> resting;
        for (int i = 0; i < 200; ++i) {
            OrderId id = nextId++;
            book.addOrder(Order{id, static_cast<Price>(1000 + i), 10, Side::Sell});
            resting.push_back(id);
        }
        state.ResumeTiming();

        for (int i = 0; i < kBatch; ++i) {
            int roll = opPicker(rng);
            if (roll < 60) {
                OrderId id = nextId++;
                book.addOrder(Order{id, static_cast<Price>(500 + priceJitter(rng)), 5, Side::Buy});
                resting.push_back(id);
            } else if (roll < 90) {
                OrderId id = nextId++;
                book.addOrder(Order{id, static_cast<Price>(1000 + priceJitter(rng)), 15, Side::Buy});
            } else if (!resting.empty()) {
                std::uniform_int_distribution<size_t> pick(0, resting.size() - 1);
                size_t idx = pick(rng);
                book.deleteOrder(resting[idx]);
                resting[idx] = resting.back();
                resting.pop_back();
            }
        }
        benchmark::DoNotOptimize(book);
        benchmark::ClobberMemory();
    }
    state.SetItemsProcessed(state.iterations() * kBatch);
}
BENCHMARK(BM_MixedRealisticWorkload);

// ---------------------------------------------------------------------
// main(): redirect std::cout to a no-op sink so fill logging doesn't pollute
// the timing (see file header note above).
// ---------------------------------------------------------------------
namespace {
class NullBuffer : public std::streambuf {
protected:
    int overflow(int c) override { return c; }
};
} // namespace

int main(int argc, char** argv) {
    // Google Benchmark's own results table is written to std::cout by
    // default, so muting std::cout globally would silence it too. Point the
    // reporter at std::cerr instead, keeping the mute scoped to the
    // Orderbook's own fill logging.
    benchmark::ConsoleReporter reporter;
    reporter.SetOutputStream(&std::cerr);
    reporter.SetErrorStream(&std::cerr);

    static NullBuffer nullBuffer;
    std::streambuf* oldBuf = std::cout.rdbuf(&nullBuffer);

    benchmark::Initialize(&argc, argv);
    if (benchmark::ReportUnrecognizedArguments(argc, argv)) return 1;
    benchmark::RunSpecifiedBenchmarks(&reporter);
    benchmark::Shutdown();

    std::cout.rdbuf(oldBuf);
    return 0;
}
