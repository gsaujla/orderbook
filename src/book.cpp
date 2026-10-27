#include "book.hpp"
#include <cassert>

Level::Level(Order* f, Order* b, int64_t sz, uint64_t qty) : front(f), back(b), size(sz), total_qty(qty) {}

template <typename Compare>
void Book::processAdd(map<int64_t, Level, Compare>& subbook, Order* order) {
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

bool Book::Add(Order order) {
    assert(order.qty > 0);
    auto [it, inserted] = ordermap.try_emplace(order.id, order);
    if (!inserted) return false;
    Order* ptr = &it->second;

    if (ptr->side == Side::BUY)
        processAdd(bid, ptr);
    else
        processAdd(ask, ptr);
    return true;
}

template <typename Compare>
bool Book::processCancel(map<int64_t, Level, Compare>& subbook, Order* order) {
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

bool Book::Cancel(uint64_t id) {
    auto it = ordermap.find(id);
    if (it == ordermap.end()) return false;

    Order* ptr = &it->second;
    bool output;
    if (ptr->side == Side::BUY)
        output= processCancel(bid, ptr);
    else
        output = processCancel(ask, ptr);

    if (output) ordermap.erase(it);
    return output;
}

optional<int64_t> Book::Best_Bid() const {
    if (bid.empty()) return nullopt;
    return bid.begin()->first;
}

optional<int64_t> Book::Best_Ask() const {
    if (ask.empty()) return nullopt;
    return ask.begin()->first;
}


Order* Book::Front(Side s) {
    if (s == Side::BUY) {
        if (bid.empty()) return nullptr;
        return bid.begin()->second.front;
    } else if (ask.empty()) return nullptr;
    return ask.begin()->second.front;
    
}

void Book::Fill(Order* o, uint32_t qty) {
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
        if (o->side == Side::BUY) processCancel(bid, o);
        else processCancel(ask, o);
        auto it = ordermap.find(o->id);
        ordermap.erase(it);
    }
}

template <typename Compare>
vector<pair<int64_t, uint64_t>> Book::processDepth(const map<int64_t, Level, Compare>& subbook, size_t n) {
    vector<pair<int64_t, uint64_t>> levels;
    levels.reserve(min(n, subbook.size()));
    for (auto it = subbook.begin(); it != subbook.end() && levels.size() < n; it++) {
        levels.emplace_back(it->first, it->second.total_qty);
    }
    return levels;
}

vector<pair<int64_t, uint64_t>> Book::Depth(Side s, size_t n) const {
    if (s == Side::BUY) return processDepth(bid, n);
    return processDepth(ask, n);
}

