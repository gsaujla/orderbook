// Parse an ITCH 5.0 file and print message counts, without touching the book.
// Used to check the parser against real data.
//
//   ./build/release/itch_stats data/01302019.NASDAQ_ITCH50.gz [--limit N] [--dump K]
//
// --limit N  stop after roughly N messages
// --dump K   print the first K decoded add/execute/cancel/delete/replace messages

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <map>
#include <string>
#include <unordered_map>

#include "itch_reader.hpp"

namespace {

struct Stats : itch::Handler {
    uint64_t total = 0;
    uint64_t by_type[256] = {};
    uint64_t bad = 0;
    uint64_t dump = 0;
    uint64_t last_ts = 0;
    uint64_t ts_backwards = 0;
    std::unordered_map<uint16_t, std::string> stocks;  // locate -> symbol
    uint64_t add_unknown_locate = 0;

    void count(const itch::Header& h) {
        total++;
        by_type[uint8_t(h.type)]++;
        if (h.timestamp < last_ts) ts_backwards++;
        last_ts = h.timestamp;
    }
    bool dumping() { return dump > 0 && dump--; }
    const char* sym(uint16_t locate) {
        auto it = stocks.find(locate);
        return it == stocks.end() ? "?" : it->second.c_str();
    }

    void onSystemEvent(const itch::SystemEvent& m) {
        count(m.h);
        std::printf("system event '%c' at %s\n", m.code, clock(m.h.timestamp).c_str());
    }
    void onStockDirectory(const itch::StockDirectory& m) {
        count(m.h);
        stocks[m.h.locate] = std::string(m.stock.view());
    }
    void onAdd(const itch::AddOrder& m) {
        count(m.h);
        if (!stocks.count(m.h.locate)) add_unknown_locate++;
        if (dumping())
            std::printf("%s %c ref=%llu %s %u @ %lld.%04lld\n", clock(m.h.timestamp).c_str(),
                        m.has_mpid ? 'F' : 'A', (unsigned long long)m.ref,
                        m.side == Side::BUY ? "BUY " : "SELL", m.shares, (long long)m.price / 10000,
                        (long long)m.price % 10000);
    }
    void onExecuted(const itch::Executed& m) {
        count(m.h);
        if (dumping())
            std::printf("%s E ref=%llu %u shares (%s)\n", clock(m.h.timestamp).c_str(),
                        (unsigned long long)m.ref, m.shares, sym(m.h.locate));
    }
    void onExecutedWithPrice(const itch::ExecutedWithPrice& m) {
        count(m.h);
        if (dumping())
            std::printf("%s C ref=%llu %u shares @ %lld.%04lld (%s)\n",
                        clock(m.h.timestamp).c_str(), (unsigned long long)m.ref, m.shares,
                        (long long)m.price / 10000, (long long)m.price % 10000, sym(m.h.locate));
    }
    void onCancel(const itch::Cancel& m) {
        count(m.h);
        if (dumping())
            std::printf("%s X ref=%llu -%u shares (%s)\n", clock(m.h.timestamp).c_str(),
                        (unsigned long long)m.ref, m.shares, sym(m.h.locate));
    }
    void onDelete(const itch::Delete& m) {
        count(m.h);
        if (dumping())
            std::printf("%s D ref=%llu (%s)\n", clock(m.h.timestamp).c_str(),
                        (unsigned long long)m.ref, sym(m.h.locate));
    }
    void onReplace(const itch::Replace& m) {
        count(m.h);
        if (dumping())
            std::printf("%s U ref=%llu -> %llu %u @ %lld.%04lld (%s)\n",
                        clock(m.h.timestamp).c_str(), (unsigned long long)m.old_ref,
                        (unsigned long long)m.new_ref, m.shares, (long long)m.price / 10000,
                        (long long)m.price % 10000, sym(m.h.locate));
    }
    void onOther(char type, const uint8_t* m, size_t) {
        total++;
        by_type[uint8_t(type)]++;
        (void)m;
    }
    void onBadLength(char type, size_t got, size_t want) {
        if (bad++ < 10)
            std::fprintf(stderr, "bad length for '%c': %zu, expected %zu\n", type, got, want);
    }

    static std::string clock(uint64_t ns) {
        uint64_t s = ns / 1000000000;
        char out[32];
        std::snprintf(out, sizeof out, "%02llu:%02llu:%02llu.%09llu", (unsigned long long)s / 3600,
                      (unsigned long long)s / 60 % 60, (unsigned long long)s % 60,
                      (unsigned long long)ns % 1000000000);
        return out;
    }
};

}  // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr, "usage: %s FILE[.gz] [--limit N] [--dump K]\n", argv[0]);
        return 2;
    }
    std::string path = argv[1];
    uint64_t limit = 0;
    Stats st;
    for (int i = 2; i + 1 < argc; i += 2) {
        if (!std::strcmp(argv[i], "--limit"))
            limit = std::strtoull(argv[i + 1], nullptr, 10);
        else if (!std::strcmp(argv[i], "--dump"))
            st.dump = std::strtoull(argv[i + 1], nullptr, 10);
        else {
            std::fprintf(stderr, "unknown flag %s\n", argv[i]);
            return 2;
        }
    }

    auto t0 = std::chrono::steady_clock::now();
    itch::ReadStats rs = itch::readFile(path, st, [&] { return limit == 0 || st.total < limit; });
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();

    std::printf("\n%llu messages, %.1f MB uncompressed, %.1fs (%.1f M msg/s incl. gunzip)\n",
                (unsigned long long)st.total, rs.bytes / 1e6, secs, st.total / secs / 1e6);
    std::printf("stocks in directory: %zu\n", st.stocks.size());
    std::map<char, uint64_t> sorted;
    for (int t = 0; t < 256; t++)
        if (st.by_type[t]) sorted[char(t)] = st.by_type[t];
    for (auto& [t, n] : sorted)
        std::printf("  '%c' %12llu%s\n", t, (unsigned long long)n,
                    itch::sizeOf(t) ? "" : "  (skipped)");

    int rc = 0;
    if (st.bad || rs.bad_length) {
        std::printf("ERROR: %llu messages with wrong length\n", (unsigned long long)st.bad);
        rc = 1;
    }
    if (rs.truncated && limit == 0) {
        std::printf("ERROR: file ends mid-message\n");
        rc = 1;
    }
    if (st.ts_backwards)
        std::printf("note: timestamp went backwards %llu times\n",
                    (unsigned long long)st.ts_backwards);
    if (st.add_unknown_locate)
        std::printf("note: %llu adds for a locate with no directory entry\n",
                    (unsigned long long)st.add_unknown_locate);
    return rc;
}
