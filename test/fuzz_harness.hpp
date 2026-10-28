#pragma once
// Drives Engine and ReferenceEngine with the same seeded random events and checks
// after every event that they agree. Shared by the gtest fuzz tests and the
// standalone `fuzz` binary used for long runs.

#include <cstdint>
#include <cstdio>
#include <deque>
#include <limits>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "engine.hpp"
#include "reference_book.hpp"

namespace fuzz {

struct Config {
    uint64_t seed = 1;
    uint64_t events = 100000;
    int64_t start_mid = 10000;  // ticks; use 0 to exercise negative prices
    int64_t spread = 8;         // how far from mid a limit can rest
    int64_t cross = 2;          // how far past mid a limit can reach (aggressive orders)
    uint32_t max_qty = 100;
    int limit_pct = 50;  // remaining percent are cancels
    int market_pct = 8;
    int bad_cancel_pct = 2;  // cancels of ids that were never live
};

struct Event {
    enum Kind { Submit, Cancel } kind;
    NewOrder order;  // Submit
    uint64_t id;     // Cancel
};

inline std::string describe(const Event& e) {
    std::ostringstream s;
    if (e.kind == Event::Cancel) {
        s << "cancel id=" << e.id;
    } else {
        const NewOrder& o = e.order;
        s << (o.type == OrderType::Market ? "market " : "limit ")
          << (o.side == Side::BUY ? "BUY " : "SELL ") << "id=" << o.id << " qty=" << o.qty;
        if (o.type == OrderType::Limit) s << " price=" << o.price;
    }
    return s.str();
}

inline std::string describe(const Trade& t) {
    std::ostringstream s;
    s << "{buyer=" << t.buyer_id << " seller=" << t.seller_id << " price=" << t.price
      << " qty=" << t.qty << "}";
    return s.str();
}

inline bool same(const Trade& a, const Trade& b) {
    return a.buyer_id == b.buyer_id && a.seller_id == b.seller_id && a.price == b.price &&
           a.qty == b.qty;
}

struct Result {
    bool ok = true;
    uint64_t events_run = 0;
    uint64_t trades = 0;
    size_t max_live = 0;
    std::string error;  // filled on divergence
};

inline Result run(const Config& cfg) {
    std::mt19937_64 rng(cfg.seed);
    auto roll = [&](int64_t lo, int64_t hi) {
        return std::uniform_int_distribution<int64_t>(lo, hi)(rng);
    };

    Engine engine;
    ReferenceEngine ref;
    Result res;

    int64_t mid = cfg.start_mid;
    uint64_t next_id = 1;              // ids are never reused
    std::vector<uint64_t> candidates;  // ids that may still be resting
    std::deque<Event> recent;          // last few events, for the failure report

    auto fail = [&](uint64_t i, const std::string& what) {
        std::ostringstream s;
        s << "divergence at event " << i << " (seed " << cfg.seed << "): " << what
          << "\nlast events:\n";
        for (const Event& e : recent) s << "  " << describe(e) << "\n";
        res.ok = false;
        res.error = s.str();
        res.events_run = i + 1;
        return res;
    };

    for (uint64_t i = 0; i < cfg.events; i++) {
        if (roll(0, 99) < 5) mid += roll(-1, 1);  // slow random walk

        Event ev{};
        int r = static_cast<int>(roll(0, 99));
        if (r < cfg.limit_pct + cfg.market_pct || candidates.empty()) {
            ev.kind = Event::Submit;
            ev.order.id = next_id++;
            ev.order.side = roll(0, 1) ? Side::BUY : Side::SELL;
            ev.order.qty = static_cast<uint32_t>(roll(1, cfg.max_qty));
            if (r >= cfg.limit_pct && r < cfg.limit_pct + cfg.market_pct) {
                ev.order.type = OrderType::Market;
                ev.order.price = 0;
            } else {
                ev.order.type = OrderType::Limit;
                // Buys lean below mid and sells above it, so most limits rest and the
                // book builds depth; the `cross` ticks of overlap keep trades happening.
                if (ev.order.side == Side::BUY)
                    ev.order.price = mid + roll(-cfg.spread, cfg.cross);
                else
                    ev.order.price = mid + roll(-cfg.cross, cfg.spread);
            }
        } else {
            ev.kind = Event::Cancel;
            if (roll(0, 99) < cfg.bad_cancel_pct) {
                ev.id = next_id + 1000000;  // never issued
            } else {
                size_t k = static_cast<size_t>(roll(0, (int64_t)candidates.size() - 1));
                ev.id = candidates[k];
                candidates[k] = candidates.back();
                candidates.pop_back();
            }
        }

        recent.push_back(ev);
        if (recent.size() > 12) recent.pop_front();

        size_t trades_before = ref.trades().size();
        if (ev.kind == Event::Submit) {
            uint32_t got = engine.submit(ev.order);
            uint32_t want = ref.submit(ev.order);
            if (got != want)
                return fail(i, "submit returned " + std::to_string(got) + ", reference " +
                                   std::to_string(want));
            if (ev.order.type == OrderType::Limit && want > 0) candidates.push_back(ev.order.id);
        } else {
            bool got = engine.cancel(ev.id);
            bool want = ref.cancel(ev.id);
            if (got != want)
                return fail(i, std::string("cancel returned ") + (got ? "true" : "false") +
                                   ", reference " + (want ? "true" : "false"));
        }

        // Trades produced by this event, in order.
        const auto& et = engine.GetTrades();
        const auto& rt = ref.trades();
        if (et.size() != rt.size())
            return fail(i, "trade count " + std::to_string(et.size()) + ", reference " +
                               std::to_string(rt.size()));
        for (size_t t = trades_before; t < rt.size(); t++)
            if (!same(et[t], rt[t]))
                return fail(i, "trade " + std::to_string(t) + " is " + describe(et[t]) +
                                   ", reference " + describe(rt[t]));

        // Full depth on both sides.
        const Book& book = engine.GetBook();
        const size_t all = std::numeric_limits<size_t>::max();
        for (Side s : {Side::BUY, Side::SELL}) {
            auto got = book.Depth(s, all);
            auto want = ref.depth(s);
            if (got != want) {
                std::ostringstream m;
                m << (s == Side::BUY ? "bid" : "ask") << " depth differs:\n    engine:   ";
                for (auto& [p, q] : got) m << p << "x" << q << " ";
                m << "\n    reference: ";
                for (auto& [p, q] : want) m << p << "x" << q << " ";
                return fail(i, m.str());
            }
        }

        // Book must never be left crossed.
        auto bb = book.Best_Bid();
        auto ba = book.Best_Ask();
        if (bb && ba && *bb >= *ba)
            return fail(
                i, "crossed book: bid " + std::to_string(*bb) + " >= ask " + std::to_string(*ba));

        if (ref.liveCount() > res.max_live) res.max_live = ref.liveCount();
    }

    res.events_run = cfg.events;
    res.trades = ref.trades().size();
    return res;
}

}  // namespace fuzz
