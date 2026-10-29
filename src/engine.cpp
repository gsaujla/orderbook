#include "engine.hpp"

uint32_t Engine::processOrder(const NewOrder& freshorder) {
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

uint32_t Engine::submit(const NewOrder& freshorder) {
    return processOrder(freshorder);
}

bool Engine::cancel(uint64_t id) {
    return book.Cancel(id);
}

const vector<Trade>& Engine::GetTrades() const {
    return trades;
}

const Book& Engine::GetBook() const {
    return book;
}

void Engine::ClearTrades() {
    trades.clear();
}
