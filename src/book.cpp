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
    
    auto [it, inserted] = ordermap.try_emplace(order.id, order);
    if (!inserted) return;
    Order* ptr = &it->second;

    if (ptr->side == Side::BUY) processAdd(bid, ptr);
    else processAdd(ask, ptr);
    
}

template <typename Compare>
void Book::processCancel (map<int64_t, Level, Compare>& subbook, Order* order) {
    auto it = subbook.find(order->price);
    if (it == subbook.end()) return;
    Level& lvl = it->second;

    Order* behind = order->prev;
    Order* after = order->next;

    if (behind != nullptr) behind->next = after;
    else lvl.front = after;

    if (after != nullptr) after->prev = behind;
    else lvl.back = behind;

    lvl.size--;

    if (lvl.size == 0) subbook.erase(it);


}

void Book::Cancel (uint64_t id) {
    auto it = ordermap.find(id);
    if (it == ordermap.end()) return;
    
    Order* ptr = &it->second;
    if (ptr->side == Side::BUY) processCancel(bid, ptr);
    else processCancel(ask, ptr);
    
    ordermap.erase(it);
}


optional<int64_t> Book::Best_Bid() const {
    if (bid.empty()) return nullopt;
    return bid.begin()->first;
}

optional<int64_t> Book::Best_Ask() const {
    if (ask.empty()) return nullopt;
    return ask.begin()->first;
}