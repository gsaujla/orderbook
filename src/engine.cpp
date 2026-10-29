#include "engine.hpp"

template <typename BookT>
uint32_t BasicEngine<BookT>::processOrder(const NewOrder& freshorder) {
    OrderType ot = freshorder.type;
    uint32_t qty = freshorder.qty;
    Side opposite = (freshorder.side == Side::BUY) ? Side::SELL : Side::BUY;
    Side s = freshorder.side;
    Order* best = book.Front(opposite);
    int64_t potentiallimitprice = freshorder.price;

    uint32_t completed = 0;
    while (completed < qty && best != nullptr) {
        if (ot == OrderType::Limit) {
            if (s == Side::BUY) {
                if (best->price > potentiallimitprice) break;
            } else if (best->price < potentiallimitprice)
                break;
        }

        uint32_t tradeQty = min(best->qty, qty - completed);
        uint64_t buyer_id = (s == Side::BUY) ? freshorder.id : best->id;
        uint64_t seller_id = (s == Side::SELL) ? freshorder.id : best->id;
        Trade t{buyer_id, seller_id, best->price, tradeQty};

        book.Fill(best, tradeQty);
        completed += tradeQty;
        best = book.Front(opposite);
        trades.emplace_back(t);
    }
    if (ot == OrderType::Limit) {
        if (qty > completed) {
            Order bookbound{freshorder.id, qty - completed, potentiallimitprice, s};
            book.Add(bookbound);
        }
    }

    return qty - completed;
}

template <typename BookT>
uint32_t BasicEngine<BookT>::submit(const NewOrder& freshorder) {
    return processOrder(freshorder);
}

template <typename BookT>
bool BasicEngine<BookT>::cancel(uint64_t id) {
    return book.Cancel(id);
}

template <typename BookT>
const vector<Trade>& BasicEngine<BookT>::GetTrades() const {
    return trades;
}

template <typename BookT>
const BookT& BasicEngine<BookT>::GetBook() const {
    return book;
}

template <typename BookT>
void BasicEngine<BookT>::ClearTrades() {
    trades.clear();
}

template class BasicEngine<Book>;
template class BasicEngine<PoolBook>;
