#include "order_book.hpp"

#include <cassert>
#include <cstdio>

// ---------------------------------------------------------------------------
// No test framework here on purpose — plain assert() is enough for now and
// keeps the dependency list at zero. Each test is a function; run them all
// from main(). If an assert fails, the program aborts and prints the file/
// line, which is all you need to find the bug.
// ---------------------------------------------------------------------------

static Order makeLimit(uint64_t id, Side side, int64_t price, uint64_t qty) {
    return Order{id, side, OrderType::Limit, price, qty, 0};
}

static Order makeMarket(uint64_t id, Side side, uint64_t qty) {
    return Order{id, side, OrderType::Market, 0, qty, 0};
}

void test_resting_order_with_no_match() {
    OrderBook book;
    auto trades = book.addOrder(makeLimit(1, Side::Buy, 100, 10));
    assert(trades.empty());
    assert(book.bestBid() == 100);
    assert(book.bestAsk() == std::nullopt);
    printf("PASS: test_resting_order_with_no_match\n");
}

void test_full_fill_exact_quantity() {
    OrderBook book;
    book.addOrder(makeLimit(1, Side::Sell, 100, 10));
    auto trades = book.addOrder(makeLimit(2, Side::Buy, 100, 10));

    assert(trades.size() == 1);
    assert(trades[0].price == 100);
    assert(trades[0].quantity == 10);
    assert(trades[0].restingOrderId == 1);
    assert(trades[0].incomingOrderId == 2);
    assert(book.bestAsk() == std::nullopt); // fully consumed
    assert(book.bestBid() == std::nullopt); // fully consumed
    printf("PASS: test_full_fill_exact_quantity\n");
}

void test_partial_fill_leaves_remainder_resting() {
    OrderBook book;
    book.addOrder(makeLimit(1, Side::Sell, 100, 5));
    auto trades = book.addOrder(makeLimit(2, Side::Buy, 100, 10));

    assert(trades.size() == 1);
    assert(trades[0].quantity == 5);
    assert(book.bestAsk() == std::nullopt);  // seller fully filled
    assert(book.bestBid() == 100);           // buyer has 5 left, resting
    assert(book.restingOrderCount() == 1);
    printf("PASS: test_partial_fill_leaves_remainder_resting\n");
}

void test_price_time_priority_fifo() {
    OrderBook book;
    // Two resting buy orders at the same price — order 1 arrives first,
    // so it should be filled first (time priority).
    book.addOrder(makeLimit(1, Side::Buy, 100, 5));
    book.addOrder(makeLimit(2, Side::Buy, 100, 5));

    auto trades = book.addOrder(makeLimit(3, Side::Sell, 100, 5));
    assert(trades.size() == 1);
    assert(trades[0].restingOrderId == 1); // the EARLIER buy order, not order 2
    printf("PASS: test_price_time_priority_fifo\n");
}

void test_limit_order_does_not_cross_at_bad_price() {
    OrderBook book;
    book.addOrder(makeLimit(1, Side::Sell, 100, 10));
    // Buyer only willing to pay 99, ask is 100 -> no trade, buy order rests.
    auto trades = book.addOrder(makeLimit(2, Side::Buy, 99, 10));

    assert(trades.empty());
    assert(book.bestAsk() == 100);
    assert(book.bestBid() == 99);
    printf("PASS: test_limit_order_does_not_cross_at_bad_price\n");
}

void test_market_order_sweeps_multiple_levels() {
    OrderBook book;
    book.addOrder(makeLimit(1, Side::Sell, 100, 5));
    book.addOrder(makeLimit(2, Side::Sell, 101, 5));

    auto trades = book.addOrder(makeMarket(3, Side::Buy, 8));
    assert(trades.size() == 2);
    assert(trades[0].price == 100);
    assert(trades[0].quantity == 5);
    assert(trades[1].price == 101);
    assert(trades[1].quantity == 3);
    assert(book.bestAsk() == 101); // 2 units left at 101
    printf("PASS: test_market_order_sweeps_multiple_levels\n");
}

void test_market_order_unfilled_remainder_is_dropped_not_rested() {
    OrderBook book;
    book.addOrder(makeLimit(1, Side::Sell, 100, 3));
    auto trades = book.addOrder(makeMarket(2, Side::Buy, 10)); // only 3 available

    assert(trades.size() == 1);
    assert(trades[0].quantity == 3);
    assert(book.restingOrderCount() == 0); // remainder of 7 was NOT added to the book
    printf("PASS: test_market_order_unfilled_remainder_is_dropped_not_rested\n");
}

void test_cancel_removes_resting_order() {
    OrderBook book;
    book.addOrder(makeLimit(1, Side::Buy, 100, 10));
    assert(book.cancelOrder(1) == true);
    assert(book.bestBid() == std::nullopt);
    assert(book.cancelOrder(1) == false); // already gone, can't cancel twice
    assert(book.cancelOrder(999) == false); // never existed
    printf("PASS: test_cancel_removes_resting_order\n");
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
    printf("\nAll tests passed.\n");
    return 0;
}
