#pragma once
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

#include "level.hpp"
using namespace std;


class Book {
    map<int64_t, Level, greater<>> bid;
    map<int64_t, Level, less<>> ask;
    unordered_map<uint64_t, Order> ordermap;
    template <typename Compare>
    static void processAdd(map<int64_t, Level, Compare>& subbook, Order* order);
    template <typename Compare>
    static bool processCancel(map<int64_t, Level, Compare>& subbook, Order* order);
    template <typename Compare>
    static vector<pair<int64_t, uint64_t>> processDepth(const map<int64_t, Level, Compare>& subbook,
                                                        size_t n);

public:
    bool Add(Order order);
    bool Cancel(uint64_t id);
    optional<int64_t> Best_Bid() const;
    optional<int64_t> Best_Ask() const;
    Order* Front(Side);
    void Fill(Order* o, uint32_t qty);
    vector<pair<int64_t, uint64_t>> Depth(Side s, size_t n) const;
    const Order* Front(Side) const;

    Order* Find(uint64_t id);
    const Order* Find(uint64_t id) const;
};
