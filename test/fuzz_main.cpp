// Long fuzz runs outside gtest:
//   ./build/release/fuzz --events 10000000 --seed 42 [--mid 0] [--book map|pool]
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

#include "fuzz_harness.hpp"

int main(int argc, char** argv) {
    fuzz::Config cfg;
    cfg.events = 10000000;
    std::string book = "map";
    for (int i = 1; i + 1 < argc; i += 2) {
        if (!std::strcmp(argv[i], "--events"))
            cfg.events = std::strtoull(argv[i + 1], nullptr, 10);
        else if (!std::strcmp(argv[i], "--seed"))
            cfg.seed = std::strtoull(argv[i + 1], nullptr, 10);
        else if (!std::strcmp(argv[i], "--mid"))
            cfg.start_mid = std::strtoll(argv[i + 1], nullptr, 10);
        else if (!std::strcmp(argv[i], "--book"))
            book = argv[i + 1];
        else {
            std::fprintf(stderr, "unknown flag %s\n", argv[i]);
            return 2;
        }
    }

    auto start = std::chrono::steady_clock::now();
    if (book != "map" && book != "pool") {
        std::fprintf(stderr, "--book must be map or pool\n");
        return 2;
    }
    fuzz::Result r = book == "pool" ? fuzz::run<PoolEngine>(cfg) : fuzz::run<Engine>(cfg);
    double secs = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();

    if (!r.ok) {
        std::fprintf(stderr, "%s", r.error.c_str());
        return 1;
    }
    std::printf("ok: %llu events, %llu trades, max %zu resting orders, seed %llu, %s book, %.1fs\n",
                (unsigned long long)r.events_run, (unsigned long long)r.trades, r.max_live,
                (unsigned long long)cfg.seed, book.c_str(), secs);
    return 0;
}
