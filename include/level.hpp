#pragma once
#include "order.hpp"

class Level {
    Order* front = nullptr;
    Order* back = nullptr;
    int64_t size = 0;
    uint64_t total_qty = 0;

public:
    Level(Order* f, Order* b, int64_t sz, uint64_t qty);

    friend class Book;
    friend class PoolBook;
};

inline Level::Level(Order* f, Order* b, int64_t sz, uint64_t qty)
    : front(f), back(b), size(sz), total_qty(qty) {}
