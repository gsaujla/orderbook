#pragma once
#include "book.hpp"
#include <vector>


class Trade {
    uint64_t buyer_id;
    uint64_t seller_id;

    int64_t price;
    uint32_t qty;
};


class Engine {
    Book book;
    vector<Trade> trades;

    public:
    void submit();
    void cancel();
};

