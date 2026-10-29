#include "itch_replay.hpp"

#include <gtest/gtest.h>

#include "itch_reader.hpp"
#include "itch_writer.hpp"

namespace {

using Levels = std::vector<std::pair<int64_t, uint64_t>>;

itch::Replayer replay(const Writer& w) {
    itch::Replayer r;
    bool bad = false;
    itch::parse(w.out.data(), w.out.size(), r, bad);
    EXPECT_FALSE(bad);
    return r;
}

}  // namespace

TEST(Replay, AddBuildsPerStockBooks) {
    Writer w;
    w.directory(1, "AAPL");
    w.directory(2, "MSFT");
    w.add(10, 'B', 100, "AAPL", 1500000, nullptr, 1);
    w.add(11, 'S', 200, "AAPL", 1500100, nullptr, 1);
    w.add(12, 'B', 300, "MSFT", 1000000, "GSCO", 2);
    itch::Replayer r = replay(w);

    EXPECT_EQ(r.errors().total(), 0u);
    EXPECT_EQ(r.counts().adds, 3u);
    EXPECT_EQ(r.counts().resting, 3u);
    ASSERT_NE(r.bookFor("AAPL"), nullptr);
    EXPECT_EQ(r.bookFor("AAPL")->Best_Bid(), 1500000);
    EXPECT_EQ(r.bookFor("AAPL")->Best_Ask(), 1500100);
    EXPECT_EQ(r.bookFor("MSFT")->Depth(Side::BUY, 5), (Levels{{1000000, 300}}));
    EXPECT_FALSE(r.bookFor("MSFT")->Best_Ask().has_value());
    EXPECT_EQ(r.symbol(2), "MSFT");
    EXPECT_EQ(r.bookFor("TSLA"), nullptr);
}

TEST(Replay, ExecutePartialThenFull) {
    Writer w;
    w.add(10, 'S', 100, "AAPL", 1500000);
    w.add(11, 'S', 50, "AAPL", 1500000);
    w.executed(10, 30, 1);
    itch::Replayer r = replay(w);
    EXPECT_EQ(r.bookFor(1)->Depth(Side::SELL, 1), (Levels{{1500000, 120}}));
    EXPECT_EQ(r.bookFor(1)->Front(Side::SELL)->id, 10u);  // partial keeps its place

    Writer w2 = w;
    w2.executedWithPrice(10, 70, 2, 'Y', 1499000);  // print price differs; book uses own
    itch::Replayer r2 = replay(w2);
    EXPECT_EQ(r2.errors().total(), 0u);
    EXPECT_EQ(r2.counts().executes, 2u);
    EXPECT_EQ(r2.counts().executed_shares, 100u);
    EXPECT_EQ(r2.counts().resting, 1u);
    EXPECT_EQ(r2.bookFor(1)->Front(Side::SELL)->id, 11u);
    EXPECT_EQ(r2.bookFor(1)->Depth(Side::SELL, 5), (Levels{{1500000, 50}}));
}

TEST(Replay, PartialCancelKeepsPlace) {
    Writer w;
    w.add(10, 'B', 100, "AAPL", 1500000);
    w.add(11, 'B', 100, "AAPL", 1500000);
    w.cancel(10, 40);
    itch::Replayer r = replay(w);
    EXPECT_EQ(r.errors().total(), 0u);
    EXPECT_EQ(r.counts().partial_cancels, 1u);
    EXPECT_EQ(r.bookFor(1)->Front(Side::BUY)->id, 10u);
    EXPECT_EQ(r.bookFor(1)->Front(Side::BUY)->qty, 60u);
    EXPECT_EQ(r.bookFor(1)->Depth(Side::BUY, 1), (Levels{{1500000, 160}}));
    EXPECT_EQ(r.counts().resting, 2u);
}

TEST(Replay, PartialCancelOfEverythingRemovesOrder) {
    Writer w;
    w.add(10, 'B', 100, "AAPL", 1500000);
    w.cancel(10, 100);
    itch::Replayer r = replay(w);
    EXPECT_EQ(r.errors().total(), 0u);
    EXPECT_FALSE(r.bookFor(1)->Best_Bid().has_value());
    EXPECT_EQ(r.counts().resting, 0u);
}

TEST(Replay, DeleteRemovesOrderAndLevel) {
    Writer w;
    w.add(10, 'B', 100, "AAPL", 1500000);
    w.add(11, 'B', 100, "AAPL", 1490000);
    w.del(10);
    itch::Replayer r = replay(w);
    EXPECT_EQ(r.errors().total(), 0u);
    EXPECT_EQ(r.bookFor(1)->Best_Bid(), 1490000);
    EXPECT_EQ(r.counts().resting, 1u);
}

TEST(Replay, ReplaceKeepsSideMovesToBack) {
    Writer w;
    w.add(10, 'S', 100, "AAPL", 1500000);
    w.add(11, 'S', 100, "AAPL", 1500000);
    w.replace(10, 20, 250, 1500000);  // same price, new size: loses priority
    itch::Replayer r = replay(w);
    EXPECT_EQ(r.errors().total(), 0u);
    EXPECT_EQ(r.counts().replaces, 1u);
    EXPECT_EQ(r.counts().resting, 2u);
    const Book* b = r.bookFor(1);
    EXPECT_EQ(b->Find(10), nullptr);
    ASSERT_NE(b->Find(20), nullptr);
    EXPECT_EQ(b->Find(20)->side, Side::SELL);  // side came from the old order
    EXPECT_EQ(b->Front(Side::SELL)->id, 11u);
    EXPECT_EQ(b->Depth(Side::SELL, 1), (Levels{{1500000, 350}}));
}

TEST(Replay, ReplaceToNewPrice) {
    Writer w;
    w.add(10, 'B', 100, "AAPL", 1500000);
    w.replace(10, 20, 100, 1510000);
    itch::Replayer r = replay(w);
    EXPECT_EQ(r.bookFor(1)->Depth(Side::BUY, 5), (Levels{{1510000, 100}}));
}

TEST(Replay, ErrorsAreCountedNotApplied) {
    Writer w;
    w.add(10, 'B', 100, "AAPL", 1500000);
    w.executed(99, 10, 1);               // unknown ref
    w.cancel(98, 10);                    // unknown ref
    w.del(97);                           // unknown ref
    w.replace(96, 30, 10, 1500000);      // unknown ref
    w.executed(10, 150, 2);              // overfill
    w.add(10, 'S', 5, "AAPL", 1600000);  // duplicate ref
    w.add(11, 'B', 0, "AAPL", 1500000);  // zero shares
    itch::Replayer r = replay(w);
    EXPECT_EQ(r.errors().unknown_ref, 4u);
    EXPECT_EQ(r.errors().overfill, 1u);
    EXPECT_EQ(r.errors().duplicate_add, 1u);
    EXPECT_EQ(r.errors().zero_shares, 1u);
    EXPECT_EQ(r.bookFor(1)->Depth(Side::BUY, 5), (Levels{{1500000, 100}}));  // untouched
    EXPECT_FALSE(r.bookFor(1)->Best_Ask().has_value());
}

TEST(Replay, SameRefOnDifferentStocksIsIndependent) {
    // Refs are unique per day in real data; books are still keyed by locate.
    Writer w;
    w.add(10, 'B', 100, "AAPL", 1500000, nullptr, 1);
    w.add(11, 'B', 200, "MSFT", 1000000, nullptr, 2);
    w.del(11, 2);
    itch::Replayer r = replay(w);
    EXPECT_EQ(r.errors().total(), 0u);
    EXPECT_EQ(r.bookFor(1)->Best_Bid(), 1500000);
    EXPECT_FALSE(r.bookFor(2)->Best_Bid().has_value());
}

TEST(Replay, FromFile) {
    Writer w;
    w.directory(1, "AAPL");
    for (uint64_t i = 1; i <= 200; i++) w.add(i, 'B', 10, "AAPL", 1000000 + i * 100);
    for (uint64_t i = 1; i <= 200; i += 2) w.del(i);
    std::string path = writeTemp(w.out, "itch_replay_test.bin");
    itch::Replayer r;
    itch::ReadStats st = itch::readFile(path, r, [] { return true; }, 13);
    std::remove(path.c_str());
    EXPECT_FALSE(st.bad_length);
    EXPECT_EQ(r.errors().total(), 0u);
    EXPECT_EQ(r.counts().resting, 100u);
    EXPECT_EQ(r.bookFor("AAPL")->Best_Bid(), 1000000 + 200 * 100);
}
