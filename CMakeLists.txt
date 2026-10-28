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

add_executable(booktests test/book_test.cpp test/engine_test.cpp test/fuzz_test.cpp)
target_link_libraries(booktests PRIVATE orderbook GTest::gtest_main)
gtest_discover_tests(booktests)

# Standalone fuzzer for long runs (e.g. 10M events); not part of ctest.
add_executable(fuzz test/fuzz_main.cpp)
target_link_libraries(fuzz PRIVATE orderbook)
