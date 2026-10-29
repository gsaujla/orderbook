#include "poolbook.hpp"

#include <cassert>

PoolBook::PoolBook(size_t capacity) : pool(capacity) {
    index.reserve(capacity);
}

Order* PoolBook::acquire() {
    if (free_head != nullptr) {
        Order* slot = free_head;
        free_head = slot->next;
        return slot;
    }
    if (used == pool.size()) return nullptr;
    return &pool[used++];
}

void PoolBook::release(Order* slot) {
    slot->next = free_head;
    free_head = slot;
}

template <typename Compare>
void PoolBook::processAdd(map<int64_t, Level, Compare>& subbook, Order* order) {
    order->next = nullptr;
    order->prev = nullptr;
    Level newLevel{order, order, 1, order->qty};
    auto [it, inserted] = subbook.try_emplace(order->price, newLevel);
    if (!inserted) {
        Level& lvl = it->second;
        Order* oldback = lvl.back;
        oldback->next = order;
        order->prev = oldback;
        lvl.size++;
        lvl.back = order;
        lvl.total_qty += order->qty;
    }
}

bool PoolBook::Add(Order order) {
    assert(order.qty > 0);
    if (index.find(order.id) != index.end()) return false;
    Order* ptr = acquire();
    if (ptr == nullptr) return false;
    *ptr = order;
    index.emplace(order.id, ptr);

    if (ptr->side == Side::BUY)
        processAdd(bid, ptr);
    else
        processAdd(ask, ptr);
    return true;
}

template <typename Compare>
bool PoolBook::processCancel(map<int64_t, Level, Compare>& subbook, Order* order) {
    auto it = subbook.find(order->price);
    if (it == subbook.end()) return false;
    Level& lvl = it->second;

    Order* behind = order->prev;
    Order* after = order->next;

    if (behind != nullptr)
        behind->next = after;
    else
        lvl.front = after;

    if (after != nullptr)
        after->prev = behind;
    else
        lvl.back = behind;

    lvl.size--;
    lvl.total_qty -= order->qty;
    if (lvl.size == 0) subbook.erase(it);
    return true;
}

bool PoolBook::Cancel(uint64_t id) {
    auto it = index.find(id);
    if (it == index.end()) return false;

    Order* ptr = it->second;
    bool output;
    if (ptr->side == Side::BUY)
        output = processCancel(bid, ptr);
    else
        output = processCancel(ask, ptr);

    if (output) {
        index.erase(it);
        release(ptr);  // after processCancel: it still needed next/prev to unlink
    }
    return output;
}

optional<int64_t> PoolBook::Best_Bid() const {
    if (bid.empty()) return nullopt;
    return bid.begin()->first;
}

optional<int64_t> PoolBook::Best_Ask() const {
    if (ask.empty()) return nullopt;
    return ask.begin()->first;
}

Order* PoolBook::Front(Side s) {
    if (s == Side::BUY) {
        if (bid.empty()) return nullptr;
        return bid.begin()->second.front;
    } else if (ask.empty())
        return nullptr;
    return ask.begin()->second.front;
}

void PoolBook::Fill(Order* o, uint32_t qty) {
    assert(o->qty >= qty);
    if (o->qty > qty) {
        o->qty -= qty;
        if (o->side == Side::BUY) {
            auto it = bid.find(o->price);
            assert(it != bid.end());
            it->second.total_qty -= qty;
        } else {
            auto it = ask.find(o->price);
            assert(it != ask.end());
            it->second.total_qty -= qty;
        }
    } else if (o->qty == qty) {
        if (o->side == Side::BUY)
            processCancel(bid, o);
        else
            processCancel(ask, o);
        index.erase(o->id);
        release(o);
    }
}

template <typename Compare>
vector<pair<int64_t, uint64_t>> PoolBook::processDepth(const map<int64_t, Level, Compare>& subbook,
                                                       size_t n) {
    vector<pair<int64_t, uint64_t>> levels;
    levels.reserve(min(n, subbook.size()));
    for (auto it = subbook.begin(); it != subbook.end() && levels.size() < n; it++) {
        levels.emplace_back(it->first, it->second.total_qty);
    }
    return levels;
}

vector<pair<int64_t, uint64_t>> PoolBook::Depth(Side s, size_t n) const {
    if (s == Side::BUY) return processDepth(bid, n);
    return processDepth(ask, n);
}

const Order* PoolBook::Front(Side s) const {
    if (s == Side::BUY) {
        if (bid.empty()) return nullptr;
        return bid.begin()->second.front;
    } else if (ask.empty())
        return nullptr;
    return ask.begin()->second.front;
}

Order* PoolBook::Find(uint64_t id) {
    auto it = index.find(id);
    return (it == index.end()) ? nullptr : it->second;
}

const Order* PoolBook::Find(uint64_t id) const {
    auto it = index.find(id);
    return (it == index.end()) ? nullptr : it->second;
}
