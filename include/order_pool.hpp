#pragma once
#include <cstddef>
#include <vector>

#include "order.hpp"

class OrderPool {
    std::vector<Order> slots;
    size_t used = 0;
    Order* free_head = nullptr;
    size_t live = 0;

public:
    explicit OrderPool(size_t capacity) : slots(capacity) {}
    OrderPool(const OrderPool&) = delete;
    OrderPool& operator=(const OrderPool&) = delete;

    Order* acquire() {
        Order* slot;
        if (free_head != nullptr) {
            slot = free_head;
            free_head = slot->next;
        } else if (used < slots.size()) {
            slot = &slots[used++];
        } else {
            return nullptr;
        }
        live++;
        return slot;
    }

    void release(Order* slot) {
        slot->next = free_head;
        free_head = slot;
        live--;
    }

    size_t Capacity() const { return slots.size(); }
    size_t Live() const { return live; }
    size_t HighWater() const { return used; }
};
