#include "order_book.hpp"

#include <algorithm>

std::vector<Trade> OrderBook::addOrder(Order order) {
    order.timestamp = nextTimestamp_++;

    std::vector<Trade> trades = match(order);

    // Anything left over from a limit order becomes a resting order.
    // Market orders never rest — leftover quantity is simply dropped,
    // matching how real markets treat unfilled market orders.
    if (order.quantity > 0 && order.type == OrderType::Limit) {
        insertResting(order);
    }

    return trades;
}

std::vector<Trade> OrderBook::match(Order& incoming) {
    std::vector<Trade> trades;

    if (incoming.side == Side::Buy) {
        // A buy matches against the ask side, cheapest ask first.
        // asks_ is sorted ascending, so asks_.begin() is always the best ask.
        while (incoming.quantity > 0 && !asks_.empty()) {
            auto bestLevel = asks_.begin();
            int64_t askPrice = bestLevel->first;

            // A market order crosses at any price. A limit order only
            // crosses if it's willing to pay at least the ask price.
            bool crosses = (incoming.type == OrderType::Market) ||
                           (incoming.price >= askPrice);
            if (!crosses) break;

            std::deque<Order>& queue = bestLevel->second;
            Order& resting = queue.front(); // oldest order at this price = time priority

            uint64_t fillQty = std::min(incoming.quantity, resting.quantity);
            trades.push_back(Trade{resting.id, incoming.id, askPrice, fillQty,
                                    nextTimestamp_++});

            incoming.quantity -= fillQty;
            resting.quantity -= fillQty;

            if (resting.quantity == 0) {
                locations_.erase(resting.id);
                queue.pop_front();
                if (queue.empty()) {
                    asks_.erase(bestLevel); // no orders left at this price level
                }
            }
        }
    } else {
        // A sell matches against the bid side, highest bid first.
        // bids_ uses std::greater as its comparator, so begin() is highest.
        while (incoming.quantity > 0 && !bids_.empty()) {
            auto bestLevel = bids_.begin();
            int64_t bidPrice = bestLevel->first;

            bool crosses = (incoming.type == OrderType::Market) ||
                           (incoming.price <= bidPrice);
            if (!crosses) break;

            std::deque<Order>& queue = bestLevel->second;
            Order& resting = queue.front();

            uint64_t fillQty = std::min(incoming.quantity, resting.quantity);
            trades.push_back(Trade{resting.id, incoming.id, bidPrice, fillQty,
                                    nextTimestamp_++});

            incoming.quantity -= fillQty;
            resting.quantity -= fillQty;

            if (resting.quantity == 0) {
                locations_.erase(resting.id);
                queue.pop_front();
                if (queue.empty()) {
                    bids_.erase(bestLevel);
                }
            }
        }
    }

    return trades;
}

void OrderBook::insertResting(Order order) {
    locations_[order.id] = Location{order.side, order.price};

    if (order.side == Side::Buy) {
        bids_[order.price].push_back(order); // push_back -> joins the back
                                              // of the FIFO queue at this
                                              // price, i.e. behind orders
                                              // that arrived earlier
    } else {
        asks_[order.price].push_back(order);
    }
}

// Small helper shared by both branches of cancelOrder below. It's a template
// because bids_ and asks_ are different C++ types (they use different sort
// comparators: std::greater for bids, default std::less for asks), even
// though the logic to erase from either one is identical. Templates let us
// write the logic once and have the compiler generate a version for each
// concrete type — this is what "generic code" looks like in C++, as opposed
// to void* + casts in C.
template <typename PriceMap>
static bool eraseFromLevel(PriceMap& book, int64_t price, uint64_t orderId) {
    auto levelIt = book.find(price);
    if (levelIt == book.end()) return false; // shouldn't happen, but be safe

    std::deque<Order>& queue = levelIt->second;

    // NOTE / future work: this is a linear scan within the price level.
    // Fine for a learning project and for realistic order-cancel rates at a
    // single price level, but a production engine would use an intrusive
    // linked list per order so cancel is O(1) instead of O(orders at price).
    auto orderIt = std::find_if(queue.begin(), queue.end(),
                                 [orderId](const Order& o) { return o.id == orderId; });
    if (orderIt == queue.end()) return false;

    queue.erase(orderIt);
    if (queue.empty()) {
        book.erase(levelIt);
    }
    return true;
}

bool OrderBook::cancelOrder(uint64_t orderId) {
    auto it = locations_.find(orderId);
    if (it == locations_.end()) return false; // not a resting order

    Side side = it->second.side;
    int64_t price = it->second.price;

    bool erased = (side == Side::Buy) ? eraseFromLevel(bids_, price, orderId)
                                       : eraseFromLevel(asks_, price, orderId);
    if (!erased) return false;

    locations_.erase(it);
    return true;
}

std::optional<int64_t> OrderBook::bestBid() const {
    if (bids_.empty()) return std::nullopt;
    return bids_.begin()->first;
}

std::optional<int64_t> OrderBook::bestAsk() const {
    if (asks_.empty()) return std::nullopt;
    return asks_.begin()->first;
}
