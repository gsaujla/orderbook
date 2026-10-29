// Per-message latency and throughput of Engine on the synthetic event stream.
//
//   ./build/release/bench_synthetic [--events N] [--warmup W] [--runs R] [--seed S]
//                                   [--csv latency.csv]
//
// Each run replays the same pre-generated events into a fresh Engine:
//   1. latency pass:    warm-up untimed, then every event timed on its own
//   2. throughput pass: warm-up untimed, then the whole block timed once (no per-event timer)
// Reported numbers are the median across runs.

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

#include "engine.hpp"
#include "histogram.hpp"
#include "synthetic_gen.hpp"

namespace {

using Clock = std::chrono::steady_clock;

inline uint64_t ns(Clock::time_point a, Clock::time_point b) {
    return static_cast<uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(b - a).count());
}

enum Kind { kLimit, kMarket, kCancel, kKinds };
const char* kKindName[kKinds] = {"limit", "market", "cancel"};

Kind kindOf(const synth::Event& e) {
    if (e.kind == synth::Event::Cancel) return kCancel;
    return e.order.type == OrderType::Market ? kMarket : kLimit;
}

inline void apply(Engine& e, const synth::Event& ev) {
    if (ev.kind == synth::Event::Submit)
        e.submit(ev.order);
    else
        e.cancel(ev.id);
}

// Generate events and learn which limits rest, so cancels target live orders.
std::vector<synth::Event> generate(const synth::Config& cfg, size_t n) {
    synth::Generator gen(cfg);
    Engine scratch;
    std::vector<synth::Event> events;
    events.reserve(n);
    for (size_t i = 0; i < n; i++) {
        synth::Event ev = gen.next();
        if (ev.kind == synth::Event::Submit) {
            uint32_t left = scratch.submit(ev.order);
            if (ev.order.type == OrderType::Limit && left > 0) gen.rested(ev.order.id);
        } else {
            scratch.cancel(ev.id);
        }
        scratch.ClearTrades();
        events.push_back(ev);
    }
    return events;
}

struct RunResult {
    Histogram all;
    Histogram byKind[kKinds];
    double msgs_per_sec = 0;
};

RunResult runOnce(const std::vector<synth::Event>& events, size_t warmup) {
    RunResult r;

    {  // latency pass
        Engine e;
        for (size_t i = 0; i < warmup; i++) {
            apply(e, events[i]);
            e.ClearTrades();
        }
        for (size_t i = warmup; i < events.size(); i++) {
            const synth::Event& ev = events[i];
            auto t0 = Clock::now();
            apply(e, ev);
            auto t1 = Clock::now();
            uint64_t d = ns(t0, t1);
            r.all.record(d);
            r.byKind[kindOf(ev)].record(d);
            e.ClearTrades();
        }
    }

    {  // throughput pass
        Engine e;
        for (size_t i = 0; i < warmup; i++) {
            apply(e, events[i]);
            e.ClearTrades();
        }
        auto t0 = Clock::now();
        for (size_t i = warmup; i < events.size(); i++) {
            apply(e, events[i]);
            e.ClearTrades();
        }
        auto t1 = Clock::now();
        r.msgs_per_sec = double(events.size() - warmup) / (double(ns(t0, t1)) / 1e9);
    }
    return r;
}

Histogram timerOverhead() {
    Histogram h;
    for (int i = 0; i < 2000000; i++) {
        auto a = Clock::now();
        auto b = Clock::now();
        h.record(ns(a, b));
    }
    return h;
}

template <typename F>
double median(const std::vector<RunResult>& runs, F get) {
    std::vector<double> v;
    for (const RunResult& r : runs) v.push_back(double(get(r)));
    std::sort(v.begin(), v.end());
    return v[v.size() / 2];
}

void printRow(const char* name, const std::vector<RunResult>& runs,
              const Histogram* (*pick)(const RunResult&)) {
    std::printf("%-8s %10.0f %7.0f %7.0f %7.0f %8.0f %10.0f\n", name,
                median(runs, [&](const RunResult& r) { return pick(r)->count(); }),
                median(runs, [&](const RunResult& r) { return pick(r)->percentile(50); }),
                median(runs, [&](const RunResult& r) { return pick(r)->percentile(99); }),
                median(runs, [&](const RunResult& r) { return pick(r)->percentile(99.9); }),
                median(runs, [&](const RunResult& r) { return pick(r)->percentile(99.99); }),
                median(runs, [&](const RunResult& r) { return pick(r)->max(); }));
}

}  // namespace

int main(int argc, char** argv) {
    synth::Config cfg;
    size_t events = 4000000, warmup = 1000000;
    int runs = 5;
    std::string csv;
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!std::strcmp(argv[i], "--events"))
            events = std::strtoull(argv[i + 1], nullptr, 10);
        else if (!std::strcmp(argv[i], "--warmup"))
            warmup = std::strtoull(argv[i + 1], nullptr, 10);
        else if (!std::strcmp(argv[i], "--runs"))
            runs = std::atoi(argv[i + 1]);
        else if (!std::strcmp(argv[i], "--seed"))
            cfg.seed = std::strtoull(argv[i + 1], nullptr, 10);
        else if (!std::strcmp(argv[i], "--csv"))
            csv = argv[i + 1];
        else {
            std::fprintf(stderr, "unknown flag %s\n", argv[i]);
            return 2;
        }
    }

    std::printf("generating %zu events (%zu warm-up), seed %llu...\n", warmup + events, warmup,
                (unsigned long long)cfg.seed);
    std::vector<synth::Event> evs = generate(cfg, warmup + events);

    Histogram overhead = timerOverhead();
    std::printf("timer overhead (back-to-back now()): p50 %llu ns, p99 %llu ns\n",
                (unsigned long long)overhead.percentile(50),
                (unsigned long long)overhead.percentile(99));

    std::vector<RunResult> results;
    for (int r = 0; r < runs; r++) {
        results.push_back(runOnce(evs, warmup));
        std::printf("run %d: %.2f M msg/s, p50 %llu ns, p99 %llu ns\n", r + 1,
                    results.back().msgs_per_sec / 1e6,
                    (unsigned long long)results.back().all.percentile(50),
                    (unsigned long long)results.back().all.percentile(99));
    }

    std::printf("\nmedian of %d runs, latency in ns (includes timer overhead):\n", runs);
    std::printf("%-8s %10s %7s %7s %7s %8s %10s\n", "event", "count", "p50", "p99", "p99.9",
                "p99.99", "max");
    printRow("all", results, [](const RunResult& r) { return &r.all; });
    printRow(kKindName[kLimit], results, [](const RunResult& r) { return &r.byKind[kLimit]; });
    printRow(kKindName[kMarket], results, [](const RunResult& r) { return &r.byKind[kMarket]; });
    printRow(kKindName[kCancel], results, [](const RunResult& r) { return &r.byKind[kCancel]; });
    std::printf("\nthroughput (no per-event timer): %.2f M msg/s\n",
                median(results, [](const RunResult& r) { return r.msgs_per_sec; }) / 1e6);

    if (!csv.empty()) {
        std::FILE* f = std::fopen(csv.c_str(), "w");
        if (!f) {
            std::perror(csv.c_str());
            return 1;
        }
        results[results.size() / 2].all.writeCsv(f);
        std::fclose(f);
        std::printf("histogram of run %zu written to %s\n", results.size() / 2 + 1, csv.c_str());
    }
    return 0;
}
