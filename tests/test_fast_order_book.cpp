#include "fast_order_book.hpp"

#include <cassert>
#include <cstdio>

// Same test scenarios as tests/test_order_book.cpp -- FastOrderBook must
// behave identically to OrderBook. If any of these diverge, the redesign
// introduced a correctness bug, and speed doesn't matter until that's fixed.

static Order makeLimit(uint64_t id, Side side, int64_t price, uint64_t qty) {
    return Order{id, side, OrderType::Limit, price, qty, 0};
}

static Order makeMarket(uint64_t id, Side side, uint64_t qty) {
    return Order{id, side, OrderType::Market, 0, qty, 0};
}

void test_resting_order_with_no_match() {
    FastOrderBook book(0, 1000);
    auto trades = book.addOrder(makeLimit(1, Side::Buy, 100, 10));
    assert(trades.empty());
    assert(book.bestBid() == 100);
    assert(book.bestAsk() == std::nullopt);
    printf("PASS: test_resting_order_with_no_match\n");
}

void test_full_fill_exact_quantity() {
    FastOrderBook book(0, 1000);
    book.addOrder(makeLimit(1, Side::Sell, 100, 10));
    auto trades = book.addOrder(makeLimit(2, Side::Buy, 100, 10));

    assert(trades.size() == 1);
    assert(trades[0].price == 100);
    assert(trades[0].quantity == 10);
    assert(trades[0].restingOrderId == 1);
    assert(trades[0].incomingOrderId == 2);
    assert(book.bestAsk() == std::nullopt);
    assert(book.bestBid() == std::nullopt);
    printf("PASS: test_full_fill_exact_quantity\n");
}

void test_partial_fill_leaves_remainder_resting() {
    FastOrderBook book(0, 1000);
    book.addOrder(makeLimit(1, Side::Sell, 100, 5));
    auto trades = book.addOrder(makeLimit(2, Side::Buy, 100, 10));

    assert(trades.size() == 1);
    assert(trades[0].quantity == 5);
    assert(book.bestAsk() == std::nullopt);
    assert(book.bestBid() == 100);
    assert(book.restingOrderCount() == 1);
    printf("PASS: test_partial_fill_leaves_remainder_resting\n");
}

void test_price_time_priority_fifo() {
    FastOrderBook book(0, 1000);
    book.addOrder(makeLimit(1, Side::Buy, 100, 5));
    book.addOrder(makeLimit(2, Side::Buy, 100, 5));

    auto trades = book.addOrder(makeLimit(3, Side::Sell, 100, 5));
    assert(trades.size() == 1);
    assert(trades[0].restingOrderId == 1);
    printf("PASS: test_price_time_priority_fifo\n");
}

void test_limit_order_does_not_cross_at_bad_price() {
    FastOrderBook book(0, 1000);
    book.addOrder(makeLimit(1, Side::Sell, 100, 10));
    auto trades = book.addOrder(makeLimit(2, Side::Buy, 99, 10));

    assert(trades.empty());
    assert(book.bestAsk() == 100);
    assert(book.bestBid() == 99);
    printf("PASS: test_limit_order_does_not_cross_at_bad_price\n");
}

void test_market_order_sweeps_multiple_levels() {
    FastOrderBook book(0, 1000);
    book.addOrder(makeLimit(1, Side::Sell, 100, 5));
    book.addOrder(makeLimit(2, Side::Sell, 101, 5));

    auto trades = book.addOrder(makeMarket(3, Side::Buy, 8));
    assert(trades.size() == 2);
    assert(trades[0].price == 100);
    assert(trades[0].quantity == 5);
    assert(trades[1].price == 101);
    assert(trades[1].quantity == 3);
    assert(book.bestAsk() == 101);
    printf("PASS: test_market_order_sweeps_multiple_levels\n");
}

void test_market_order_unfilled_remainder_is_dropped_not_rested() {
    FastOrderBook book(0, 1000);
    book.addOrder(makeLimit(1, Side::Sell, 100, 3));
    auto trades = book.addOrder(makeMarket(2, Side::Buy, 10));

    assert(trades.size() == 1);
    assert(trades[0].quantity == 3);
    assert(book.restingOrderCount() == 0);
    printf("PASS: test_market_order_unfilled_remainder_is_dropped_not_rested\n");
}

void test_cancel_removes_resting_order() {
    FastOrderBook book(0, 1000);
    book.addOrder(makeLimit(1, Side::Buy, 100, 10));
    assert(book.cancelOrder(1) == true);
    assert(book.bestBid() == std::nullopt);
    assert(book.cancelOrder(1) == false);
    assert(book.cancelOrder(999) == false);
    printf("PASS: test_cancel_removes_resting_order\n");
}

void test_slot_reuse_after_cancel() {
    // Specific to FastOrderBook's pool design: cancel an order, then add a
    // new one -- the freed slot should be reused, not leak.
    FastOrderBook book(0, 1000);
    book.addOrder(makeLimit(1, Side::Buy, 100, 10));
    book.cancelOrder(1);
    book.addOrder(makeLimit(2, Side::Buy, 105, 20));
    assert(book.bestBid() == 105);
    assert(book.restingOrderCount() == 1);
    printf("PASS: test_slot_reuse_after_cancel\n");
}

int main() {
    test_resting_order_with_no_match();
    test_full_fill_exact_quantity();
    test_partial_fill_leaves_remainder_resting();
    test_price_time_priority_fifo();
    test_limit_order_does_not_cross_at_bad_price();
    test_market_order_sweeps_multiple_levels();
    test_market_order_unfilled_remainder_is_dropped_not_rested();
    test_cancel_removes_resting_order();
    test_slot_reuse_after_cancel();
    printf("\nAll tests passed.\n");
    return 0;
}
