#pragma once
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

#include "level.hpp"
using namespace std;

class PoolBook {
    map<int64_t, Level, greater<>> bid;
    map<int64_t, Level, less<>> ask;

    vector<Order> pool;                   
    size_t used = 0;                       
    Order* free_head = nullptr;             
    unordered_map<uint64_t, Order*> index;  

    Order* acquire();          
    void release(Order* slot); 
    template <typename Compare>
    static void processAdd(map<int64_t, Level, Compare>& subbook, Order* order);
    template <typename Compare>
    static bool processCancel(map<int64_t, Level, Compare>& subbook, Order* order);
    template <typename Compare>
    static vector<pair<int64_t, uint64_t>> processDepth(const map<int64_t, Level, Compare>& subbook,
                                                        size_t n);

public:
    explicit PoolBook(size_t capacity);
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

    size_t Capacity() const { return pool.size(); }
    size_t Size() const { return index.size(); }
};
