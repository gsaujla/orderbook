#pragma once
// Log-bucketed latency histogram (HdrHistogram-style). Values below 32 get their own
// bucket; above that, each power of two is split into 32 sub-buckets, so a bucket is
// at most ~3% wide. Fixed size, no allocation when recording.

#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>

class Histogram {
public:
    static constexpr int kSubBits = 5;
    static constexpr uint64_t kSub = 1u << kSubBits;  // 32
    static constexpr size_t kBuckets = (64 - kSubBits + 1) * kSub;

    void record(uint64_t v) {
        counts[index(v)]++;
        total++;
        if (v > max_) max_ = v;
        if (v < min_) min_ = v;
    }

    uint64_t count() const { return total; }
    uint64_t max() const { return max_; }
    uint64_t min() const { return total ? min_ : 0; }

    // Upper bound of the bucket holding the p-th percentile (p in [0, 100]).
    uint64_t percentile(double p) const {
        if (total == 0) return 0;
        uint64_t rank = static_cast<uint64_t>(p / 100.0 * static_cast<double>(total));
        if (rank >= total) rank = total - 1;
        uint64_t seen = 0;
        for (size_t i = 0; i < kBuckets; i++) {
            seen += counts[i];
            if (seen > rank) return upper(i) < max_ ? upper(i) : max_;
        }
        return max_;
    }

    // One row per non-empty bucket: lower_ns,upper_ns,count
    void writeCsv(std::FILE* f) const {
        std::fprintf(f, "lower_ns,upper_ns,count\n");
        for (size_t i = 0; i < kBuckets; i++)
            if (counts[i])
                std::fprintf(f, "%llu,%llu,%llu\n", (unsigned long long)lower(i),
                             (unsigned long long)upper(i), (unsigned long long)counts[i]);
    }

private:
    std::array<uint64_t, kBuckets> counts{};
    uint64_t total = 0;
    uint64_t max_ = 0;
    uint64_t min_ = UINT64_MAX;

    static size_t index(uint64_t v) {
        if (v < kSub) return static_cast<size_t>(v);
        int top = 63 - std::countl_zero(v);  // position of highest set bit, >= kSubBits
        int shift = top - kSubBits;
        return static_cast<size_t>((shift + 1) * kSub + ((v >> shift) - kSub));
    }

    static uint64_t lower(size_t i) {
        if (i < kSub) return i;
        uint64_t shift = i / kSub - 1;
        return (kSub + i % kSub) << shift;
    }

    static uint64_t upper(size_t i) {
        if (i < kSub) return i;
        uint64_t shift = i / kSub - 1;
        return lower(i) + (uint64_t{1} << shift) - 1;
    }
};
