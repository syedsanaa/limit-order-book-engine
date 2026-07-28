#include "order_book.hpp"

#include <cstdio>
#include <string>

static void printTrades(const std::vector<Trade>& trades) {
    for (const Trade& t : trades) {
        printf("  TRADE  resting=#%llu incoming=#%llu price=%lld qty=%llu\n",
               (unsigned long long)t.restingOrderId,
               (unsigned long long)t.incomingOrderId,
               (long long)t.price,
               (unsigned long long)t.quantity);
    }
}

static void printBookState(const OrderBook& book) {
    auto bid = book.bestBid();
    auto ask = book.bestAsk();
    printf("  book: best_bid=%s best_ask=%s levels(bid=%zu, ask=%zu)\n",
           bid ? std::to_string(*bid).c_str() : "none",
           ask ? std::to_string(*ask).c_str() : "none",
           book.bidLevels(), book.askLevels());
}

int main() {
    OrderBook book;
    uint64_t nextId = 1;

    printf("1) Resting sell orders arrive first (no crossing order yet)\n");
    printTrades(book.addOrder(Order{nextId++, Side::Sell, OrderType::Limit, 10050, 10, 0}));
    printTrades(book.addOrder(Order{nextId++, Side::Sell, OrderType::Limit, 10100, 5, 0}));
    printBookState(book);

    printf("\n2) A buy limit order crosses and partially fills\n");
    printTrades(book.addOrder(Order{nextId++, Side::Buy, OrderType::Limit, 10075, 6, 0}));
    printBookState(book);

    printf("\n3) A market buy sweeps through remaining levels\n");
    printTrades(book.addOrder(Order{nextId++, Side::Buy, OrderType::Market, 0, 20, 0}));
    printBookState(book);

    printf("\nDone. See tests/test_order_book.cpp for the full behavior spec.\n");
    return 0;
}
