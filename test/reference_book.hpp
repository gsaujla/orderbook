#pragma once
// Deliberately naive order book + matching engine used as a correctness oracle.
// Written independently of Book/Engine: no intrusive lists, no pointers into other
// containers, no cached totals. Slow, but each step is easy to check by eye.

#include <algorithm>
#include <cstdint>
#include <functional>
#include <list>
#include <map>
#include <unordered_map>
#include <utility>
#include <vector>

#include "engine.hpp"

class ReferenceEngine {
public:
    struct Resting {
        uint64_t id;
        uint32_t qty;
    };

    // Returns qty left over (rested for a limit, dropped for a market), like Engine::submit.
    uint32_t submit(const NewOrder& o) {
        uint32_t remaining = o.qty;
        if (o.side == Side::BUY)
            remaining = match(o, remaining, asks);
        else
            remaining = match(o, remaining, bids);

        if (o.type == OrderType::Limit && remaining > 0) {
            if (o.side == Side::BUY)
                bids[o.price].push_back({o.id, remaining});
            else
                asks[o.price].push_back({o.id, remaining});
            where[o.id] = {o.side, o.price};
        }
        return remaining;
    }

    bool cancel(uint64_t id) {
        auto w = where.find(id);
        if (w == where.end()) return false;
        auto [side, price] = w->second;
        where.erase(w);
        if (side == Side::BUY) return eraseFrom(bids, price, id);
        return eraseFrom(asks, price, id);
    }

    // Same shape as Book::Depth: best price first, (price, sum of resting qty).
    std::vector<std::pair<int64_t, uint64_t>> depth(Side s) const {
        if (s == Side::BUY) return depthOf(bids);
        return depthOf(asks);
    }

    const std::vector<Trade>& trades() const { return trades_; }
    bool isLive(uint64_t id) const { return where.count(id) != 0; }
    size_t liveCount() const { return where.size(); }

private:
    std::map<int64_t, std::list<Resting>, std::greater<>> bids;
    std::map<int64_t, std::list<Resting>, std::less<>> asks;
    std::unordered_map<uint64_t, std::pair<Side, int64_t>> where;  // id -> (side, price)
    std::vector<Trade> trades_;

    static bool crosses(const NewOrder& o, int64_t restingPrice) {
        if (o.type == OrderType::Market) return true;
        if (o.side == Side::BUY) return restingPrice <= o.price;
        return restingPrice >= o.price;
    }

    template <typename Levels>
    uint32_t match(const NewOrder& o, uint32_t remaining, Levels& opposite) {
        while (remaining > 0 && !opposite.empty()) {
            auto level = opposite.begin();
            if (!crosses(o, level->first)) break;

            auto& queue = level->second;
            Resting& front = queue.front();
            uint32_t qty = std::min(remaining, front.qty);

            Trade t{};
            t.buyer_id = (o.side == Side::BUY) ? o.id : front.id;
            t.seller_id = (o.side == Side::SELL) ? o.id : front.id;
            t.price = level->first;
            t.qty = qty;
            trades_.push_back(t);

            remaining -= qty;
            front.qty -= qty;
            if (front.qty == 0) {
                where.erase(front.id);
                queue.pop_front();
            }
            if (queue.empty()) opposite.erase(level);
        }
        return remaining;
    }

    template <typename Levels>
    static bool eraseFrom(Levels& levels, int64_t price, uint64_t id) {
        auto level = levels.find(price);
        if (level == levels.end()) return false;
        auto& queue = level->second;
        auto it =
            std::find_if(queue.begin(), queue.end(), [id](const Resting& r) { return r.id == id; });
        if (it == queue.end()) return false;
        queue.erase(it);
        if (queue.empty()) levels.erase(level);
        return true;
    }

    template <typename Levels>
    static std::vector<std::pair<int64_t, uint64_t>> depthOf(const Levels& levels) {
        std::vector<std::pair<int64_t, uint64_t>> out;
        for (const auto& [price, queue] : levels) {
            uint64_t total = 0;
            for (const Resting& r : queue) total += r.qty;
            out.emplace_back(price, total);
        }
        return out;
    }
};
