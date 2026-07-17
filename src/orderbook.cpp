// orderbook.cpp
#include "orderbook.h"
#include <iostream>
#include <algorithm>
#include <iterator>

void Orderbook::addOrder(Order order)
{

    // instantaneously check order
    matchIncoming(order);

    if (order.quantity > 0)
    {
        auto& book_side = (order.side == Side::Buy) ? bids_ : asks_;
        auto& level = book_side[order.price];

        level.push_back(order);

        auto it = std::prev(level.end()); // gets reference to location in list

        orderLookup_[order.id] = {order.price, order.side, it}; // adds to order lookup

    }


};

void Orderbook::deleteOrder(OrderId order_id)
{
    auto found = orderLookup_.find(order_id);
    if (found == orderLookup_.end()) return; // order not in lookup

    const OrderLocation& location = found->second;
    auto& book_side = (location.side == Side::Buy) ? bids_ : asks_;

    auto levelIt = book_side.find(location.price);
    if (levelIt != book_side.end()) {
        levelIt->second.erase(location.it);
        if (levelIt->second.empty()) {
            book_side.erase(levelIt);
        }
    }

    orderLookup_.erase(found);

};

// Any change to the print book method will require an additional change in the google tests, espicially if there
// is a change in format
void Orderbook::printBook() const
{
    std::cout << "--- BOOK ---\n";
    std::cout << "ASKS (low to high):\n";
    for (auto it = asks_.begin(); it != asks_.end(); ++it)
    {
        Quantity totalQty = 0;
        for (const auto& order : it->second)
        {
            totalQty += order.quantity;
        }
        std::cout << "  " << it->first
                  << " x " << totalQty
                  << " (" << it->second.size() << " orders)\n";
    }

    std::cout << "BIDS (high to low):\n";
    for (auto it = bids_.rbegin(); it != bids_.rend(); ++it)
    {
        Quantity totalQty = 0;
        for (const auto& order : it->second)
        {
            totalQty += order.quantity;
        }
        std::cout << "  " << it->first
                  << " x " << totalQty
                  << " (" << it->second.size() << " orders)\n";
    }

    std::cout << "------------\n";
}

void Orderbook::matchIncoming(Order& order)
    {
        // determine the side of the order
        if (order.side == Side::Buy)
        {
            while (order.quantity > 0 && !asks_.empty() && order.price >= asks_.begin()->first)
            {

                auto bestAskLevel = asks_.begin();
                OrderId askId;
                bool askExhausted;
                // code is scoped to prevent a dangling reference with best ask
                {
                    auto& bestAsk = bestAskLevel->second.front();
                    askId = bestAsk.id;
                    Quantity traded = std::min(bestAsk.quantity, order.quantity);
                    order.quantity -= traded;
                    bestAsk.quantity -= traded;
                    askExhausted = (bestAsk.quantity == 0);

                    std::cout << "FILL " << order.id << " " << askId
                    << " " << traded << " " << bestAskLevel->first << "\n";

                }


                if (askExhausted)
                {
                    orderLookup_.erase(askId);        // remove from lookup
                    bestAskLevel->second.pop_front();       // remove from list
                    if (bestAskLevel->second.empty()) {
                        asks_.erase(bestAskLevel);   // remove empty level
                    }
                }
            }
        } else
        {
            while (order.quantity > 0 && !bids_.empty() && order.price <= bids_.rbegin()->first)
            {

                auto bestBuyLevel = bids_.rbegin();
                OrderId bidId;
                bool bidExhausted;

                // Code is scoped to prevent danlging reference
                {
                    auto& bestBid = bestBuyLevel->second.front();
                    bidId = bestBid.id;
                    Quantity traded = std::min(bestBid.quantity, order.quantity);
                    order.quantity -= traded;
                    bestBid.quantity -= traded;
                    bidExhausted = (bestBid.quantity == 0);

                    std::cout << "FILL " << bidId << " " << order.id
              << " " << traded << " " << bestBuyLevel->first << "\n";

                }


                if (bidExhausted)
                {
                    orderLookup_.erase(bidId);        // remove from lookup
                    bestBuyLevel->second.pop_front();       // remove from list
                    if (bestBuyLevel->second.empty()) {
                        auto eraseIt = std::next(bestBuyLevel).base();
                        bids_.erase(eraseIt);
                    }
                }
            }
        }

    }