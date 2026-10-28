#include <gtest/gtest.h>

#include "fuzz_harness.hpp"

// Short runs for CI. Long runs: ./build/release/fuzz --events 10000000 --seed N

TEST(Fuzz, ManySeeds) {
    for (uint64_t seed = 1; seed <= 20; seed++) {
        fuzz::Config cfg;
        cfg.seed = seed;
        cfg.events = 20000;
        fuzz::Result r = fuzz::run(cfg);
        ASSERT_TRUE(r.ok) << r.error;
        EXPECT_GT(r.trades, 0u);
    }
}

TEST(Fuzz, NegativePricesAroundZero) {
    fuzz::Config cfg;
    cfg.seed = 7;
    cfg.events = 50000;
    cfg.start_mid = 0;
    fuzz::Result r = fuzz::run(cfg);
    ASSERT_TRUE(r.ok) << r.error;
}

TEST(Fuzz, MarketHeavy) {
    fuzz::Config cfg;
    cfg.seed = 11;
    cfg.events = 50000;
    cfg.market_pct = 30;
    cfg.limit_pct = 45;
    fuzz::Result r = fuzz::run(cfg);
    ASSERT_TRUE(r.ok) << r.error;
}

TEST(Fuzz, DeepBookFewCancels) {
    fuzz::Config cfg;
    cfg.seed = 13;
    cfg.events = 50000;
    cfg.limit_pct = 85;
    cfg.market_pct = 2;
    cfg.spread = 40;
    fuzz::Result r = fuzz::run(cfg);
    ASSERT_TRUE(r.ok) << r.error;
}

TEST(Fuzz, LargeQuantities) {
    fuzz::Config cfg;
    cfg.seed = 17;
    cfg.events = 20000;
    cfg.max_qty = 4000000000u;  // near uint32 max: level totals must not wrap
    fuzz::Result r = fuzz::run(cfg);
    ASSERT_TRUE(r.ok) << r.error;
}
