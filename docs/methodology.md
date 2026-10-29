# Benchmark methodology

How the latency and throughput numbers for the matching engine are produced, what
they mean, and what they do not control for.

## What is measured

The cost of one call to `Engine::submit` (limit or market order) or `Engine::cancel`,
including all matching and book updates that call triggers. Not included: event
generation, decoding, I/O, or reading the trades back out.

Two numbers come out of every run:

- **Per-message latency**: each event is timed on its own and recorded in a histogram.
  Reported as p50 / p99 / p99.9 / p99.99 / max, overall and per event type.
- **Throughput**: the same events timed as one block with no per-event timer, reported
  as messages per second. This is the more precise number on platforms with a coarse
  clock (see [Timer](#timer)).

## Workload

Synthetic events from `feed/synthetic_gen.hpp`, the same generator the fuzz test
uses. It is seeded, so a given seed and config always produce the same stream.

| Parameter | Default | Meaning |
|---|---|---|
| Mix | 50% limit, 8% market, 42% cancel | Share of each event type |
| Prices | mid ± 8 ticks | Buys lean below mid, sells above it, with 2 ticks of overlap so some limits cross |
| Mid | starts at 10000, random walk | Moves by ±1 tick on about 5% of events |
| Quantity | uniform 1–100 | Per order |
| Cancels | random live order | About 2% target ids that never existed |
| IDs | never reused | |

With seeds 1–4 the book peaks at 440–500 resting orders, and there are about 0.34
trades per event (3.39M trades over 10M events), measured with the fuzz runner.

## Procedure

1. Generate warm-up + measured events up front (default 1M + 4M) into a `vector`, so
   generation is not timed. Cancels are made valid by running the stream through a
   scratch engine once and recording which limits rested.
2. For each of 5 runs, with a fresh `Engine` each time:
   - **Latency pass:** replay the 1M warm-up events untimed. Then time each of the 4M
     measured events individually with two `steady_clock::now()` calls and record the
     difference in the histogram.
   - **Throughput pass:** fresh engine, same warm-up, then time the whole 4M-event
     block once.
3. Report the **median across the 5 runs** for each percentile and for throughput.

After every event the benchmark calls `Engine::ClearTrades()`, outside the timed
region. It keeps the trade buffer's capacity, so after warm-up the trade vector
never reallocates. Without it, an ever-growing vector (about 110 MB over a 10M-event
run) would add reallocation spikes to the tail that have nothing to do with matching.

### Histogram

`bench/histogram.hpp` is a fixed-size, log-bucketed histogram in the style of
HdrHistogram. Values below 32 ns get their own bucket. Above that, each power of two
is split into 32 sub-buckets, so a bucket is at most about 3% wide. Recording does no
allocation. A percentile is reported as the upper edge of the bucket that contains it,
capped at the observed max, so it is slightly conservative.

## Timer

`std::chrono::steady_clock`. The benchmark also measures timer overhead (back-to-back
`now()` calls) and prints it with every run.

**On Apple Silicon this clock ticks every 41.67 ns (24 MHz).** That is coarser than a
typical message, which costs about 30 ns (1 / 33 M msg/s). As a result:

- Per-event times are multiples of about 41.7 ns (0, 41, 83, 125, ...). A p50 of
  41 ns means "under one tick", and 0 ns means the clock did not advance.
- The **throughput number is accurate**, because one block of 4M events spans many
  ticks.
- p99 and above span several ticks and are meaningful, though only to about ±42 ns.

The intended fix is to run the final numbers on Linux x86 with `rdtsc` (sub-nanosecond
resolution), pinned to one core with `taskset`. See [Not controlled](#not-controlled).

## Results

Machine: Apple M5 (4 performance + 6 efficiency cores), 16 GB, macOS 26.6.2.
Compiler: Apple clang 21.0.0, `-std=c++20 -O3 -march=native -DNDEBUG` (CMake preset
`release`). Commit: `4826341`. Seed 1.

Median of 5 runs, 4M measured events after 1M warm-up. Latency includes timer
overhead.

| Event | Count | p50 | p99 | p99.9 | p99.99 | max |
|---|---|---|---|---|---|---|
| all | 4,000,000 | 41 ns | 85 ns | 125 ns | 211 ns | 41.6 µs |
| limit | 1,998,384 | 41 ns | 85 ns | 167 ns | 211 ns | 34.5 µs |
| market | 319,537 | 42 ns | 125 ns | 167 ns | 251 ns | 21.4 µs |
| cancel | 1,682,079 | 0 ns | 42 ns | 85 ns | 167 ns | 17.4 µs |

**Throughput: 33.4 M msg/s** (runs ranged from 32.1 to 33.8 M msg/s), which is about
30 ns per message on average.

### Reading the results

- **Cancels are cheapest.** A cancel is a hash lookup plus an O(1) unlink.
- **Market orders have the heaviest tail,** because one market order can sweep several
  price levels and produce several trades.
- **The 17–42 µs maximums** occur a handful of times per 4M events. They are almost
  certainly the OS pre-empting the process (interrupts, scheduling, moving the process
  between cores), not the engine. Tail percentiles up to p99.99 stay under about 250 ns.
- **These are best-case numbers.** The whole book (about 500 orders) fits in L1/L2
  cache. A real ITCH day (M4) has far deeper books and more cache misses, so expect it
  to be slower.

## Not controlled

| Factor | Effect | Planned fix |
|---|---|---|
| Clock resolution (41.67 ns) | p50 and low percentiles are quantized | Linux x86 + `rdtsc` |
| No core pinning (macOS has no API) | The process can move between cores or land on an efficiency core; this adds run-to-run spread | Linux + `taskset -c N` |
| Heterogeneous cores | P-cores and E-cores have very different speeds | Linux on a homogeneous CPU, or report core type |
| Frequency scaling / thermal | Clock speed can change during a run | Fixed governor (`performance`) on Linux; note in results |
| Background load | Other processes add tail noise | Quiet machine; run several times and report the median |
| Small, hot book | Optimistic versus real market data | ITCH replay benchmark (M4) |
| Heap allocation on the hot path | `std::map` level inserts and `unordered_map` order inserts allocate | Flat price array + order pool (M5); measure with heaptrack |

## Reproducing

```sh
cmake --preset release
cmake --build --preset release

# Defaults: 4M events, 1M warm-up, 5 runs, seed 1
./build/release/bench_synthetic --csv build/release/latency.csv

# Options
./build/release/bench_synthetic --events 4000000 --warmup 1000000 --runs 5 --seed 1

# Plot (needs matplotlib)
python3 tools/plot_latency.py build/release/latency.csv
```

When adding results here, record the machine, OS, compiler, flags, commit and seed
next to them, and only report numbers produced by the commands above.
