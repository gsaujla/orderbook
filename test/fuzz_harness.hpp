#pragma once
// Drives Engine and ReferenceEngine with the same seeded random events and checks
// after every event that they agree. Shared by the gtest fuzz tests and the
// standalone `fuzz` binary used for long runs.

#include <cstdint>
#include <cstdio>
#include <deque>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

#include "engine.hpp"
#include "reference_book.hpp"
#include "synthetic_gen.hpp"

namespace fuzz {

struct Config : synth::Config {
    uint64_t events = 100000;
};

using synth::Event;

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
    synth::Generator gen(cfg);
    Engine engine;
    ReferenceEngine ref;
    Result res;

    std::deque<Event> recent;  // last few events, for the failure report

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
        Event ev = gen.next();

        recent.push_back(ev);
        if (recent.size() > 12) recent.pop_front();

        size_t trades_before = ref.trades().size();
        if (ev.kind == Event::Submit) {
            uint32_t got = engine.submit(ev.order);
            uint32_t want = ref.submit(ev.order);
            if (got != want)
                return fail(i, "submit returned " + std::to_string(got) + ", reference " +
                                   std::to_string(want));
            if (ev.order.type == OrderType::Limit && want > 0) gen.rested(ev.order.id);
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
