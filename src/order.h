// order.h
#pragma once
#include <cstdint>

using Price    = uint32_t;
using Quantity = uint32_t;
using OrderId  = uint64_t;

enum class Side { Buy, Sell };

struct Order {
    OrderId  id;
    Price    price;
    Quantity quantity;
    Side     side;
};