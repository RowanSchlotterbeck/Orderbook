// orderbook.h
#pragma once
#include <map>
#include <list>
#include <unordered_map>
#include "order.h"

class Orderbook {
public:
    void addOrder(Order order);
    void deleteOrder(OrderId order_id);
    void printBook() const;

private:
    struct OrderLocation {
        Price price;
        Side  side;
        std::list<Order>::iterator it;
    };

    std::map<Price, std::list<Order>> bids_;
    std::map<Price, std::list<Order>> asks_;
    std::unordered_map<OrderId, OrderLocation> orderLookup_;

    void matchIncoming(Order& order);
};