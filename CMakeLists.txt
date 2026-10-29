cmake_minimum_required(VERSION 3.24)
project(orderbook LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(ORDERBOOK_SANITIZE "Build with AddressSanitizer and UBSan" OFF)
if(ORDERBOOK_SANITIZE)
    # Applied to everything (including GoogleTest) so ASan sees all allocations.
    add_compile_options(-fsanitize=address,undefined -fno-omit-frame-pointer -fno-sanitize-recover=all)
    add_link_options(-fsanitize=address,undefined)
endif()

# Core book/engine code, shared by the executable, tests and (later) benchmarks.
add_library(orderbook
    src/book.cpp
    src/poolbook.cpp
    src/engine.cpp
)
target_include_directories(orderbook PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_compile_options(orderbook PRIVATE -Wall -Wextra)

add_executable(bookexecutable src/main.cpp)
target_link_libraries(bookexecutable PRIVATE orderbook)

include(FetchContent)
FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
FetchContent_MakeAvailable(googletest)

enable_testing()
include(GoogleTest)

add_executable(booktests test/book_test.cpp test/poolbook_test.cpp test/engine_test.cpp test/fuzz_test.cpp)
target_link_libraries(booktests PRIVATE orderbook GTest::gtest_main)
gtest_discover_tests(booktests)

# Standalone fuzzer for long runs (e.g. 10M events); not part of ctest.
add_executable(fuzz test/fuzz_main.cpp)
target_link_libraries(fuzz PRIVATE orderbook)

# Synthetic event generator, shared by the fuzzer and benchmarks.
add_library(synthgen INTERFACE)
target_include_directories(synthgen INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/feed)
target_link_libraries(booktests PRIVATE synthgen)
target_link_libraries(fuzz PRIVATE synthgen)

# Latency / throughput benchmark. Meaningful numbers only from the release preset.
add_executable(bench_synthetic bench/bench_synthetic.cpp)
target_include_directories(bench_synthetic PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/bench)
target_link_libraries(bench_synthetic PRIVATE orderbook synthgen)

# ITCH 5.0 parser (header-only); zlib lets it read the .gz sample files directly.
find_package(ZLIB REQUIRED)
add_library(itch INTERFACE)
target_include_directories(itch INTERFACE ${CMAKE_CURRENT_SOURCE_DIR}/feed)
target_link_libraries(itch INTERFACE ZLIB::ZLIB)
target_link_libraries(booktests PRIVATE itch)
target_sources(booktests PRIVATE test/itch_test.cpp)

# Parse a real ITCH file and print message counts (no book).
add_executable(itch_stats feed/itch_stats.cpp)
target_link_libraries(itch_stats PRIVATE orderbook itch)
target_sources(booktests PRIVATE test/itch_replay_test.cpp)

# Replay a full ITCH day into per-stock books, with snapshots and optional timing.
add_executable(itch_replay feed/itch_replay.cpp)
target_include_directories(itch_replay PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/bench)
target_link_libraries(itch_replay PRIVATE orderbook itch)
