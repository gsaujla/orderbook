// Replay a full ITCH 5.0 day into per-stock books, print book snapshots for a few
// symbols at fixed times, and report replay errors. Optionally time every book update.
//
//   ./build/release/itch_replay data/01302019.NASDAQ_ITCH50.gz
//       [--symbols AAPL,MSFT,SPY] [--at 09:35:00,12:00:00,15:55:00]
//       [--limit N] [--time] [--csv latency.csv] [--book map|pool] [--pool N]
//
// --book map replays into Book; --book pool into PoolBook, all stocks sharing one
// OrderPool of N slots (default 4M).
//
// Exit code 0 only if the whole file replays with zero errors.

#include "itch_replay.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sstream>
#include <string>
#include <vector>

#include "histogram.hpp"
#include "itch_reader.hpp"

namespace {

using Clock = std::chrono::steady_clock;

std::vector<std::string> split(const std::string& s) {
    std::vector<std::string> out;
    std::stringstream ss(s);
    std::string item;
    while (std::getline(ss, item, ','))
        if (!item.empty()) out.push_back(item);
    return out;
}

// "HH:MM:SS" -> ns since midnight
uint64_t parseTime(const std::string& s) {
    unsigned h = 0, m = 0, sec = 0;
    std::sscanf(s.c_str(), "%u:%u:%u", &h, &m, &sec);
    return (uint64_t(h) * 3600 + m * 60 + sec) * 1000000000ull;
}

std::string px(int64_t ticks) {
    char b[32];
    std::snprintf(b, sizeof b, "%lld.%04lld", (long long)ticks / 10000, (long long)ticks % 10000);
    return b;
}

std::string hhmmss(uint64_t ns) {
    uint64_t s = ns / 1000000000;
    char b[16];
    std::snprintf(b, sizeof b, "%02llu:%02llu:%02llu", (unsigned long long)s / 3600,
                  (unsigned long long)s / 60 % 60, (unsigned long long)s % 60);
    return b;
}

enum Kind { kAdd, kExec, kCancel, kDelete, kReplace, kKinds };
const char* kKindName[kKinds] = {"add A/F", "exec E/C", "cancel X", "delete D", "replace U"};

template <typename BookT>
struct Driver : itch::Handler {
    explicit Driver(size_t pool_capacity) : r(pool_capacity) {}

    itch::BasicReplayer<BookT> r;
    std::vector<std::string> symbols;
    std::vector<uint64_t> at;  // snapshot times, ascending
    size_t next_at = 0;
    bool timing = false;
    Histogram all, byKind[kKinds];
    uint64_t messages = 0;
    uint64_t last_ts = 0;

    void tick(uint64_t ts) {
        messages++;
        last_ts = ts;
        while (next_at < at.size() && ts >= at[next_at]) snapshot(at[next_at++]);
    }

    template <typename F>
    void timed(Kind k, F f) {
        if (!timing) {
            f();
            return;
        }
        auto t0 = Clock::now();
        f();
        auto t1 = Clock::now();
        uint64_t d =
            uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
        all.record(d);
        byKind[k].record(d);
    }

    void onSystemEvent(const itch::SystemEvent& m) { tick(m.h.timestamp); }
    void onStockDirectory(const itch::StockDirectory& m) {
        tick(m.h.timestamp);
        r.onStockDirectory(m);
    }
    void onAdd(const itch::AddOrder& m) {
        tick(m.h.timestamp);
        timed(kAdd, [&] { r.onAdd(m); });
    }
    void onExecuted(const itch::Executed& m) {
        tick(m.h.timestamp);
        timed(kExec, [&] { r.onExecuted(m); });
    }
    void onExecutedWithPrice(const itch::ExecutedWithPrice& m) {
        tick(m.h.timestamp);
        timed(kExec, [&] { r.onExecutedWithPrice(m); });
    }
    void onCancel(const itch::Cancel& m) {
        tick(m.h.timestamp);
        timed(kCancel, [&] { r.onCancel(m); });
    }
    void onDelete(const itch::Delete& m) {
        tick(m.h.timestamp);
        timed(kDelete, [&] { r.onDelete(m); });
    }
    void onReplace(const itch::Replace& m) {
        tick(m.h.timestamp);
        timed(kReplace, [&] { r.onReplace(m); });
    }
    void onOther(char, const uint8_t*, size_t) { messages++; }
    void onBadLength(char type, size_t got, size_t want) {
        std::fprintf(stderr, "bad length for '%c': %zu, expected %zu\n", type, got, want);
    }

    uint64_t crossedBooks() const {
        uint64_t n = 0;
        r.forEachBook([&](uint16_t, const BookT& b) {
            auto bid = b.Best_Bid(), ask = b.Best_Ask();
            if (bid && ask && *bid >= *ask) n++;
        });
        return n;
    }

    void snapshot(uint64_t t) {
        std::printf("\n=== %s  (%llu resting orders, %llu crossed books) ===\n", hhmmss(t).c_str(),
                    (unsigned long long)r.counts().resting, (unsigned long long)crossedBooks());
        for (const std::string& s : symbols) {
            const BookT* b = r.bookFor(s);
            if (!b) {
                std::printf("%-6s (no orders yet)\n", s.c_str());
                continue;
            }
            auto bids = b->Depth(Side::BUY, 5);
            auto asks = b->Depth(Side::SELL, 5);
            std::printf("%-6s", s.c_str());
            if (!bids.empty() && !asks.empty())
                std::printf("  spread %s", px(asks[0].first - bids[0].first).c_str());
            std::printf("\n");
            for (size_t i = 0; i < 5 && (i < bids.size() || i < asks.size()); i++) {
                std::string bl = i < bids.size()
                                     ? std::to_string(bids[i].second) + " @ " + px(bids[i].first)
                                     : "";
                std::string al = i < asks.size()
                                     ? px(asks[i].first) + " x " + std::to_string(asks[i].second)
                                     : "";
                std::printf("    %24s  |  %-24s\n", bl.c_str(), al.c_str());
            }
        }
    }
};

}  // namespace

struct Options {
    std::string path;
    std::vector<std::string> symbols = split("AAPL,MSFT,AMZN,SPY,QQQ");
    std::vector<std::string> at = split("09:35:00,12:00:00,15:55:00");
    uint64_t limit = 0;
    bool timing = false;
    std::string csv;
    std::string book = "map";
    size_t pool_capacity = size_t(1) << 22;
};

template <typename BookT>
int run(const Options& opt) {
    Driver<BookT> d(opt.pool_capacity);
    d.symbols = opt.symbols;
    d.timing = opt.timing;
    for (const std::string& t : opt.at) d.at.push_back(parseTime(t));
    std::sort(d.at.begin(), d.at.end());
    const uint64_t limit = opt.limit;
    const std::string& csv = opt.csv;
    std::printf("book: %s\n",
                opt.book == "pool" ? "pool (PoolBook, shared OrderPool)" : "map (Book)");

    auto t0 = Clock::now();
    itch::ReadStats rs =
        itch::readFile(opt.path, d, [&] { return limit == 0 || limit > d.messages; });
    double secs = std::chrono::duration<double>(Clock::now() - t0).count();

    const itch::ReplayCounts& n = d.r.counts();
    const itch::ReplayErrors& e = d.r.errors();
    std::printf("\n=== end of replay at %s ===\n", hhmmss(d.last_ts).c_str());
    std::printf("%llu messages, %.1f GB uncompressed, %.1fs (%.2f M msg/s incl. gunzip)\n",
                (unsigned long long)d.messages, rs.bytes / 1e9, secs, d.messages / secs / 1e6);
    std::printf(
        "adds %llu, executions %llu (%llu shares), partial cancels %llu, deletes %llu, "
        "replaces %llu\n",
        (unsigned long long)n.adds, (unsigned long long)n.executes,
        (unsigned long long)n.executed_shares, (unsigned long long)n.partial_cancels,
        (unsigned long long)n.deletes, (unsigned long long)n.replaces);
    std::printf("resting at end %llu, peak %llu, crossed books at end %llu\n",
                (unsigned long long)n.resting, (unsigned long long)n.max_resting,
                (unsigned long long)d.crossedBooks());
    std::printf(
        "errors: unknown ref %llu, overfill %llu, duplicate add %llu, zero shares %llu, "
        "pool full %llu\n",
        (unsigned long long)e.unknown_ref, (unsigned long long)e.overfill,
        (unsigned long long)e.duplicate_add, (unsigned long long)e.zero_shares,
        (unsigned long long)e.pool_full);
    if (const OrderPool* p = d.r.orderPool())
        std::printf("order pool: %zu slots, %zu ever used, %zu live at end\n", p->Capacity(),
                    p->HighWater(), p->Live());

    if (d.timing) {
        std::printf("\nbook update latency, ns (excludes parsing; includes ~42 ns clock tick):\n");
        std::printf("%-10s %12s %6s %6s %7s %8s %9s\n", "message", "count", "p50", "p99", "p99.9",
                    "p99.99", "max");
        auto row = [](const char* name, const Histogram& h) {
            std::printf("%-10s %12llu %6llu %6llu %7llu %8llu %9llu\n", name,
                        (unsigned long long)h.count(), (unsigned long long)h.percentile(50),
                        (unsigned long long)h.percentile(99),
                        (unsigned long long)h.percentile(99.9),
                        (unsigned long long)h.percentile(99.99), (unsigned long long)h.max());
        };
        row("all", d.all);
        for (int k = 0; k < kKinds; k++) row(kKindName[k], d.byKind[k]);
        if (!csv.empty()) {
            if (std::FILE* f = std::fopen(csv.c_str(), "w")) {
                d.all.writeCsv(f);
                std::fclose(f);
                std::printf("histogram written to %s\n", csv.c_str());
            }
        }
    }

    bool ok = e.total() == 0 && !rs.bad_length && (limit != 0 || !rs.truncated);
    if (rs.truncated && limit == 0) std::printf("ERROR: file ends mid-message\n");
    std::printf("%s\n", ok ? "OK: replay clean" : "FAILED");
    return ok ? 0 : 1;
}

int main(int argc, char** argv) {
    if (argc < 2) {
        std::fprintf(stderr,
                     "usage: %s FILE[.gz] [--symbols A,B] [--at HH:MM:SS,...] [--limit N] "
                     "[--time] [--csv path] [--book map|pool] [--pool N]\n",
                     argv[0]);
        return 2;
    }
    Options opt;
    opt.path = argv[1];
    for (int i = 2; i < argc; i++) {
        auto arg = [&] {
            if (i + 1 >= argc) {
                std::fprintf(stderr, "%s needs a value\n", argv[i]);
                std::exit(2);
            }
            return std::string(argv[++i]);
        };
        if (!std::strcmp(argv[i], "--symbols"))
            opt.symbols = split(arg());
        else if (!std::strcmp(argv[i], "--at"))
            opt.at = split(arg());
        else if (!std::strcmp(argv[i], "--limit"))
            opt.limit = std::strtoull(arg().c_str(), nullptr, 10);
        else if (!std::strcmp(argv[i], "--csv"))
            opt.csv = arg();
        else if (!std::strcmp(argv[i], "--time"))
            opt.timing = true;
        else if (!std::strcmp(argv[i], "--book"))
            opt.book = arg();
        else if (!std::strcmp(argv[i], "--pool"))
            opt.pool_capacity = std::strtoull(arg().c_str(), nullptr, 10);
        else {
            std::fprintf(stderr, "unknown flag %s\n", argv[i]);
            return 2;
        }
    }
    if (opt.book == "pool") return run<PoolBook>(opt);
    if (opt.book == "map") return run<Book>(opt);
    std::fprintf(stderr, "--book must be map or pool\n");
    return 2;
}
