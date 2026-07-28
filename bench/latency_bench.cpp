#include "order_book.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <random>
#include <vector>

// ---------------------------------------------------------------------------
// WHAT THIS MEASURES
//
// We pre-generate a realistic sequence of events (new orders + cancels)
// BEFORE starting the clock, so RNG cost never pollutes the timing loop.
// Then we replay that sequence through a real OrderBook, timing each
// individual operation with std::chrono::steady_clock (monotonic, won't
// jump backward — unlike wall-clock time, which can).
//
// PERCENTILES, IN PLAIN TERMS
// Sort every recorded latency fastest -> slowest.
//   p50  = the value at the halfway point   -> "typical" latency
//   p99  = the value 99% of calls beat      -> catches occasional slow ones
//   p99.9 = the value 99.9% of calls beat   -> your worst realistic case
// We report percentiles instead of the average because averages hide
// outliers: one 50-microsecond cache-miss stall barely moves an average
// over 500,000 samples, but it's exactly the kind of event that matters in
// a latency-sensitive system. Tail latency, not average latency, is what
// production trading systems are actually graded on.
//
// HONEST CAVEAT
// This runs in a shared, virtualized container with no CPU pinning and no
// control over the host's other tenants. Treat these numbers as internally
// consistent for comparing "before" vs "after" a code change on this same
// machine -- not as absolute numbers you'd quote for a real exchange.
// ---------------------------------------------------------------------------

struct BenchEvent {
    enum class Kind { NewOrder, Cancel };
    Kind kind;
    Order order;       // valid when kind == NewOrder
    uint64_t cancelId;  // valid when kind == Cancel
};

// Builds a synthetic but realistic order flow: a randomly walking reference
// price, a mix of limit orders (some resting, some crossing the spread),
// occasional market orders, and cancels of previously-issued limit orders.
static std::vector<BenchEvent> generateEvents(size_t count, uint32_t seed) {
    std::vector<BenchEvent> events;
    events.reserve(count);

    std::mt19937 rng(seed); // fixed seed -> reproducible, comparable runs
    std::uniform_int_distribution<int> eventPick(0, 99);
    std::uniform_int_distribution<int> sidePick(0, 1);
    std::uniform_int_distribution<int64_t> walkStep(-2, 2);
    std::uniform_int_distribution<int64_t> spreadOffset(-20, 20);
    std::uniform_int_distribution<uint64_t> qtyPick(1, 100);

    int64_t referencePrice = 10000; // starting mid price, in ticks (cents)
    uint64_t nextOrderId = 1;
    std::vector<uint64_t> restingIds; // pool of ids we can try to cancel

    for (size_t i = 0; i < count; ++i) {
        referencePrice += walkStep(rng); // slow random walk of the "market"
        int roll = eventPick(rng);

        if (roll < 5 && !restingIds.empty()) {
            // 5%: cancel a previously issued limit order (may already be
            // filled/gone -- that's realistic, cancelOrder just returns false)
            std::uniform_int_distribution<size_t> pick(0, restingIds.size() - 1);
            size_t idx = pick(rng);
            uint64_t id = restingIds[idx];
            restingIds[idx] = restingIds.back();
            restingIds.pop_back();
            events.push_back(BenchEvent{BenchEvent::Kind::Cancel, Order{}, id});
        } else if (roll < 15) {
            // 10%: market order -- demands immediacy, never rests
            Side side = sidePick(rng) ? Side::Buy : Side::Sell;
            events.push_back(BenchEvent{
                BenchEvent::Kind::NewOrder,
                Order{nextOrderId++, side, OrderType::Market, 0, qtyPick(rng), 0},
                0});
        } else {
            // 85%: limit order scattered around the reference price. Some
            // land inside the spread and cross immediately, others rest.
            Side side = sidePick(rng) ? Side::Buy : Side::Sell;
            int64_t price = referencePrice + spreadOffset(rng);
            uint64_t id = nextOrderId++;
            events.push_back(BenchEvent{
                BenchEvent::Kind::NewOrder,
                Order{id, side, OrderType::Limit, price, qtyPick(rng), 0},
                0});
            restingIds.push_back(id); // may or may not still be resting later
        }
    }
    return events;
}

static void report(const char* label, std::vector<uint64_t>& latenciesNs) {
    if (latenciesNs.empty()) {
        printf("%-24s (no samples)\n", label);
        return;
    }
    std::sort(latenciesNs.begin(), latenciesNs.end());
    auto pct = [&](double p) -> uint64_t {
        size_t idx = static_cast<size_t>(p * (latenciesNs.size() - 1));
        return latenciesNs[idx];
    };
    printf("%-24s n=%-8zu p50=%6llu ns   p90=%6llu ns   p99=%7llu ns   p99.9=%8llu ns   max=%8llu ns\n",
           label, latenciesNs.size(),
           (unsigned long long)pct(0.50), (unsigned long long)pct(0.90),
           (unsigned long long)pct(0.99), (unsigned long long)pct(0.999),
           (unsigned long long)latenciesNs.back());
}

int main(int argc, char** argv) {
    size_t count = 500000;
    if (argc > 1) count = static_cast<size_t>(std::atol(argv[1]));

    printf("Generating %zu synthetic events...\n", count);
    std::vector<BenchEvent> events = generateEvents(count, /*seed=*/42);

    OrderBook book;
    std::vector<uint64_t> addLatencies;
    std::vector<uint64_t> cancelLatencies;
    addLatencies.reserve(count);
    cancelLatencies.reserve(count / 10);

    // Small warm-up pass so the first few real measurements aren't paying
    // for cold instruction/data cache effects.
    OrderBook warmup;
    for (int i = 0; i < 1000; ++i) {
        warmup.addOrder(Order{(uint64_t)i + 1, Side::Buy, OrderType::Limit, 10000, 10, 0});
    }

    auto wallStart = std::chrono::steady_clock::now();

    for (const BenchEvent& ev : events) {
        if (ev.kind == BenchEvent::Kind::NewOrder) {
            auto t0 = std::chrono::steady_clock::now();
            book.addOrder(ev.order);
            auto t1 = std::chrono::steady_clock::now();
            addLatencies.push_back(
                std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        } else {
            auto t0 = std::chrono::steady_clock::now();
            book.cancelOrder(ev.cancelId);
            auto t1 = std::chrono::steady_clock::now();
            cancelLatencies.push_back(
                std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        }
    }

    auto wallEnd = std::chrono::steady_clock::now();
    double wallSeconds = std::chrono::duration<double>(wallEnd - wallStart).count();
    double throughput = static_cast<double>(events.size()) / wallSeconds;

    printf("\n--- Baseline: std::map<price, std::deque<Order>> ---\n");
    printf("Total events: %zu   wall time: %.4f s   throughput: %.0f events/sec\n\n",
           events.size(), wallSeconds, throughput);
    report("addOrder latency", addLatencies);
    report("cancelOrder latency", cancelLatencies);
    printf("\nFinal book state: bidLevels=%zu askLevels=%zu restingOrders=%zu\n",
           book.bidLevels(), book.askLevels(), book.restingOrderCount());

    return 0;
}
