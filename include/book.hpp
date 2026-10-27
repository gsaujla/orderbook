#pragma once
#include <map>
#include <optional>
#include <unordered_map>

#include "order.hpp"
using namespace std;

class Level {
    Order* front = nullptr;
    Order* back = nullptr;
    int64_t size = 0;

public:
    Level(Order* f, Order* b, int64_t sz);

    friend class Book;
};

class Book {
    map<int64_t, Level, greater<>> bid;
    map<int64_t, Level, less<>> ask;
    unordered_map<uint64_t, Order> ordermap;
    template <typename Compare>
    static void processAdd(map<int64_t, Level, Compare>& subbook, Order* order);
    template <typename Compare>
    static void processCancel(map<int64_t, Level, Compare>& subbook, Order* order);

public:
    void Add(Order order);
    void Cancel(uint64_t id);
    optional<int64_t> Best_Bid() const;
    optional<int64_t> Best_Ask() const;
};
