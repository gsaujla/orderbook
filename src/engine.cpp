#include "engine.hpp"



uint32_t Engine::processMarketOrder(const NewOrder& freshorder) {
    uint32_t qty = freshorder.qty;
    Side opposite = (freshorder.side == Side::BUY) ? Side::SELL : Side::BUY;
    Side s = freshorder.side;
    Order* best = book.Front(opposite);

    uint32_t completed = 0;
    while (completed < qty && best != nullptr) {
        
        uint32_t tradeQty = min(best->qty, qty-completed);
        uint64_t buyer_id = (s == Side::BUY) ? freshorder.id : best->id;
        uint64_t seller_id = (s == Side::SELL) ? freshorder.id : best->id;
        Trade t {buyer_id, seller_id, best->price, tradeQty};

        book.Fill(best, tradeQty);
        completed += tradeQty;
        best = book.Front(opposite);
        trades.emplace_back(t);
    }

    return qty - completed;
}

uint32_t Engine::processLimitOrder(const NewOrder& freshorder) {
    uint32_t qty = freshorder.qty;
    Side opposite = (freshorder.side == Side::BUY) ? Side::SELL : Side::BUY;
    int64_t limitprice = freshorder.price;
    Side s = freshorder.side;
    Order* best = book.Front(opposite);
    
    

    uint32_t completed = 0;
    while (completed < qty && best != nullptr) {
        if (s == Side::BUY) {
            if (best->price > limitprice) break;
        } else if (best->price < limitprice) break;

        uint32_t tradeQty = min(best->qty, qty-completed);
        uint64_t buyer_id = (s == Side::BUY) ? freshorder.id : best->id;
        uint64_t seller_id = (s == Side::SELL) ? freshorder.id : best->id;
        Trade t {buyer_id, seller_id, best->price, tradeQty};

        book.Fill(best, tradeQty);
        completed += tradeQty;
        best = book.Front(opposite);
        trades.emplace_back(t);
    }
    
    if (qty - completed > 0) {
        Order bookbound {freshorder.id, qty - completed, limitprice, s};
        book.Add(bookbound);
    }
    return qty - completed;
}

uint32_t Engine::submit(const NewOrder& freshorder) {
    
    
    uint32_t left = 0;
    if (freshorder.type == OrderType::Market) left = processMarketOrder(freshorder);
    else left = processLimitOrder(freshorder);
    return left;

}


