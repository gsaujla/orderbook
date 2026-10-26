#include "book.hpp"



Level::Level (Order* f, Order* b, int64_t sz): front(f), back(b), size(sz) {}



template <typename Compare>
void Book::processAdd (map<int64_t, Level, Compare>& subbook, Order* order) {
    order->next = nullptr;
    order->prev = nullptr;
    Level newLevel {order, order, 1};
    auto [it, inserted]  = subbook.try_emplace(order->price, newLevel);
    if (!inserted) {
        Level& lvl = it->second;
        Order* oldback = lvl.back;
        oldback->next = order;
        order->prev = oldback;
        lvl.size++;
        lvl.back = order;
    }
}

void Book::Add(Order order) {
    
    if (ordermap.find(order.id) != ordermap.end()) return;
    
    ordermap[order.id] = order;
    Order* ptr = &ordermap[order.id];

    if (order.side == Side::BUY) processAdd(bid, ptr);
    else processAdd(ask, ptr);
    
}

