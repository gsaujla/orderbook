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
