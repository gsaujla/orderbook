cmake_minimum_required(VERSION 4.4.4)
project(orderbook LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

add_executable(bookexecutable
    src/main.cpp
)
target_include_directories(bookexecutable
    PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include
)