cmake_minimum_required(VERSION 4.1)
project(Orderbook)

set(CMAKE_CXX_STANDARD 23)

add_executable(Orderbook src/main.cpp
        src/order.h
        src/orderbook.h
        src/orderbook.cpp)

# ---- Tests ----
include(FetchContent)
FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.zip
)
FetchContent_MakeAvailable(googletest)

enable_testing()

add_executable(OrderbookTests
        src/order.h
        src/orderbook.h
        src/orderbook.cpp
        tests/orderbook_tests.cpp)

target_include_directories(OrderbookTests PRIVATE src)
target_link_libraries(OrderbookTests PRIVATE gtest_main)

include(GoogleTest)
gtest_discover_tests(OrderbookTests)
