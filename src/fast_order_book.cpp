#include "fast_order_book.hpp"

#include <algorithm>

FastOrderBook::FastOrderBook(int64_t minPrice, int64_t maxPrice, size_t expectedOrders)
    : minPrice_(minPrice), maxPrice_(maxPrice),
      rangeSize_(static_cast<size_t>(maxPrice - minPrice + 1)) {
    bidHead_.assign(rangeSize_, -1);
    bidTail_.assign(rangeSize_, -1);
    askHead_.assign(rangeSize_, -1);
    askTail_.assign(rangeSize_, -1);
    pool_.reserve(expectedOrders);
    idToSlot_.reserve(expectedOrders);
}

int32_t FastOrderBook::allocateSlot(const Order& order) {
    if (freeListHead_ != -1) {
        int32_t slotIdx = freeListHead_;
        freeListHead_ = pool_[slotIdx].next; // pop from the reuse chain
        pool_[slotIdx] = Slot{order, -1, -1};
        return slotIdx;
    }
    int32_t slotIdx = static_cast<int32_t>(pool_.size());
    pool_.push_back(Slot{order, -1, -1});
    return slotIdx;
}

void FastOrderBook::freeSlot(int32_t slotIdx) {
    // Push this slot onto the free chain, reusing the `next` field --
    // no heap deallocation happens here, the slot just becomes available
    // for the next allocateSlot() call.
    pool_[slotIdx].next = freeListHead_;
    pool_[slotIdx].prev = -1;
    freeListHead_ = slotIdx;
}

void FastOrderBook::pushBack(Side side, size_t idx, int32_t slotIdx) {
    std::vector<int32_t>& head = (side == Side::Buy) ? bidHead_ : askHead_;
    std::vector<int32_t>& tail = (side == Side::Buy) ? bidTail_ : askTail_;

    bool wasEmpty = (head[idx] == -1);
    if (wasEmpty) {
        head[idx] = tail[idx] = slotIdx;
    } else {
        pool_[tail[idx]].next = slotIdx;
        pool_[slotIdx].prev = tail[idx];
        tail[idx] = slotIdx;
    }

    if (wasEmpty) {
        if (side == Side::Buy) {
            ++bidLevelCount_;
            if (bestBidIdx_ == -1 || static_cast<int64_t>(idx) > bestBidIdx_)
                bestBidIdx_ = static_cast<int64_t>(idx);
        } else {
            ++askLevelCount_;
            if (bestAskIdx_ == -1 || static_cast<int64_t>(idx) < bestAskIdx_)
                bestAskIdx_ = static_cast<int64_t>(idx);
        }
    }
}

void FastOrderBook::unlink(Side side, size_t idx, int32_t slotIdx) {
    std::vector<int32_t>& head = (side == Side::Buy) ? bidHead_ : askHead_;
    std::vector<int32_t>& tail = (side == Side::Buy) ? bidTail_ : askTail_;

    Slot& slot = pool_[slotIdx];
    if (slot.prev != -1) pool_[slot.prev].next = slot.next; else head[idx] = slot.next;
    if (slot.next != -1) pool_[slot.next].prev = slot.prev; else tail[idx] = slot.prev;

    if (head[idx] == -1) {
        // Level just became empty.
        if (side == Side::Buy) {
            --bidLevelCount_;
            if (static_cast<int64_t>(idx) == bestBidIdx_) {
                // Scan toward worse prices for the new best bid. A
                // contiguous array scan -- cache-friendly even though it's
                // not worst-case O(1).
                int64_t scan = bestBidIdx_ - 1;
                while (scan >= 0 && bidHead_[scan] == -1) --scan;
                bestBidIdx_ = scan;
            }
        } else {
            --askLevelCount_;
            if (static_cast<int64_t>(idx) == bestAskIdx_) {
                int64_t scan = bestAskIdx_ + 1;
                while (scan < static_cast<int64_t>(rangeSize_) && askHead_[scan] == -1) ++scan;
                bestAskIdx_ = (scan < static_cast<int64_t>(rangeSize_)) ? scan : -1;
            }
        }
    }
}

std::vector<Trade> FastOrderBook::match(Order& incoming) {
    std::vector<Trade> trades;

    if (incoming.side == Side::Buy) {
        while (incoming.quantity > 0 && bestAskIdx_ != -1) {
            int64_t askPrice = minPrice_ + bestAskIdx_;
            bool crosses = (incoming.type == OrderType::Market) || (incoming.price >= askPrice);
            if (!crosses) break;

            size_t idx = static_cast<size_t>(bestAskIdx_);
            int32_t headSlot = askHead_[idx];
            Order& resting = pool_[headSlot].order;

            uint64_t fillQty = std::min(incoming.quantity, resting.quantity);
            trades.push_back(Trade{resting.id, incoming.id, askPrice, fillQty, nextTimestamp_++});

            incoming.quantity -= fillQty;
            resting.quantity -= fillQty;

            if (resting.quantity == 0) {
                idToSlot_[resting.id] = -1;
                unlink(Side::Sell, idx, headSlot);
                freeSlot(headSlot);
                --restingCount_;
            }
        }
    } else {
        while (incoming.quantity > 0 && bestBidIdx_ != -1) {
            int64_t bidPrice = minPrice_ + bestBidIdx_;
            bool crosses = (incoming.type == OrderType::Market) || (incoming.price <= bidPrice);
            if (!crosses) break;

            size_t idx = static_cast<size_t>(bestBidIdx_);
            int32_t headSlot = bidHead_[idx];
            Order& resting = pool_[headSlot].order;

            uint64_t fillQty = std::min(incoming.quantity, resting.quantity);
            trades.push_back(Trade{resting.id, incoming.id, bidPrice, fillQty, nextTimestamp_++});

            incoming.quantity -= fillQty;
            resting.quantity -= fillQty;

            if (resting.quantity == 0) {
                idToSlot_[resting.id] = -1;
                unlink(Side::Buy, idx, headSlot);
                freeSlot(headSlot);
                --restingCount_;
            }
        }
    }

    return trades;
}

std::vector<Trade> FastOrderBook::addOrder(Order order) {
    order.timestamp = nextTimestamp_++;

    if (order.type == OrderType::Limit &&
        (order.price < minPrice_ || order.price > maxPrice_)) {
        return {}; // out of the configured range; dropped
    }

    std::vector<Trade> trades = match(order);

    if (order.quantity > 0 && order.type == OrderType::Limit) {
        if (static_cast<size_t>(order.id) >= idToSlot_.size()) {
            idToSlot_.resize(order.id + 1, -1);
        }
        int32_t slotIdx = allocateSlot(order);
        idToSlot_[order.id] = slotIdx;
        pushBack(order.side, priceIndex(order.price), slotIdx);
        ++restingCount_;
    }

    return trades;
}

bool FastOrderBook::cancelOrder(uint64_t orderId) {
    if (orderId >= idToSlot_.size()) return false;
    int32_t slotIdx = idToSlot_[orderId];
    if (slotIdx == -1) return false;

    const Order& order = pool_[slotIdx].order;
    size_t idx = priceIndex(order.price);

    unlink(order.side, idx, slotIdx);
    freeSlot(slotIdx);
    idToSlot_[orderId] = -1;
    --restingCount_;
    return true;
}

std::optional<int64_t> FastOrderBook::bestBid() const {
    if (bestBidIdx_ == -1) return std::nullopt;
    return minPrice_ + bestBidIdx_;
}

std::optional<int64_t> FastOrderBook::bestAsk() const {
    if (bestAskIdx_ == -1) return std::nullopt;
    return minPrice_ + bestAskIdx_;
}
