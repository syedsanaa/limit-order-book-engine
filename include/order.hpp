#pragma once

#include <cstdint>

// ---------------------------------------------------------------------------
// C++ note: this file only defines plain data (structs + enums). No pointers,
// no manual memory management, nothing scary. If you're re-orienting from C,
// the main new thing here is `enum class` (a "scoped enum" — you write
// Side::Buy instead of a bare BUY, which avoids name collisions).
// ---------------------------------------------------------------------------

// Which side of the book an order sits on.
enum class Side : uint8_t { Buy, Sell };

// Limit orders rest in the book if they don't fully match. Market orders
// never rest — they match immediately against whatever's available, and any
// unfilled remainder is simply dropped (this matches how real markets treat
// market orders).
enum class OrderType : uint8_t { Limit, Market };

// A single order.
//
// IMPORTANT DESIGN DECISION: price is an integer number of "ticks"
// (e.g. cents), NOT a double. Comparing doubles for equality/ordering in a
// matching engine is a classic bug source (0.1 + 0.2 != 0.3 in floating
// point). Real exchanges quote prices in fixed tick sizes for exactly this
// reason. If your instrument trades in cents, price=10050 means $100.50.
struct Order {
    uint64_t id;
    Side side;
    OrderType type;
    int64_t price;      // in ticks; ignored for market orders
    uint64_t quantity;  // remaining unfilled quantity
    uint64_t timestamp; // insertion sequence number — lower means earlier,
                         // used to enforce time priority at a price level
};

// A completed match between a resting order and an incoming order.
struct Trade {
    uint64_t restingOrderId;
    uint64_t incomingOrderId;
    int64_t price;      // trades execute at the RESTING order's price
    uint64_t quantity;
    uint64_t timestamp;
};
