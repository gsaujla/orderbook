#include "engine.hpp"

#include <gtest/gtest.h>

namespace {

using Levels = std::vector<std::pair<int64_t, uint64_t>>;

NewOrder limit(uint64_t id, Side side, int64_t price, uint32_t qty) {
    return NewOrder{id, qty, price, side, OrderType::Limit};
}

NewOrder market(uint64_t id, Side side, uint32_t qty) {
    return NewOrder{id, qty, 0, side, OrderType::Market};
}

void ExpectTrade(const Trade& t, uint64_t buyer, uint64_t seller, int64_t price, uint32_t qty) {
    EXPECT_EQ(t.buyer_id, buyer);
    EXPECT_EQ(t.seller_id, seller);
    EXPECT_EQ(t.price, price);
    EXPECT_EQ(t.qty, qty);
}

}  // namespace

TEST(Limit, NoCrossRests) {
    Engine e;
    e.submit(limit(1, Side::SELL, 103, 50));
    EXPECT_EQ(e.submit(limit(2, Side::BUY, 102, 10)), 10u);
    EXPECT_TRUE(e.GetTrades().empty());
    EXPECT_EQ(e.GetBook().Best_Bid(), 102);
    EXPECT_EQ(e.GetBook().Best_Ask(), 103);
}

TEST(Limit, IntoEmptyBookRests) {
    Engine e;
    EXPECT_EQ(e.submit(limit(1, Side::BUY, 100, 10)), 10u);
    EXPECT_TRUE(e.GetTrades().empty());
    EXPECT_EQ(e.GetBook().Depth(Side::BUY, 5), (Levels{{100, 10}}));
}

TEST(Limit, ExactFillTradesAtRestingPrice) {
    Engine e;
    e.submit(limit(1, Side::SELL, 101, 10));
    EXPECT_EQ(e.submit(limit(2, Side::BUY, 105, 10)), 0u);
    ASSERT_EQ(e.GetTrades().size(), 1u);
    ExpectTrade(e.GetTrades()[0], 2, 1, 101, 10);
    EXPECT_FALSE(e.GetBook().Best_Ask().has_value());
    EXPECT_FALSE(e.GetBook().Best_Bid().has_value());
}

TEST(Limit, SellCrossesHigherBidAtBidPrice) {
    Engine e;
    e.submit(limit(1, Side::BUY, 105, 10));
    EXPECT_EQ(e.submit(limit(2, Side::SELL, 100, 10)), 0u);
    ASSERT_EQ(e.GetTrades().size(), 1u);
    ExpectTrade(e.GetTrades()[0], 1, 2, 105, 10);
}

TEST(Limit, EqualPriceCrosses) {
    Engine e;
    e.submit(limit(1, Side::SELL, 101, 10));
    e.submit(limit(2, Side::BUY, 101, 10));
    ASSERT_EQ(e.GetTrades().size(), 1u);
    ExpectTrade(e.GetTrades()[0], 2, 1, 101, 10);
}

// Trades 30 @ 101, stops before 103, rests 70 @ 102.
TEST(Limit, PartialFillRestsRemainderAtLimit) {
    Engine e;
    e.submit(limit(1, Side::SELL, 101, 30));
    e.submit(limit(2, Side::SELL, 103, 50));
    EXPECT_EQ(e.submit(limit(3, Side::BUY, 102, 100)), 70u);
    ASSERT_EQ(e.GetTrades().size(), 1u);
    ExpectTrade(e.GetTrades()[0], 3, 1, 101, 30);
    EXPECT_EQ(e.GetBook().Depth(Side::BUY, 5), (Levels{{102, 70}}));
    EXPECT_EQ(e.GetBook().Depth(Side::SELL, 5), (Levels{{103, 50}}));
}

TEST(Limit, RestedRemainderCanBeCancelled) {
    Engine e;
    e.submit(limit(1, Side::SELL, 101, 30));
    e.submit(limit(2, Side::BUY, 102, 100));
    EXPECT_TRUE(e.cancel(2));
    EXPECT_FALSE(e.GetBook().Best_Bid().has_value());
}

TEST(Limit, SweepsLevelsInPriceOrder) {
    Engine e;
    e.submit(limit(1, Side::SELL, 103, 10));
    e.submit(limit(2, Side::SELL, 101, 10));
    e.submit(limit(3, Side::SELL, 102, 10));
    EXPECT_EQ(e.submit(limit(4, Side::BUY, 103, 25)), 0u);
    ASSERT_EQ(e.GetTrades().size(), 3u);
    ExpectTrade(e.GetTrades()[0], 4, 2, 101, 10);
    ExpectTrade(e.GetTrades()[1], 4, 3, 102, 10);
    ExpectTrade(e.GetTrades()[2], 4, 1, 103, 5);
    EXPECT_EQ(e.GetBook().Depth(Side::SELL, 5), (Levels{{103, 5}}));
}

TEST(Limit, TimePriorityWithinLevel) {
    Engine e;
    e.submit(limit(1, Side::BUY, 100, 10));
    e.submit(limit(2, Side::BUY, 100, 10));
    e.submit(limit(3, Side::BUY, 100, 10));
    e.submit(limit(4, Side::SELL, 100, 15));
    ASSERT_EQ(e.GetTrades().size(), 2u);
    ExpectTrade(e.GetTrades()[0], 1, 4, 100, 10);
    ExpectTrade(e.GetTrades()[1], 2, 4, 100, 5);
    EXPECT_EQ(e.GetBook().Front(Side::BUY)->id, 2u);
    EXPECT_EQ(e.GetBook().Depth(Side::BUY, 1), (Levels{{100, 15}}));
}

TEST(Limit, RestingPartialKeepsFront) {
    Engine e;
    e.submit(limit(1, Side::BUY, 100, 10));
    e.submit(limit(2, Side::BUY, 100, 10));
    e.submit(limit(3, Side::SELL, 100, 4));
    EXPECT_EQ(e.GetBook().Front(Side::BUY)->id, 1u);
    EXPECT_EQ(e.GetBook().Front(Side::BUY)->qty, 6u);
    e.submit(limit(4, Side::SELL, 100, 6));
    EXPECT_EQ(e.GetBook().Front(Side::BUY)->id, 2u);
}

TEST(Limit, NegativePrices) {
    Engine e;
    e.submit(limit(1, Side::SELL, -5, 10));
    EXPECT_EQ(e.submit(limit(2, Side::BUY, 10, 10)), 0u);
    ASSERT_EQ(e.GetTrades().size(), 1u);
    ExpectTrade(e.GetTrades()[0], 2, 1, -5, 10);
}

TEST(Market, IntoEmptyBookIsDropped) {
    Engine e;
    EXPECT_EQ(e.submit(market(1, Side::BUY, 10)), 10u);
    EXPECT_TRUE(e.GetTrades().empty());
    EXPECT_FALSE(e.GetBook().Best_Bid().has_value());
    EXPECT_FALSE(e.cancel(1));  // never rested
}

TEST(Market, ExactFill) {
    Engine e;
    e.submit(limit(1, Side::BUY, 100, 10));
    EXPECT_EQ(e.submit(market(2, Side::SELL, 10)), 0u);
    ASSERT_EQ(e.GetTrades().size(), 1u);
    ExpectTrade(e.GetTrades()[0], 1, 2, 100, 10);
    EXPECT_FALSE(e.GetBook().Best_Bid().has_value());
}

TEST(Market, SweepsAndDropsLeftover) {
    Engine e;
    e.submit(limit(1, Side::SELL, 101, 30));
    e.submit(limit(2, Side::SELL, 102, 20));
    EXPECT_EQ(e.submit(market(3, Side::BUY, 100)), 50u);
    ASSERT_EQ(e.GetTrades().size(), 2u);
    ExpectTrade(e.GetTrades()[0], 3, 1, 101, 30);
    ExpectTrade(e.GetTrades()[1], 3, 2, 102, 20);
    EXPECT_FALSE(e.GetBook().Best_Ask().has_value());
    EXPECT_FALSE(e.GetBook().Best_Bid().has_value());  // leftover did not rest
}

TEST(Market, PartialFillOfRestingOrder) {
    Engine e;
    e.submit(limit(1, Side::SELL, 101, 30));
    EXPECT_EQ(e.submit(market(2, Side::BUY, 12)), 0u);
    EXPECT_EQ(e.GetBook().Front(Side::SELL)->id, 1u);
    EXPECT_EQ(e.GetBook().Depth(Side::SELL, 1), (Levels{{101, 18}}));
}

TEST(Engine, CancelUnknownIdReturnsFalse) {
    Engine e;
    EXPECT_FALSE(e.cancel(99));
}

TEST(Engine, TradesAccumulateAcrossSubmits) {
    Engine e;
    e.submit(limit(1, Side::SELL, 101, 10));
    e.submit(limit(2, Side::SELL, 101, 10));
    e.submit(market(3, Side::BUY, 5));
    e.submit(market(4, Side::BUY, 10));
    ASSERT_EQ(e.GetTrades().size(), 3u);
    ExpectTrade(e.GetTrades()[0], 3, 1, 101, 5);
    ExpectTrade(e.GetTrades()[1], 4, 1, 101, 5);
    ExpectTrade(e.GetTrades()[2], 4, 2, 101, 5);
}
