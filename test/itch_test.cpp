#include <gtest/gtest.h>

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

#include "itch_parser.hpp"
#include "itch_reader.hpp"
#include "itch_writer.hpp"

namespace {

struct Recorder : itch::Handler {
    std::vector<itch::SystemEvent> sys;
    std::vector<itch::StockDirectory> dir;
    std::vector<itch::AddOrder> adds;
    std::vector<itch::Executed> execs;
    std::vector<itch::ExecutedWithPrice> cexecs;
    std::vector<itch::Cancel> cancels;
    std::vector<itch::Delete> deletes;
    std::vector<itch::Replace> replaces;
    std::vector<char> other;
    int bad = 0;

    void onSystemEvent(const itch::SystemEvent& m) { sys.push_back(m); }
    void onStockDirectory(const itch::StockDirectory& m) { dir.push_back(m); }
    void onAdd(const itch::AddOrder& m) { adds.push_back(m); }
    void onExecuted(const itch::Executed& m) { execs.push_back(m); }
    void onExecutedWithPrice(const itch::ExecutedWithPrice& m) { cexecs.push_back(m); }
    void onCancel(const itch::Cancel& m) { cancels.push_back(m); }
    void onDelete(const itch::Delete& m) { deletes.push_back(m); }
    void onReplace(const itch::Replace& m) { replaces.push_back(m); }
    void onOther(char t, const uint8_t*, size_t) { other.push_back(t); }
    void onBadLength(char, size_t, size_t) { bad++; }
};

size_t parseAll(const std::vector<uint8_t>& buf, Recorder& r, bool& bad) {
    return itch::parse(buf.data(), buf.size(), r, bad);
}

}  // namespace

TEST(Itch, AddOrderFields) {
    Writer w;
    w.add(123456789012ull, 'B', 300, "AAPL", 1567800, nullptr, 42, 34200000000123ull);
    Recorder r;
    bool bad = false;
    EXPECT_EQ(parseAll(w.out, r, bad), w.out.size());
    EXPECT_FALSE(bad);
    ASSERT_EQ(r.adds.size(), 1u);
    const itch::AddOrder& a = r.adds[0];
    EXPECT_EQ(a.h.type, 'A');
    EXPECT_EQ(a.h.locate, 42);
    EXPECT_EQ(a.h.timestamp, 34200000000123ull);  // 09:30:00.000000123
    EXPECT_EQ(a.ref, 123456789012ull);
    EXPECT_EQ(a.side, Side::BUY);
    EXPECT_EQ(a.shares, 300u);
    EXPECT_EQ(a.stock.view(), "AAPL");
    EXPECT_EQ(a.price, 1567800);  // $156.78
    EXPECT_FALSE(a.has_mpid);
}

TEST(Itch, AddWithMpid) {
    Writer w;
    w.add(7, 'S', 100, "MSFT", 1000000, "GSCO");
    Recorder r;
    bool bad = false;
    parseAll(w.out, r, bad);
    ASSERT_EQ(r.adds.size(), 1u);
    EXPECT_EQ(r.adds[0].h.type, 'F');
    EXPECT_EQ(r.adds[0].side, Side::SELL);
    EXPECT_TRUE(r.adds[0].has_mpid);
    EXPECT_EQ(std::string(r.adds[0].mpid, 4), "GSCO");
    EXPECT_EQ(r.adds[0].stock.view(), "MSFT");
}

TEST(Itch, ExecuteCancelDeleteReplace) {
    Writer w;
    w.executed(11, 50, 900001);
    w.executedWithPrice(12, 60, 900002, 'Y', 2500100);
    w.executedWithPrice(13, 70, 900003, 'N', 2500200);
    w.cancel(14, 25);
    w.del(15);
    w.replace(16, 17, 400, 999900);
    Recorder r;
    bool bad = false;
    EXPECT_EQ(parseAll(w.out, r, bad), w.out.size());
    EXPECT_FALSE(bad);

    ASSERT_EQ(r.execs.size(), 1u);
    EXPECT_EQ(r.execs[0].ref, 11u);
    EXPECT_EQ(r.execs[0].shares, 50u);
    EXPECT_EQ(r.execs[0].match, 900001u);

    ASSERT_EQ(r.cexecs.size(), 2u);
    EXPECT_EQ(r.cexecs[0].ref, 12u);
    EXPECT_EQ(r.cexecs[0].shares, 60u);
    EXPECT_EQ(r.cexecs[0].match, 900002u);
    EXPECT_TRUE(r.cexecs[0].printable);
    EXPECT_EQ(r.cexecs[0].price, 2500100);
    EXPECT_FALSE(r.cexecs[1].printable);

    ASSERT_EQ(r.cancels.size(), 1u);
    EXPECT_EQ(r.cancels[0].ref, 14u);
    EXPECT_EQ(r.cancels[0].shares, 25u);

    ASSERT_EQ(r.deletes.size(), 1u);
    EXPECT_EQ(r.deletes[0].ref, 15u);

    ASSERT_EQ(r.replaces.size(), 1u);
    EXPECT_EQ(r.replaces[0].old_ref, 16u);
    EXPECT_EQ(r.replaces[0].new_ref, 17u);
    EXPECT_EQ(r.replaces[0].shares, 400u);
    EXPECT_EQ(r.replaces[0].price, 999900);
}

TEST(Itch, DirectoryAndSystemEvent) {
    Writer w;
    w.system('O');
    w.directory(42, "AAPL");
    w.directory(43, "BRK A");
    Recorder r;
    bool bad = false;
    parseAll(w.out, r, bad);
    EXPECT_FALSE(bad);
    ASSERT_EQ(r.sys.size(), 1u);
    EXPECT_EQ(r.sys[0].code, 'O');
    ASSERT_EQ(r.dir.size(), 2u);
    EXPECT_EQ(r.dir[0].h.locate, 42);
    EXPECT_EQ(r.dir[0].stock.view(), "AAPL");
    EXPECT_EQ(r.dir[0].round_lot_size, 100u);
    EXPECT_EQ(r.dir[1].stock.view(), "BRK A");  // inner space kept, padding trimmed
}

TEST(Itch, MaxValuesDoNotTruncate) {
    Writer w;
    w.add(0xFFFFFFFFFFFFFFFFull, 'B', 0xFFFFFFFFu, "ZZZZZZZZ", 0xFFFFFFFFu, nullptr, 0xFFFF,
          0xFFFFFFFFFFFFull);
    Recorder r;
    bool bad = false;
    parseAll(w.out, r, bad);
    ASSERT_EQ(r.adds.size(), 1u);
    EXPECT_EQ(r.adds[0].ref, 0xFFFFFFFFFFFFFFFFull);
    EXPECT_EQ(r.adds[0].shares, 0xFFFFFFFFu);
    EXPECT_EQ(r.adds[0].price, int64_t(0xFFFFFFFFu));  // stays positive in int64
    EXPECT_EQ(r.adds[0].h.locate, 0xFFFF);
    EXPECT_EQ(r.adds[0].h.timestamp, 0xFFFFFFFFFFFFull);
    EXPECT_EQ(r.adds[0].stock.view(), "ZZZZZZZZ");
}

TEST(Itch, SkipsOtherTypes) {
    Writer w;
    w.raw('P', 44);  // trade (non-cross), not decoded
    w.raw('I', 50);  // NOII
    w.add(1, 'B', 10, "AAPL", 100);
    Recorder r;
    bool bad = false;
    EXPECT_EQ(parseAll(w.out, r, bad), w.out.size());
    EXPECT_FALSE(bad);
    EXPECT_EQ(r.other, (std::vector<char>{'P', 'I'}));
    EXPECT_EQ(r.adds.size(), 1u);
}

TEST(Itch, WrongLengthIsReported) {
    Writer w;
    w.raw('A', 30);  // 'A' must be 36
    w.add(1, 'B', 10, "AAPL", 100);
    Recorder r;
    bool bad = false;
    parseAll(w.out, r, bad);
    EXPECT_TRUE(bad);
    EXPECT_EQ(r.bad, 1);
    EXPECT_EQ(r.adds.size(), 1u);  // framing still lets the next message through
}

TEST(Itch, PartialMessageLeftForNextChunk) {
    Writer w;
    w.add(1, 'B', 10, "AAPL", 100);
    w.add(2, 'S', 20, "AAPL", 200);
    size_t first = 2 + 36;
    for (size_t cut = 0; cut <= w.out.size(); cut++) {
        Recorder r;
        bool bad = false;
        size_t used = itch::parse(w.out.data(), cut, r, bad);
        size_t want = cut >= w.out.size() ? w.out.size() : (cut >= first ? first : 0);
        EXPECT_EQ(used, want) << "cut at " << cut;
        EXPECT_EQ(r.adds.size(), want / first) << "cut at " << cut;
    }
}

TEST(Itch, ReaderAcrossTinyChunks) {
    Writer w;
    w.directory(1, "AAPL");
    for (uint64_t i = 1; i <= 500; i++) w.add(i, i % 2 ? 'B' : 'S', uint32_t(i), "AAPL", 1000 + i);
    w.del(250);

    std::string path = ::testing::TempDir() + "itch_reader_test.bin";
    std::FILE* f = std::fopen(path.c_str(), "wb");
    ASSERT_NE(f, nullptr);
    std::fwrite(w.out.data(), 1, w.out.size(), f);
    std::fclose(f);

    // A 7-byte chunk splits almost every message across reads.
    Recorder r;
    itch::ReadStats st = itch::readFile(path, r, [] { return true; }, 7);
    std::remove(path.c_str());

    EXPECT_FALSE(st.bad_length);
    EXPECT_FALSE(st.truncated);
    EXPECT_EQ(st.bytes, w.out.size());
    ASSERT_EQ(r.adds.size(), 500u);
    EXPECT_EQ(r.adds[499].ref, 500u);
    EXPECT_EQ(r.adds[499].price, 1500);
    ASSERT_EQ(r.deletes.size(), 1u);
    EXPECT_EQ(r.deletes[0].ref, 250u);
}

TEST(Itch, ReaderDetectsTruncatedFile) {
    Writer w;
    w.add(1, 'B', 10, "AAPL", 100);
    std::string path = ::testing::TempDir() + "itch_truncated_test.bin";
    std::FILE* f = std::fopen(path.c_str(), "wb");
    ASSERT_NE(f, nullptr);
    std::fwrite(w.out.data(), 1, w.out.size() - 5, f);
    std::fclose(f);

    Recorder r;
    itch::ReadStats st = itch::readFile(path, r);
    std::remove(path.c_str());
    EXPECT_TRUE(st.truncated);
    EXPECT_TRUE(r.adds.empty());
}
