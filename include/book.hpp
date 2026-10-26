#pragma once
#include "order.hpp"
#include <unordered_map>
#include <map>
using namespace std;



class Level {
    Order* front = nullptr;
    Order* back = nullptr;
    int64_t size = 0;
    public:
        Level(Order* f, Order* b, int64_t sz);


    friend class Book;
};

//  bid, ask
// l1, l2
// umap price -> order <-> order <->
class Book
{
    map<int64_t, Level, greater<>> bid;
    map<int64_t, Level, less<>> ask;
    unordered_map<uint64_t, Order> ordermap;
    template <typename Compare>
    static void processAdd (map<int64_t, Level, Compare>& subbook, Order* order);

    public:
        void Add(Order order);
        void Cancel(Order order);
        int64_t Best_Bid();
        int64_t Best_Ask();
        
};





