#pragma once
#include <vector>

#include "book.hpp"


enum class OrderType {
    Limit, Market
};

struct NewOrder {
    uint64_t id;
    uint32_t qty;
    int64_t price;
    Side side;

    OrderType type;
};

struct Trade {
    uint64_t buyer_id;
    uint64_t seller_id;

    int64_t price;
    uint32_t qty;
};

class Engine {
    Book book;
    vector<Trade> trades;

    uint32_t processMarketOrder(const NewOrder& freshorder);
    uint32_t processLimitOrder(const NewOrder& freshorder);
public:
    uint32_t submit(const NewOrder& freshorder);
    void cancel();
};
