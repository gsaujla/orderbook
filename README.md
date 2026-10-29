# orderbook

A single-threaded limit order book and matching engine in C++20. It supports limit and
market orders, matches by price-time priority, and is checked against a naive
reference implementation with a seeded fuzzer. It replays a full day of real NASDAQ
ITCH 5.0 data.

## Status

| Milestone | State |
|---|---|
| M0 Skeleton: CMake, GoogleTest, CI, clang-format, ASan/UBSan | Done |
| M1 Correct book: add, cancel, limit and market orders, partial fills, trades | Done (modify pending) |
| M2 Reference book + fuzzing | Done: 10M events per seed, no divergence |
| M3 Measurement | Done (macOS numbers; Linux + `rdtsc` planned) |
| M4 ITCH 5.0 replay | Done: full day (368M messages), zero errors |
| M5 Flat price array + order pool | Planned |

## Performance

Synthetic workload on an Apple M5, Apple clang 21, `-O3 -march=native`. Median of 5
runs: 4M timed events after 1M warm-up.

**Throughput: 33.4 M messages/s**, about 30 ns per message on average.

| Event | p50 | p99 | p99.9 | p99.99 |
|---|---|---|---|---|
| all | 41 ns | 85 ns | 125 ns | 211 ns |
| limit | 41 ns | 85 ns | 167 ns | 211 ns |
| market | 42 ns | 125 ns | 167 ns | 251 ns |
| cancel | 0 ns | 42 ns | 85 ns | 167 ns |

The clock on Apple Silicon ticks every 41.67 ns, which is longer than a typical
message. So the p50 values mean "under one tick", and the throughput figure is the
precise one. The book in this workload is small (about 500 orders) and fits in cache,
so these are best-case numbers. See [docs/methodology.md](docs/methodology.md) for the
full setup, the max latencies, and what is not controlled.

### Real data: NASDAQ ITCH 5.0, 30 Jan 2019

A full-day replay of 368M messages into about 8,700 per-stock books, with up to 1.74M
resting orders at once. Errors: **0**. Every order is gone by the end of the day, as
it should be, and no book is ever crossed.

Book-update latency only (parsing excluded), same machine and caveats as above:

| Message | p50 | p99 | p99.9 | p99.99 |
|---|---|---|---|---|
| all | 125 ns | 879 ns | 1.3 µs | 6.4 µs |
| add | 125 ns | 591 ns | 927 ns | 6.3 µs |
| execute | 125 ns | 927 ns | 1.3 µs | 6.1 µs |
| delete | 125 ns | 879 ns | 1.3 µs | 6.3 µs |
| replace | 295 ns | 1.2 µs | 1.8 µs | 7.8 µs |

That is about 3× slower at p50 and 10× slower at p99 than the synthetic workload: real
book state does not fit in cache. Maxima reach a few ms, probably hash-map rehashes of
large books (not yet confirmed); removing those is an M5 goal.

```sh
tools/download_itch_sample.sh          # about 4.8 GB into data/, resumable
./build/release/itch_replay data/01302019.NASDAQ_ITCH50.gz --time
```

## Design

```
Engine   matching: submit(NewOrder) -> Trades, cancel(id)
  └─ Book   storage only: add, cancel, fill, front, depth, best bid/ask
       ├─ bids  std::map<price, Level, greater>   begin() = highest bid
       ├─ asks  std::map<price, Level, less>      begin() = lowest ask
       └─ orders std::unordered_map<id, Order>    owns every resting order
```

- **`Order`** is plain data plus `prev`/`next` pointers. Each price level is an
  intrusive doubly linked FIFO queue of orders, so a cancel is an O(1) unlink after a
  hash lookup.
- **Ownership:** `unordered_map` owns orders by value. Its element addresses stay valid
  across rehash, so levels can safely point into it.
- **`Level`** tracks head, tail, order count and total resting quantity.
- **Matching lives in `Engine`, not in `Book::Add`.** ITCH replay calls `Book`
  directly, because NASDAQ already did the matching. Matching inside `Add` would
  invent trades.
- **Prices are signed integer ticks** (`int64_t`), never floating point. Trades execute
  at the resting order's price.

### Invariants enforced by tests and the fuzzer

1. Price-time priority: best price first, then FIFO within a price.
2. A trade executes at the resting order's price.
3. A partially filled resting order keeps its place at the front of its queue.
4. An emptied price level is removed immediately.
5. Every quantity change updates both the order and its level's total.
6. Only limit orders rest. A market order's leftover is dropped.
7. The book is never left crossed (best bid < best ask).

## Testing

- **52 GoogleTest cases**, all run under AddressSanitizer and UndefinedBehaviorSanitizer:
  - 30 book tests
  - 17 matching tests
  - 5 fuzz cases
- **Reference fuzzer** (`test/reference_book.hpp`): a deliberately naive book using
  `std::map` + `std::list`, written independently of the real one. A seeded generator
  drives both books. After every event it compares return values, trades and full
  depth on both sides, and checks the book is not crossed. On a mismatch it prints the
  seed, the event number and the last 12 events.
- **Long runs:** 10M events for each of 4 seeds (one centred on price 0 to cover
  negative prices), with no divergence. Two planted bugs were caught within 40 events.
- **CI** (GitHub Actions): Linux and macOS, sanitizer and release builds, plus a
  clang-format check.

## Build and run

Requires CMake ≥ 3.24 and a C++20 compiler. GoogleTest is fetched automatically.

```sh
# Tests (Debug + ASan/UBSan)
cmake --preset debug-asan
cmake --build --preset debug-asan
ctest --preset debug-asan

# Release build: benchmark and long fuzz runs
cmake --preset release
cmake --build --preset release
./build/release/bench_synthetic --csv build/release/latency.csv
./build/release/fuzz --events 10000000 --seed 42 [--mid 0]

# Formatting (CI uses clang-format 19.1.7)
clang-format -i include/*.hpp src/*.cpp test/*.cpp test/*.hpp bench/* feed/*
```

## Layout

```
include/   order.hpp, book.hpp, engine.hpp, events.hpp
src/       book.cpp, engine.cpp, main.cpp
feed/      synthetic_gen.hpp           seeded event generator (fuzz + bench)
           itch_parser/reader/replay   ITCH 5.0 decoding and per-stock replay
           itch_stats.cpp, itch_replay.cpp
test/      unit tests, reference_book.hpp, fuzz harness + runner
bench/     bench_synthetic.cpp, histogram.hpp
tools/     plot_latency.py, download_itch_sample.sh
docs/      methodology.md
```

## Roadmap

- **M5:** a flat tick-indexed price array plus a pre-allocated order pool, with no
  heap allocation on the hot path, and before/after latency tables.
- **Benchmarks on Linux x86** with `rdtsc` and core pinning, for nanosecond-accurate
  percentiles.
