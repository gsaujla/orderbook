#pragma once
#include <utility>
#include <vector>

#include "book.hpp"
#include "poolbook.hpp"

enum class OrderType { Limit, Market };

struct NewOrder {
    uint64_t id;
    uint32_t qty;
    int64_t price;
    Side side;

    OrderType type;
};

struct Trade {
    uint64_t buyer_id;
    uint64_t seller_id;

    int64_t price;
    uint32_t qty;
};

template <typename BookT>
class BasicEngine {
    BookT book;
    vector<Trade> trades;

    uint32_t processOrder(const NewOrder& freshorder);

public:
    template <typename... Args>
    explicit BasicEngine(Args&&... args) : book(std::forward<Args>(args)...) {}

    uint32_t submit(const NewOrder& freshorder);
    bool cancel(uint64_t id);
    const vector<Trade>& GetTrades() const;
    const BookT& GetBook() const;
    void ClearTrades();
};

extern template class BasicEngine<Book>;
extern template class BasicEngine<PoolBook>;

using Engine = BasicEngine<Book>;
using PoolEngine = BasicEngine<PoolBook>;
