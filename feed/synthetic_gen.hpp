#pragma once
// Seeded random event stream (limit / market / cancel) shared by the fuzzer and the
// benchmarks. Same seed + same config => same events.

#include <cstdint>
#include <random>
#include <vector>

#include "engine.hpp"

namespace synth {

struct Config {
    uint64_t seed = 1;
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

class Generator {
public:
    explicit Generator(const Config& cfg) : cfg(cfg), rng(cfg.seed), mid(cfg.start_mid) {}

    Event next() {
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
        return ev;
    }

    // Tell the generator an order rested, so later cancels can target it.
    void rested(uint64_t id) { candidates.push_back(id); }

private:
    Config cfg;
    std::mt19937_64 rng;
    int64_t mid;
    uint64_t next_id = 1;              // ids are never reused
    std::vector<uint64_t> candidates;  // ids that may still be resting

    int64_t roll(int64_t lo, int64_t hi) {
        return std::uniform_int_distribution<int64_t>(lo, hi)(rng);
    }
};

}  // namespace synth
