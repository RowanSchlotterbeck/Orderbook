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

# ---- Benchmarks ----
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_WERROR OFF CACHE BOOL "" FORCE)

FetchContent_Declare(
        googlebenchmark
        URL https://github.com/google/benchmark/archive/refs/tags/v1.9.1.zip
)
FetchContent_MakeAvailable(googlebenchmark)

# Google Benchmark's own CMake adds -pedantic-errors, which on newer Clang
# promotes a __COUNTER__ usage in benchmark.h to a hard error
# (-Wc2y-extensions). Suppress it on the vendored benchmark targets only.
foreach(_bm_target benchmark benchmark_main)
    if(TARGET ${_bm_target})
        target_compile_options(${_bm_target} PRIVATE -Wno-c2y-extensions)
    endif()
endforeach()

add_executable(OrderbookBenchmarks
        src/order.h
        src/orderbook.h
        src/orderbook.cpp
        bench/orderbook_bench.cpp)

target_include_directories(OrderbookBenchmarks PRIVATE src)
target_link_libraries(OrderbookBenchmarks PRIVATE benchmark::benchmark)

# Force optimizations for benchmarking regardless of the configured build
# type, since unoptimized numbers would be meaningless.
target_compile_options(OrderbookBenchmarks PRIVATE -O3 -DNDEBUG)
