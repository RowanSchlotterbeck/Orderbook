// orderbook_tests.cpp
//
// Orderbook exposes no getters (only addOrder / deleteOrder / printBook), so
// these tests observe behavior through stdout: printBook() dumps resting
// book state, and matchIncoming() logs each fill as
//   "FILL <buyerId> <sellerId> <qty> <price>"
// CoutCapture below redirects std::cout for the duration of a test so we can
// assert on that output without touching any production code.

#include <gtest/gtest.h>
#include <sstream>
#include <iostream>
#include <string>

#include "orderbook.h"

namespace {

class CoutCapture {
public:
    CoutCapture() : old_(std::cout.rdbuf(buffer_.rdbuf())) {}
    ~CoutCapture() { std::cout.rdbuf(old_); }
    std::string str() const { return buffer_.str(); }

private:
    std::ostringstream buffer_;
    std::streambuf* old_;
};

Order makeOrder(OrderId id, Price price, Quantity qty, Side side) {
    return Order{id, price, qty, side};
}

int countOccurrences(const std::string& haystack, const std::string& needle) {
    int count = 0;
    size_t pos = 0;
    while ((pos = haystack.find(needle, pos)) != std::string::npos) {
        ++count;
        pos += needle.size();
    }
    return count;
}

} // namespace

// ---------------------------------------------------------------------
// Resting orders / book printing
// ---------------------------------------------------------------------

TEST(Orderbook, EmptyBookPrintsNoLevels) {
    Orderbook book;
    CoutCapture cap;
    book.printBook();
    std::string out = cap.str();

    EXPECT_NE(out.find("ASKS"), std::string::npos);
    EXPECT_NE(out.find("BIDS"), std::string::npos);
    EXPECT_EQ(out.find(" x "), std::string::npos); // no level lines printed
}

TEST(Orderbook, RestingBuyOrderWithNoMatchAppearsInBids) {
    Orderbook book;
    book.addOrder(makeOrder(1, 100, 5, Side::Buy));

    CoutCapture cap;
    book.printBook();
    std::string out = cap.str();

    EXPECT_NE(out.find("100 x 5 (1 orders)"), std::string::npos);
}

TEST(Orderbook, RestingSellOrderWithNoMatchAppearsInAsks) {
    Orderbook book;
    book.addOrder(makeOrder(1, 100, 5, Side::Sell));

    CoutCapture cap;
    book.printBook();
    std::string out = cap.str();

    auto asksPos = out.find("ASKS");
    auto bidsPos = out.find("BIDS");
    auto levelPos = out.find("100 x 5 (1 orders)");
    ASSERT_NE(levelPos, std::string::npos);
    EXPECT_GT(levelPos, asksPos);
    EXPECT_LT(levelPos, bidsPos); // level line is under ASKS, not BIDS
}

TEST(Orderbook, NonCrossingOrdersBothRestOnRespectiveSides) {
    Orderbook book;
    book.addOrder(makeOrder(1, 99, 5, Side::Buy));
    book.addOrder(makeOrder(2, 101, 5, Side::Sell));

    CoutCapture cap;
    book.printBook();
    std::string out = cap.str();

    EXPECT_EQ(out.find("FILL"), std::string::npos);
    EXPECT_NE(out.find("99 x 5 (1 orders)"), std::string::npos);
    EXPECT_NE(out.find("101 x 5 (1 orders)"), std::string::npos);
}

// ---------------------------------------------------------------------
// Matching
// ---------------------------------------------------------------------

TEST(Orderbook, ExactFullMatchEmptiesBothSides) {
    Orderbook book;
    book.addOrder(makeOrder(1, 100, 5, Side::Sell));

    CoutCapture cap;
    book.addOrder(makeOrder(2, 100, 5, Side::Buy));
    std::string fillOut = cap.str();

    EXPECT_NE(fillOut.find("FILL 2 1 5 100"), std::string::npos);

    CoutCapture bookCap;
    book.printBook();
    std::string bookOut = bookCap.str();
    EXPECT_EQ(bookOut.find(" x "), std::string::npos); // book fully empty
}

TEST(Orderbook, RestingOrderPartiallyFilledLeavesResidualOnBook) {
    Orderbook book;
    book.addOrder(makeOrder(1, 100, 10, Side::Sell));

    CoutCapture cap;
    book.addOrder(makeOrder(2, 100, 4, Side::Buy));
    std::string fillOut = cap.str();
    EXPECT_NE(fillOut.find("FILL 2 1 4 100"), std::string::npos);

    CoutCapture bookCap;
    book.printBook();
    std::string bookOut = bookCap.str();
    // resting ask should have 6 remaining, still one order
    EXPECT_NE(bookOut.find("100 x 6 (1 orders)"), std::string::npos);
    // fully-filled incoming buy should not rest anywhere
    EXPECT_EQ(bookOut.find("100 x 4"), std::string::npos);
}

TEST(Orderbook, IncomingOrderPartiallyFilledRestsWithResidualQuantity) {
    Orderbook book;
    book.addOrder(makeOrder(1, 100, 4, Side::Sell));

    CoutCapture cap;
    book.addOrder(makeOrder(2, 100, 10, Side::Buy));
    std::string fillOut = cap.str();
    EXPECT_NE(fillOut.find("FILL 2 1 4 100"), std::string::npos);

    CoutCapture bookCap;
    book.printBook();
    std::string bookOut = bookCap.str();
    // ask fully consumed and removed
    EXPECT_EQ(bookOut.find("ASKS (low to high):\n  100"), std::string::npos);
    // buyer rests with the remaining 6
    EXPECT_NE(bookOut.find("100 x 6 (1 orders)"), std::string::npos);
}

TEST(Orderbook, PriceTimePriorityFillsOldestOrderFirst) {
    Orderbook book;
    book.addOrder(makeOrder(1, 100, 5, Side::Sell)); // arrives first
    book.addOrder(makeOrder(2, 100, 5, Side::Sell)); // arrives second

    CoutCapture cap;
    book.addOrder(makeOrder(3, 100, 5, Side::Buy));
    std::string fillOut = cap.str();

    // order 1 (FIFO) should be the one matched, not order 2
    EXPECT_NE(fillOut.find("FILL 3 1 5 100"), std::string::npos);
    EXPECT_EQ(fillOut.find("FILL 3 2"), std::string::npos);

    CoutCapture bookCap;
    book.printBook();
    std::string bookOut = bookCap.str();
    EXPECT_NE(bookOut.find("100 x 5 (1 orders)"), std::string::npos); // order 2 still resting
}

TEST(Orderbook, AggressiveOrderSweepsMultiplePriceLevels) {
    Orderbook book;
    book.addOrder(makeOrder(1, 100, 5, Side::Sell));
    book.addOrder(makeOrder(2, 101, 5, Side::Sell));

    CoutCapture cap;
    book.addOrder(makeOrder(3, 101, 8, Side::Buy)); // crosses both levels
    std::string fillOut = cap.str();

    EXPECT_EQ(countOccurrences(fillOut, "FILL"), 2);
    EXPECT_NE(fillOut.find("FILL 3 1 5 100"), std::string::npos); // best price first
    EXPECT_NE(fillOut.find("FILL 3 2 3 101"), std::string::npos); // remainder at next level

    CoutCapture bookCap;
    book.printBook();
    std::string bookOut = bookCap.str();
    EXPECT_NE(bookOut.find("101 x 2 (1 orders)"), std::string::npos); // 5 - 3 left resting
    EXPECT_EQ(bookOut.find("100 x"), std::string::npos); // level 100 fully consumed
}

TEST(Orderbook, BuyMatchesAtBoundaryPriceEquality) {
    Orderbook book;
    book.addOrder(makeOrder(1, 100, 5, Side::Sell));

    CoutCapture cap;
    book.addOrder(makeOrder(2, 100, 5, Side::Buy)); // price == best ask, should cross
    EXPECT_NE(cap.str().find("FILL"), std::string::npos);
}

TEST(Orderbook, SellMatchesAtBoundaryPriceEquality) {
    Orderbook book;
    book.addOrder(makeOrder(1, 100, 5, Side::Buy));

    CoutCapture cap;
    book.addOrder(makeOrder(2, 100, 5, Side::Sell)); // price == best bid, should cross
    EXPECT_NE(cap.str().find("FILL"), std::string::npos);
}

// ---------------------------------------------------------------------
// Deletion
// ---------------------------------------------------------------------

TEST(Orderbook, DeleteOrderRemovesItFromBook) {
    Orderbook book;
    book.addOrder(makeOrder(1, 100, 5, Side::Buy));
    book.deleteOrder(1);

    CoutCapture cap;
    book.printBook();
    EXPECT_EQ(cap.str().find(" x "), std::string::npos);
}

TEST(Orderbook, DeleteNonexistentOrderDoesNotThrowOrModifyBook) {
    Orderbook book;
    book.addOrder(makeOrder(1, 100, 5, Side::Buy));

    EXPECT_NO_THROW(book.deleteOrder(999));

    CoutCapture cap;
    book.printBook();
    EXPECT_NE(cap.str().find("100 x 5 (1 orders)"), std::string::npos); // untouched
}

TEST(Orderbook, DeleteOnlyRemovesTargetedOrderAtSharedLevel) {
    Orderbook book;
    book.addOrder(makeOrder(1, 100, 5, Side::Buy));
    book.addOrder(makeOrder(2, 100, 3, Side::Buy));
    book.deleteOrder(1);

    CoutCapture cap;
    book.printBook();
    std::string out = cap.str();
    EXPECT_NE(out.find("100 x 3 (1 orders)"), std::string::npos);
}

TEST(Orderbook, DeletingLastOrderAtLevelRemovesTheLevelEntirely) {
    Orderbook book;
    book.addOrder(makeOrder(1, 100, 5, Side::Buy));
    book.addOrder(makeOrder(2, 200, 5, Side::Buy));
    book.deleteOrder(1);

    CoutCapture cap;
    book.printBook();
    std::string out = cap.str();
    EXPECT_EQ(out.find("100 x"), std::string::npos);
    EXPECT_NE(out.find("200 x 5 (1 orders)"), std::string::npos);
}

TEST(Orderbook, DeletingResidualAfterPartialFillWorks) {
    Orderbook book;
    book.addOrder(makeOrder(1, 100, 10, Side::Sell));
    book.addOrder(makeOrder(2, 100, 4, Side::Buy)); // leaves order 1 resting with qty 6

    book.deleteOrder(1); // must still be reachable via orderLookup_ after partial fill

    CoutCapture cap;
    book.printBook();
    EXPECT_EQ(cap.str().find(" x "), std::string::npos);
}

// ---------------------------------------------------------------------
// Book ordering / aggregation
// ---------------------------------------------------------------------

TEST(Orderbook, AsksPrintedAscendingBidsPrintedDescending) {
    Orderbook book;
    book.addOrder(makeOrder(1, 102, 1, Side::Sell));
    book.addOrder(makeOrder(2, 100, 1, Side::Sell));
    book.addOrder(makeOrder(3, 101, 1, Side::Sell));

    book.addOrder(makeOrder(4, 97, 1, Side::Buy));
    book.addOrder(makeOrder(5, 99, 1, Side::Buy));
    book.addOrder(makeOrder(6, 98, 1, Side::Buy));

    CoutCapture cap;
    book.printBook();
    std::string out = cap.str();

    size_t p100 = out.find("100 x");
    size_t p101 = out.find("101 x");
    size_t p102 = out.find("102 x");
    ASSERT_NE(p100, std::string::npos);
    ASSERT_NE(p101, std::string::npos);
    ASSERT_NE(p102, std::string::npos);
    EXPECT_LT(p100, p101);
    EXPECT_LT(p101, p102);

    size_t p99 = out.find("99 x");
    size_t p98 = out.find("98 x");
    size_t p97 = out.find("97 x");
    ASSERT_NE(p99, std::string::npos);
    ASSERT_NE(p98, std::string::npos);
    ASSERT_NE(p97, std::string::npos);
    EXPECT_LT(p99, p98);
    EXPECT_LT(p98, p97);
}

TEST(Orderbook, MultipleOrdersAtSameLevelAggregateQuantityAndCount) {
    Orderbook book;
    book.addOrder(makeOrder(1, 100, 5, Side::Buy));
    book.addOrder(makeOrder(2, 100, 3, Side::Buy));
    book.addOrder(makeOrder(3, 100, 2, Side::Buy));

    CoutCapture cap;
    book.printBook();
    std::string out = cap.str();

    EXPECT_NE(out.find("100 x 10 (3 orders)"), std::string::npos);
}
