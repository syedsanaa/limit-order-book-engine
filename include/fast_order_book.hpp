#pragma once

#include "order.hpp"

#include <vector>
#include <optional>
#include <cstdint>

// ---------------------------------------------------------------------------
// FastOrderBook — cache-aware, allocation-free redesign of OrderBook.
//
// OrderBook (order_book.hpp) uses std::map<price, deque<Order>>: a
// red-black tree where every insert/lookup chases pointers scattered
// across the heap, and every node is its own heap allocation. Great
// asymptotic complexity, bad for a real CPU (a cache miss costs ~100x an
// L1 hit).
//
// FastOrderBook instead uses:
//   1. A flat array of price levels, indexed directly by price
//      (price -> array index, O(1), no tree at all).
//   2. A pre-allocated pool of order "slots" (one big vector) instead of
//      one heap allocation per order. Orders at the same price are linked
//      using ARRAY INDICES instead of pointers ("intrusive" linked list)
//      -- the standard trick for zero allocation in a hot path.
//   3. order id -> pool slot is a direct array lookup, so cancel() is
//      genuinely O(1), unlike OrderBook's linear scan.
//
// TRADE-OFF: needs a bounded price range fixed at construction. Not free
// lunch -- but real exchanges already trade in a bounded integer tick
// range, so this mirrors reality rather than cheating around it.
// ---------------------------------------------------------------------------

class FastOrderBook {
public:
    // [minPrice, maxPrice] (inclusive) is the entire range of prices this
    // book can ever hold. Orders outside this range are dropped -- see
    // addOrder(). Size it generously for your instrument's realistic range.
    FastOrderBook(int64_t minPrice, int64_t maxPrice, size_t expectedOrders = 1 << 16);

    std::vector<Trade> addOrder(Order order);
    bool cancelOrder(uint64_t orderId);

    std::optional<int64_t> bestBid() const;
    std::optional<int64_t> bestAsk() const;
    size_t bidLevels() const { return bidLevelCount_; }
    size_t askLevels() const { return askLevelCount_; }
    size_t restingOrderCount() const { return restingCount_; }

private:
    struct Slot {
        Order order;
        int32_t prev = -1; // previous order at this price level, or -1
        int32_t next = -1; // next order at this price level (or next free slot)
    };

    int64_t minPrice_;
    int64_t maxPrice_;
    size_t rangeSize_;

    std::vector<Slot> pool_;    // contiguous storage for every order
    int32_t freeListHead_ = -1; // head of the reusable-slot chain

    std::vector<int32_t> bidHead_, bidTail_; // per-price-level FIFO queues
    std::vector<int32_t> askHead_, askTail_;
    std::vector<int32_t> idToSlot_; // order id -> pool slot, -1 = not resting

    int64_t bestBidIdx_ = -1; // -1 = no bids resting
    int64_t bestAskIdx_ = -1; // -1 = no asks resting

    size_t bidLevelCount_ = 0;
    size_t askLevelCount_ = 0;
    size_t restingCount_ = 0;

    uint64_t nextTimestamp_ = 0;

    size_t priceIndex(int64_t price) const { return static_cast<size_t>(price - minPrice_); }

    int32_t allocateSlot(const Order& order);
    void freeSlot(int32_t slotIdx);
    void pushBack(Side side, size_t idx, int32_t slotIdx);
    void unlink(Side side, size_t idx, int32_t slotIdx);

    std::vector<Trade> match(Order& incoming);
};
