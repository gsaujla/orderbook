#pragma once

#include <cstdint>

enum struct Side { SELL, BUY };

struct Order {
    uint64_t id;
    uint32_t qty;
    int64_t price;
    Side side;
    Order* next = nullptr;
    Order* prev = nullptr;
};
