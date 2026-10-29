#include "poolbook.hpp"

#include <gtest/gtest.h>

#include <random>

#include "book.hpp"

namespace {
using Levels = std::vector<std::pair<int64_t, uint64_t>>;
}

TEST(PoolBook, AddCancelLikeBook) {
    PoolBook b(8);
    EXPECT_TRUE(b.Add({1, 100, 50, Side::BUY}));
    EXPECT_TRUE(b.Add({2, 30, 50, Side::BUY}));
    EXPECT_TRUE(b.Add({3, 10, 52, Side::SELL}));
    EXPECT_EQ(b.Best_Bid(), 50);
    EXPECT_EQ(b.Best_Ask(), 52);
    EXPECT_EQ(b.Depth(Side::BUY, 5), (Levels{{50, 130}}));
    EXPECT_TRUE(b.Cancel(1));
    EXPECT_FALSE(b.Cancel(1));
    EXPECT_EQ(b.Front(Side::BUY)->id, 2u);
    EXPECT_EQ(b.Size(), 2u);
}

TEST(PoolBook, FullPoolRejectsAdd) {
    PoolBook b(2);
    EXPECT_TRUE(b.Add({1, 10, 50, Side::BUY}));
    EXPECT_TRUE(b.Add({2, 10, 51, Side::BUY}));
    EXPECT_FALSE(b.Add({3, 10, 52, Side::BUY}));
    EXPECT_EQ(b.Find(3), nullptr);
    EXPECT_EQ(b.Depth(Side::BUY, 5), (Levels{{51, 10}, {50, 10}}));
}

TEST(PoolBook, DuplicateDoesNotUseASlot) {
    PoolBook b(2);
    EXPECT_TRUE(b.Add({1, 10, 50, Side::BUY}));
    EXPECT_FALSE(b.Add({1, 10, 50, Side::BUY}));
    EXPECT_TRUE(b.Add({2, 10, 50, Side::BUY}));  // the duplicate did not eat slot 2
}

TEST(PoolBook, CancelledSlotsAreReused) {
    const size_t n = 1000;
    PoolBook b(n);
    for (int round = 0; round < 3; round++) {
        for (uint64_t i = 0; i < n; i++)
            ASSERT_TRUE(b.Add({round * n + i, 10, int64_t(100 + i % 7), Side::SELL}));
        EXPECT_FALSE(b.Add({999999, 10, 100, Side::SELL}));
        for (uint64_t i = 0; i < n; i++) ASSERT_TRUE(b.Cancel(round * n + i));
        EXPECT_EQ(b.Size(), 0u);
        EXPECT_FALSE(b.Best_Ask().has_value());
    }
    EXPECT_EQ(b.Capacity(), n);
}

TEST(PoolBook, FullFillFreesSlotPartialKeepsIt) {
    PoolBook b(1);
    ASSERT_TRUE(b.Add({1, 100, 50, Side::BUY}));
    b.Fill(b.Find(1), 40);
    EXPECT_EQ(b.Find(1)->qty, 60u);
    EXPECT_FALSE(b.Add({2, 10, 49, Side::BUY}));  // still occupied
    b.Fill(b.Find(1), 60);
    EXPECT_EQ(b.Find(1), nullptr);
    EXPECT_TRUE(b.Add({2, 10, 49, Side::BUY}));  // slot came back
    EXPECT_EQ(b.Depth(Side::BUY, 5), (Levels{{49, 10}}));
}

TEST(PoolBook, PointersStayValidAcrossOtherAddsAndCancels) {
    PoolBook b(100);
    ASSERT_TRUE(b.Add({1, 10, 50, Side::BUY}));
    Order* p = b.Find(1);
    for (uint64_t i = 2; i < 100; i++) ASSERT_TRUE(b.Add({i, 5, int64_t(40 + i % 5), Side::BUY}));
    for (uint64_t i = 2; i < 100; i += 2) ASSERT_TRUE(b.Cancel(i));
    EXPECT_EQ(b.Find(1), p);
    EXPECT_EQ(p->id, 1u);
    EXPECT_EQ(p->qty, 10u);
}

// Same random add/cancel/fill stream into Book and PoolBook; they must agree throughout.
TEST(PoolBook, MatchesBookOnRandomStream) {
    const size_t cap = 300;
    Book ref;
    PoolBook b(cap);
    std::mt19937_64 rng(7);
    std::vector<uint64_t> live;
    uint64_t next_id = 1;
    for (int step = 0; step < 200000; step++) {
        int op = rng() % 10;
        if (op < 5 || live.empty()) {
            Order o{next_id++, uint32_t(1 + rng() % 50), int64_t(90 + rng() % 20),
                    rng() % 2 ? Side::BUY : Side::SELL};
            bool full = b.Size() == cap;
            bool ok = b.Add(o);
            ASSERT_EQ(ok, !full);
            if (ok) {
                ASSERT_TRUE(ref.Add(o));
                live.push_back(o.id);
            }
        } else {
            size_t k = rng() % live.size();
            uint64_t id = live[k];
            if (op < 8) {
                ASSERT_TRUE(b.Cancel(id));
                ASSERT_TRUE(ref.Cancel(id));
                live[k] = live.back();
                live.pop_back();
            } else {
                uint32_t q = uint32_t(1 + rng() % ref.Find(id)->qty);
                bool gone = q == ref.Find(id)->qty;
                b.Fill(b.Find(id), q);
                ref.Fill(ref.Find(id), q);
                if (gone) {
                    live[k] = live.back();
                    live.pop_back();
                }
            }
        }
        ASSERT_EQ(b.Depth(Side::BUY, 100), ref.Depth(Side::BUY, 100)) << "step " << step;
        ASSERT_EQ(b.Depth(Side::SELL, 100), ref.Depth(Side::SELL, 100)) << "step " << step;
        const Order *fb = b.Front(Side::BUY), *rb = ref.Front(Side::BUY);
        ASSERT_EQ(fb ? fb->id : 0, rb ? rb->id : 0) << "step " << step;
    }
}

TEST(PoolBook, SharedPoolAcrossBooks) {
    OrderPool pool(3);
    {
        PoolBook a(pool), b(pool);
        EXPECT_TRUE(a.Add({1, 10, 50, Side::BUY}));
        EXPECT_TRUE(b.Add({1, 10, 60, Side::SELL}));  // ids are per book
        EXPECT_TRUE(b.Add({2, 10, 61, Side::SELL}));
        EXPECT_FALSE(a.Add({3, 10, 49, Side::BUY}));  // pool exhausted by both books
        EXPECT_EQ(pool.Live(), 3u);
        EXPECT_TRUE(b.Cancel(2));
        EXPECT_TRUE(a.Add({3, 10, 49, Side::BUY}));  // b's freed slot goes to a
        EXPECT_EQ(a.Depth(Side::BUY, 5), (Levels{{50, 10}, {49, 10}}));
    }
    EXPECT_EQ(pool.Live(), 0u);  // destroyed books return their resting orders
    PoolBook c(pool);
    for (uint64_t i = 0; i < 3; i++) EXPECT_TRUE(c.Add({i, 1, 50, Side::BUY}));
}
