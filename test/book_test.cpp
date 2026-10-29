#include "book.hpp"

#include <gtest/gtest.h>

namespace {

Order buy(uint64_t id, int64_t price, uint32_t qty = 10) {
    return Order{id, qty, price, Side::BUY};
}

Order sell(uint64_t id, int64_t price, uint32_t qty = 10) {
    return Order{id, qty, price, Side::SELL};
}

}  // namespace

TEST(Book, EmptyBookHasNoBestPrices) {
    Book book;
    EXPECT_FALSE(book.Best_Bid().has_value());
    EXPECT_FALSE(book.Best_Ask().has_value());
}

TEST(Book, BestPricesOneOrderEachSide) {
    Book book;
    book.Add(buy(1, 100));
    book.Add(sell(2, 101));
    EXPECT_EQ(book.Best_Bid(), 100);
    EXPECT_EQ(book.Best_Ask(), 101);
}

TEST(Book, BestBidIsHighest) {
    Book book;
    book.Add(buy(1, 98));
    book.Add(buy(2, 100));
    book.Add(buy(3, 99));
    EXPECT_EQ(book.Best_Bid(), 100);
    EXPECT_FALSE(book.Best_Ask().has_value());
}

TEST(Book, BestAskIsLowest) {
    Book book;
    book.Add(sell(1, 104));
    book.Add(sell(2, 101));
    book.Add(sell(3, 102));
    EXPECT_EQ(book.Best_Ask(), 101);
    EXPECT_FALSE(book.Best_Bid().has_value());
}

TEST(Book, NegativePrices) {
    Book book;
    book.Add(buy(1, -5));
    book.Add(buy(2, -2));
    book.Add(sell(3, -1));
    book.Add(sell(4, 3));
    EXPECT_EQ(book.Best_Bid(), -2);
    EXPECT_EQ(book.Best_Ask(), -1);
}

TEST(Book, CancelOnlyOrderRemovesLevel) {
    Book book;
    book.Add(buy(1, 100));
    book.Add(buy(2, 99));
    book.Cancel(1);
    EXPECT_EQ(book.Best_Bid(), 99);
    book.Cancel(2);
    EXPECT_FALSE(book.Best_Bid().has_value());
}

// Level stays alive until every order at that price is cancelled,
// whatever order the cancels arrive in.
TEST(Book, CancelMiddleFrontBack) {
    Book book;
    book.Add(sell(1, 101));
    book.Add(sell(2, 101));
    book.Add(sell(3, 101));
    book.Add(sell(4, 102));

    book.Cancel(2);  // middle
    EXPECT_EQ(book.Best_Ask(), 101);
    book.Cancel(1);  // front
    EXPECT_EQ(book.Best_Ask(), 101);
    book.Cancel(3);  // back, last at 101
    EXPECT_EQ(book.Best_Ask(), 102);
}

TEST(Book, CancelBackThenAddRelinks) {
    Book book;
    book.Add(buy(1, 100));
    book.Add(buy(2, 100));
    book.Cancel(2);  // back moves to order 1
    book.Add(buy(3, 100));
    book.Cancel(1);
    EXPECT_EQ(book.Best_Bid(), 100);  // order 3 still there
    book.Cancel(3);
    EXPECT_FALSE(book.Best_Bid().has_value());
}

TEST(Book, CancelUnknownIdIsNoop) {
    Book book;
    book.Add(buy(1, 100));
    book.Cancel(42);
    EXPECT_EQ(book.Best_Bid(), 100);
}

TEST(Book, CancelTwiceIsNoop) {
    Book book;
    book.Add(buy(1, 100));
    book.Add(buy(2, 100));
    book.Cancel(1);
    book.Cancel(1);
    EXPECT_EQ(book.Best_Bid(), 100);
    book.Cancel(2);
    EXPECT_FALSE(book.Best_Bid().has_value());
}

TEST(Book, DuplicateIdRejected) {
    Book book;
    book.Add(buy(1, 100));
    book.Add(buy(1, 105));  // same id, ignored
    EXPECT_EQ(book.Best_Bid(), 100);
    book.Cancel(1);
    EXPECT_FALSE(book.Best_Bid().has_value());
}

TEST(Book, IdReusableAfterCancel) {
    Book book;
    book.Add(buy(1, 100));
    book.Cancel(1);
    book.Add(sell(1, 101));
    EXPECT_EQ(book.Best_Ask(), 101);
    EXPECT_FALSE(book.Best_Bid().has_value());
}

// Many orders force ordermap to rehash; stored pointers must survive it.
TEST(Book, ManyOrdersSurviveRehash) {
    Book book;
    const uint64_t n = 10000;
    for (uint64_t i = 1; i <= n; i++) book.Add(buy(i, 100 + (int64_t)(i % 10)));
    EXPECT_EQ(book.Best_Bid(), 109);
    for (uint64_t i = 1; i <= n; i++)
        if (i % 10 == 9) book.Cancel(i);
    EXPECT_EQ(book.Best_Bid(), 108);
    for (uint64_t i = 1; i <= n; i++)
        if (i % 10 != 9) book.Cancel(i);
    EXPECT_FALSE(book.Best_Bid().has_value());
}

using Levels = std::vector<std::pair<int64_t, uint64_t>>;

TEST(Depth, EmptyBook) {
    Book book;
    EXPECT_TRUE(book.Depth(Side::BUY, 5).empty());
    EXPECT_TRUE(book.Depth(Side::SELL, 5).empty());
}

TEST(Depth, BestPriceFirstWithTotals) {
    Book book;
    book.Add(buy(1, 99, 10));
    book.Add(buy(2, 100, 20));
    book.Add(buy(3, 100, 5));
    book.Add(buy(4, 98, 7));
    book.Add(sell(5, 102, 3));
    book.Add(sell(6, 101, 4));
    EXPECT_EQ(book.Depth(Side::BUY, 10), (Levels{{100, 25}, {99, 10}, {98, 7}}));
    EXPECT_EQ(book.Depth(Side::SELL, 10), (Levels{{101, 4}, {102, 3}}));
}

TEST(Depth, LimitsToN) {
    Book book;
    book.Add(sell(1, 101));
    book.Add(sell(2, 102));
    book.Add(sell(3, 103));
    EXPECT_EQ(book.Depth(Side::SELL, 2), (Levels{{101, 10}, {102, 10}}));
    EXPECT_TRUE(book.Depth(Side::SELL, 0).empty());
}

TEST(Depth, CancelSubtractsFromTotal) {
    Book book;
    book.Add(buy(1, 100, 10));
    book.Add(buy(2, 100, 20));
    book.Cancel(1);
    EXPECT_EQ(book.Depth(Side::BUY, 1), (Levels{{100, 20}}));
}

TEST(Fill, PartialKeepsPlaceAndUpdatesTotal) {
    Book book;
    book.Add(buy(1, 100, 10));
    book.Add(buy(2, 100, 20));
    book.Fill(book.Front(Side::BUY), 4);
    EXPECT_EQ(book.Front(Side::BUY)->id, 1u);
    EXPECT_EQ(book.Front(Side::BUY)->qty, 6u);
    EXPECT_EQ(book.Depth(Side::BUY, 1), (Levels{{100, 26}}));
}

TEST(Fill, PartialOnAskSide) {
    Book book;
    book.Add(sell(1, 101, 30));
    book.Add(sell(2, 101, 40));
    book.Fill(book.Front(Side::SELL), 5);
    EXPECT_EQ(book.Front(Side::SELL)->id, 1u);
    EXPECT_EQ(book.Depth(Side::SELL, 1), (Levels{{101, 65}}));
}

TEST(Fill, FullRemovesOrderAndNextMovesUp) {
    Book book;
    book.Add(buy(1, 100, 10));
    book.Add(buy(2, 100, 20));
    book.Fill(book.Front(Side::BUY), 10);
    EXPECT_EQ(book.Front(Side::BUY)->id, 2u);
    EXPECT_EQ(book.Depth(Side::BUY, 1), (Levels{{100, 20}}));
    EXPECT_FALSE(book.Cancel(1));  // already gone
}

TEST(Fill, PartialThenFull) {
    Book book;
    book.Add(buy(1, 100, 10));
    book.Add(buy(2, 100, 20));
    book.Fill(book.Front(Side::BUY), 4);
    book.Fill(book.Front(Side::BUY), 6);
    EXPECT_EQ(book.Front(Side::BUY)->id, 2u);
    EXPECT_EQ(book.Depth(Side::BUY, 1), (Levels{{100, 20}}));
}

TEST(Fill, LastOrderRemovesLevel) {
    Book book;
    book.Add(sell(1, 101, 10));
    book.Add(sell(2, 102, 10));
    book.Fill(book.Front(Side::SELL), 10);
    EXPECT_EQ(book.Best_Ask(), 102);
    EXPECT_EQ(book.Depth(Side::SELL, 5), (Levels{{102, 10}}));
}

TEST(Fill, CancelAfterPartialRemovesRemainingQty) {
    Book book;
    book.Add(sell(1, 101, 30));
    book.Add(sell(2, 101, 40));
    book.Fill(book.Front(Side::SELL), 5);
    EXPECT_TRUE(book.Cancel(1));
    EXPECT_EQ(book.Depth(Side::SELL, 1), (Levels{{101, 40}}));
}

TEST(Front, EmptySideIsNull) {
    Book book;
    book.Add(buy(1, 100));
    EXPECT_EQ(book.Front(Side::SELL), nullptr);
    EXPECT_EQ(book.Front(Side::BUY)->id, 1u);
}

TEST(Front, FifoWithinLevel) {
    Book book;
    book.Add(sell(1, 101));
    book.Add(sell(2, 101));
    book.Add(sell(3, 100));  // better price, arrives later
    EXPECT_EQ(book.Front(Side::SELL)->id, 3u);
    book.Fill(book.Front(Side::SELL), 10);
    EXPECT_EQ(book.Front(Side::SELL)->id, 1u);
    book.Fill(book.Front(Side::SELL), 10);
    EXPECT_EQ(book.Front(Side::SELL)->id, 2u);
}

TEST(Find, MissingIdIsNull) {
    Book book;
    EXPECT_EQ(book.Find(1), nullptr);
    book.Add(buy(1, 100));
    EXPECT_EQ(book.Find(2), nullptr);
}

TEST(Find, ReturnsTheRestingOrder) {
    Book book;
    book.Add(sell(7, 101, 30));
    Order* o = book.Find(7);
    ASSERT_NE(o, nullptr);
    EXPECT_EQ(o->id, 7u);
    EXPECT_EQ(o->price, 101);
    EXPECT_EQ(o->qty, 30u);
    EXPECT_EQ(o->side, Side::SELL);
    EXPECT_EQ(o, book.Front(Side::SELL));  // same object, not a copy
}

TEST(Find, FillThroughFoundPointer) {
    Book book;
    book.Add(buy(1, 100, 10));
    book.Add(buy(2, 100, 20));
    book.Fill(book.Find(2), 5);  // fill an order that is not at the front
    EXPECT_EQ(book.Find(2)->qty, 15u);
    EXPECT_EQ(book.Front(Side::BUY)->id, 1u);
    EXPECT_EQ(book.Depth(Side::BUY, 1), (Levels{{100, 25}}));
    book.Fill(book.Find(2), 15);
    EXPECT_EQ(book.Find(2), nullptr);
    EXPECT_EQ(book.Depth(Side::BUY, 1), (Levels{{100, 10}}));
}

TEST(Find, GoneAfterCancel) {
    Book book;
    book.Add(buy(1, 100));
    book.Cancel(1);
    EXPECT_EQ(book.Find(1), nullptr);
}

TEST(Find, ConstOverload) {
    Book book;
    book.Add(buy(1, 100, 10));
    const Book& cb = book;
    const Order* o = cb.Find(1);
    ASSERT_NE(o, nullptr);
    EXPECT_EQ(o->qty, 10u);
    EXPECT_EQ(cb.Find(2), nullptr);
}
