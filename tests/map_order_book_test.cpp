#include <gtest/gtest.h>

#include <memory_resource>
#include <ostream>
#include <vector>

#include "map_order_book.h"

// Readable failure messages: GoogleTest finds this via ADL (Fill is in the
// global namespace), otherwise it would print Fill as raw bytes.
void PrintTo(const Fill& f, std::ostream* os) {
    *os << "{maker " << f.makerId << ", taker " << f.takerId << ", price " << f.price
        << ", qty " << f.quantity << "}";
}

namespace {

using Fills = std::vector<Fill>;

// Most state tests check time priority indirectly: after a partial fill,
// cancel the FIFO-front order and look at the depth that remains. The "Fills"
// section at the end checks priority, counterparties and prices directly.

// Every test runs twice: with ordinary new/delete and with a memory pool
// (see the INSTANTIATE_TEST_SUITE_P at the end). The memory source must not
// change behaviour, only speed.
enum class Memory { Malloc, Pool };

class OrderBookTest : public ::testing::TestWithParam<Memory> {
protected:
    bool buy(OrderId id, Price price, Quantity qty) { return book.placeOrder({id, Side::Buy, price, qty}); }
    bool sell(OrderId id, Price price, Quantity qty) { return book.placeOrder({id, Side::Sell, price, qty}); }

    std::pmr::memory_resource* memory() {
        return GetParam() == Memory::Pool ? static_cast<std::pmr::memory_resource*>(&pool)
                                          : std::pmr::new_delete_resource();
    }

    std::pmr::unsynchronized_pool_resource pool;  // declared before book: must outlive it
    MapOrderBook book{memory()};
};

// ---------------------------------------------------------------------------
// Empty book and validation
// ---------------------------------------------------------------------------

TEST_P(OrderBookTest, EmptyBook) {
    EXPECT_FALSE(book.bestBid().has_value());
    EXPECT_FALSE(book.bestAsk().has_value());
    EXPECT_EQ(book.depthAt(Side::Buy, 100), 0u);
    EXPECT_EQ(book.depthAt(Side::Sell, 100), 0u);
    EXPECT_EQ(book.orderCount(), 0u);
}

TEST_P(OrderBookTest, RejectsZeroQuantity) {
    EXPECT_FALSE(buy(1, 100, 0));
    EXPECT_FALSE(sell(2, 100, 0));
    EXPECT_EQ(book.orderCount(), 0u);
}

TEST_P(OrderBookTest, RejectsDuplicateIdOfRestingOrder) {
    ASSERT_TRUE(buy(1, 100, 10));
    EXPECT_FALSE(buy(1, 99, 5));
    EXPECT_FALSE(sell(1, 200, 5));  // other side, would not even cross
    EXPECT_EQ(book.orderCount(), 1u);
    EXPECT_EQ(book.depthAt(Side::Buy, 100), 10u);
    EXPECT_FALSE(book.bestAsk().has_value());
}

TEST_P(OrderBookTest, IdReusableAfterCancel) {
    ASSERT_TRUE(buy(1, 100, 10));
    ASSERT_TRUE(book.cancelOrder(1));
    EXPECT_TRUE(sell(1, 105, 3));
    EXPECT_EQ(book.bestAsk(), Price{105});
}

TEST_P(OrderBookTest, IdReusableAfterFullFill) {
    ASSERT_TRUE(sell(1, 100, 5));
    ASSERT_TRUE(buy(2, 100, 5));
    EXPECT_TRUE(buy(1, 99, 7));
    EXPECT_EQ(book.bestBid(), Price{99});
}

TEST_P(OrderBookTest, UnknownIdOperationsFail) {
    EXPECT_FALSE(book.cancelOrder(42));
    EXPECT_FALSE(book.modifyOrderPrice(42, 100));
    EXPECT_FALSE(book.modifyOrderQuantity(42, 10));
}

// ---------------------------------------------------------------------------
// Resting orders, top of book, depth
// ---------------------------------------------------------------------------

TEST_P(OrderBookTest, BestBidIsHighestBid) {
    buy(1, 98, 1);
    buy(2, 100, 1);
    buy(3, 99, 1);
    EXPECT_EQ(book.bestBid(), Price{100});
    EXPECT_FALSE(book.bestAsk().has_value());
}

TEST_P(OrderBookTest, BestAskIsLowestAsk) {
    sell(1, 103, 1);
    sell(2, 101, 1);
    sell(3, 102, 1);
    EXPECT_EQ(book.bestAsk(), Price{101});
    EXPECT_FALSE(book.bestBid().has_value());
}

TEST_P(OrderBookTest, AskOnlyBookReportsAsk) {  // regression: bestAsk read bids
    sell(1, 105, 5);
    EXPECT_EQ(book.bestAsk(), Price{105});
}

TEST_P(OrderBookTest, BothSidesReportedIndependently) {
    buy(1, 100, 10);
    sell(2, 105, 5);
    EXPECT_EQ(book.bestBid(), Price{100});
    EXPECT_EQ(book.bestAsk(), Price{105});
    EXPECT_EQ(book.orderCount(), 2u);
}

TEST_P(OrderBookTest, DepthAggregatesLevel) {
    buy(1, 100, 10);
    buy(2, 100, 30);
    buy(3, 100, 5);
    sell(4, 102, 10);
    sell(5, 102, 15);
    EXPECT_EQ(book.depthAt(Side::Buy, 100), 45u);
    EXPECT_EQ(book.depthAt(Side::Sell, 102), 25u);
    EXPECT_EQ(book.orderCount(), 5u);
}

TEST_P(OrderBookTest, DepthOfMissingLevelIsZero) {  // regression: compared against bids.end()
    buy(1, 100, 10);
    sell(2, 105, 5);
    EXPECT_EQ(book.depthAt(Side::Buy, 99), 0u);
    EXPECT_EQ(book.depthAt(Side::Buy, 105), 0u);   // price exists, but on the other side
    EXPECT_EQ(book.depthAt(Side::Sell, 110), 0u);
    EXPECT_EQ(book.depthAt(Side::Sell, 100), 0u);  // price exists, but on the other side
}

// ---------------------------------------------------------------------------
// Cancel
// ---------------------------------------------------------------------------

TEST_P(OrderBookTest, CancelRestingBid) {
    buy(1, 100, 10);
    EXPECT_TRUE(book.cancelOrder(1));
    EXPECT_FALSE(book.bestBid().has_value());
    EXPECT_EQ(book.orderCount(), 0u);
}

TEST_P(OrderBookTest, CancelRestingAsk) {  // regression: ask stored in bids -> UB on cancel
    sell(1, 105, 10);
    EXPECT_TRUE(book.cancelOrder(1));
    EXPECT_FALSE(book.bestAsk().has_value());
    EXPECT_EQ(book.orderCount(), 0u);
}

TEST_P(OrderBookTest, CancelTwiceFails) {
    buy(1, 100, 10);
    EXPECT_TRUE(book.cancelOrder(1));
    EXPECT_FALSE(book.cancelOrder(1));
}

TEST_P(OrderBookTest, CancelHeadMiddleTailOfLevel) {
    sell(1, 101, 1);
    sell(2, 101, 2);
    sell(3, 101, 4);
    EXPECT_TRUE(book.cancelOrder(2));
    EXPECT_EQ(book.depthAt(Side::Sell, 101), 5u);
    EXPECT_TRUE(book.cancelOrder(1));
    EXPECT_EQ(book.depthAt(Side::Sell, 101), 4u);
    EXPECT_TRUE(book.cancelOrder(3));
    EXPECT_FALSE(book.bestAsk().has_value());
}

TEST_P(OrderBookTest, CancelBestLevelExposesNextLevel) {
    buy(1, 100, 1);
    buy(2, 99, 1);
    sell(3, 101, 1);
    sell(4, 102, 1);
    book.cancelOrder(1);
    book.cancelOrder(3);
    EXPECT_EQ(book.bestBid(), Price{99});
    EXPECT_EQ(book.bestAsk(), Price{102});
}

TEST_P(OrderBookTest, CancelAfterPartialFillRemovesRemainder) {
    sell(1, 100, 10);
    buy(2, 100, 4);
    ASSERT_EQ(book.depthAt(Side::Sell, 100), 6u);
    EXPECT_TRUE(book.cancelOrder(1));
    EXPECT_FALSE(book.bestAsk().has_value());
    EXPECT_EQ(book.orderCount(), 0u);
}

TEST_P(OrderBookTest, CancelFullyFilledOrderFails) {
    sell(1, 100, 5);
    buy(2, 100, 5);
    EXPECT_FALSE(book.cancelOrder(1));
    EXPECT_FALSE(book.cancelOrder(2));
}

// ---------------------------------------------------------------------------
// Matching: every buy-taker test has a sell-taker mirror
// ---------------------------------------------------------------------------

TEST_P(OrderBookTest, NoCross_BuyBelowAskRests) {
    sell(1, 105, 4);
    buy(2, 100, 10);
    EXPECT_EQ(book.bestBid(), Price{100});
    EXPECT_EQ(book.bestAsk(), Price{105});
    EXPECT_EQ(book.depthAt(Side::Sell, 105), 4u);
    EXPECT_EQ(book.orderCount(), 2u);
}

TEST_P(OrderBookTest, NoCross_SellAboveBidRests) {  // regression: sell matched with <=
    buy(1, 100, 10);
    sell(2, 105, 4);
    EXPECT_EQ(book.bestBid(), Price{100});
    EXPECT_EQ(book.bestAsk(), Price{105});
    EXPECT_EQ(book.depthAt(Side::Buy, 100), 10u);
    EXPECT_EQ(book.orderCount(), 2u);
}

TEST_P(OrderBookTest, ExactFill_BuyTaker) {
    sell(1, 100, 5);
    buy(2, 100, 5);
    EXPECT_FALSE(book.bestBid().has_value());
    EXPECT_FALSE(book.bestAsk().has_value());
    EXPECT_EQ(book.orderCount(), 0u);
}

TEST_P(OrderBookTest, ExactFill_SellTaker) {
    buy(1, 100, 5);
    sell(2, 100, 5);
    EXPECT_FALSE(book.bestBid().has_value());
    EXPECT_FALSE(book.bestAsk().has_value());
    EXPECT_EQ(book.orderCount(), 0u);
}

TEST_P(OrderBookTest, TakerRemainderRests_BuyTaker) {  // scenario A
    sell(1, 100, 4);
    buy(2, 100, 10);
    EXPECT_FALSE(book.bestAsk().has_value());
    EXPECT_EQ(book.bestBid(), Price{100});
    EXPECT_EQ(book.depthAt(Side::Buy, 100), 6u);
    EXPECT_EQ(book.orderCount(), 1u);
}

TEST_P(OrderBookTest, TakerRemainderRests_SellTaker) {
    buy(1, 100, 4);
    sell(2, 100, 10);
    EXPECT_FALSE(book.bestBid().has_value());
    EXPECT_EQ(book.bestAsk(), Price{100});
    EXPECT_EQ(book.depthAt(Side::Sell, 100), 6u);
    EXPECT_EQ(book.orderCount(), 1u);
}

TEST_P(OrderBookTest, MakerPartiallyFilled_BuyTaker) {  // scenario B
    sell(1, 100, 10);
    buy(2, 100, 3);
    EXPECT_FALSE(book.bestBid().has_value());
    EXPECT_EQ(book.depthAt(Side::Sell, 100), 7u);
    EXPECT_EQ(book.orderCount(), 1u);
}

TEST_P(OrderBookTest, MakerPartiallyFilled_SellTaker) {
    buy(1, 100, 10);
    sell(2, 100, 3);
    EXPECT_FALSE(book.bestAsk().has_value());
    EXPECT_EQ(book.depthAt(Side::Buy, 100), 7u);
    EXPECT_EQ(book.orderCount(), 1u);
}

TEST_P(OrderBookTest, AggressiveLimitCrosses_BuyTaker) {
    sell(1, 100, 10);
    buy(2, 103, 4);  // willing to pay more than the ask
    EXPECT_EQ(book.depthAt(Side::Sell, 100), 6u);
    EXPECT_FALSE(book.bestBid().has_value());
}

TEST_P(OrderBookTest, AggressiveLimitCrosses_SellTaker) {  // scenario C
    buy(1, 100, 10);
    sell(2, 95, 4);  // willing to sell for less than the bid
    EXPECT_EQ(book.depthAt(Side::Buy, 100), 6u);
    EXPECT_FALSE(book.bestAsk().has_value());
}

TEST_P(OrderBookTest, MultiLevelSweep_BuyTaker) {
    sell(1, 101, 5);
    sell(2, 102, 5);
    sell(3, 103, 5);
    sell(4, 104, 5);
    buy(5, 103, 12);  // takes 101 and 102, 2 of 103, never touches 104
    EXPECT_EQ(book.bestAsk(), Price{103});
    EXPECT_EQ(book.depthAt(Side::Sell, 101), 0u);
    EXPECT_EQ(book.depthAt(Side::Sell, 102), 0u);
    EXPECT_EQ(book.depthAt(Side::Sell, 103), 3u);
    EXPECT_EQ(book.depthAt(Side::Sell, 104), 5u);
    EXPECT_FALSE(book.bestBid().has_value());
    EXPECT_EQ(book.orderCount(), 2u);
}

TEST_P(OrderBookTest, MultiLevelSweep_SellTaker) {
    buy(1, 104, 5);
    buy(2, 103, 5);
    buy(3, 102, 5);
    buy(4, 101, 5);
    sell(5, 102, 12);  // takes 104 and 103, 2 of 102, never touches 101
    EXPECT_EQ(book.bestBid(), Price{102});
    EXPECT_EQ(book.depthAt(Side::Buy, 104), 0u);
    EXPECT_EQ(book.depthAt(Side::Buy, 103), 0u);
    EXPECT_EQ(book.depthAt(Side::Buy, 102), 3u);
    EXPECT_EQ(book.depthAt(Side::Buy, 101), 5u);
    EXPECT_FALSE(book.bestAsk().has_value());
    EXPECT_EQ(book.orderCount(), 2u);
}

TEST_P(OrderBookTest, SweepStopsAtLimitAndRests_BuyTaker) {
    sell(1, 101, 5);
    sell(2, 103, 5);
    buy(3, 102, 8);  // takes 101, cannot reach 103, rests 3 @ 102
    EXPECT_EQ(book.bestBid(), Price{102});
    EXPECT_EQ(book.depthAt(Side::Buy, 102), 3u);
    EXPECT_EQ(book.bestAsk(), Price{103});
    EXPECT_EQ(book.depthAt(Side::Sell, 103), 5u);
}

TEST_P(OrderBookTest, SweepStopsAtLimitAndRests_SellTaker) {
    buy(1, 103, 5);
    buy(2, 101, 5);
    sell(3, 102, 8);  // takes 103, cannot reach 101, rests 3 @ 102
    EXPECT_EQ(book.bestAsk(), Price{102});
    EXPECT_EQ(book.depthAt(Side::Sell, 102), 3u);
    EXPECT_EQ(book.bestBid(), Price{101});
    EXPECT_EQ(book.depthAt(Side::Buy, 101), 5u);
}

TEST_P(OrderBookTest, FifoWithinLevel_BuyTaker) {
    sell(1, 100, 5);
    sell(2, 100, 5);
    buy(3, 100, 3);  // FIFO: #1 -> 2 left, #2 untouched
    ASSERT_EQ(book.depthAt(Side::Sell, 100), 7u);
    ASSERT_TRUE(book.cancelOrder(1));
    EXPECT_EQ(book.depthAt(Side::Sell, 100), 5u);  // would be 2 if #2 had been hit first
}

TEST_P(OrderBookTest, FifoWithinLevel_SellTaker) {
    buy(1, 100, 5);
    buy(2, 100, 5);
    sell(3, 100, 3);
    ASSERT_EQ(book.depthAt(Side::Buy, 100), 7u);
    ASSERT_TRUE(book.cancelOrder(1));
    EXPECT_EQ(book.depthAt(Side::Buy, 100), 5u);
}

TEST_P(OrderBookTest, FilledMakersLeaveIndex_BuyTaker) {
    sell(1, 100, 2);
    sell(2, 100, 2);
    sell(3, 100, 2);
    buy(4, 100, 5);  // #1, #2 filled; #3 partially
    EXPECT_FALSE(book.cancelOrder(1));
    EXPECT_FALSE(book.cancelOrder(2));
    EXPECT_TRUE(book.cancelOrder(3));
    EXPECT_EQ(book.orderCount(), 0u);
}

TEST_P(OrderBookTest, FilledMakersLeaveIndex_SellTaker) {
    buy(1, 100, 2);
    buy(2, 100, 2);
    buy(3, 100, 2);
    sell(4, 100, 5);
    EXPECT_FALSE(book.cancelOrder(1));
    EXPECT_FALSE(book.cancelOrder(2));
    EXPECT_TRUE(book.cancelOrder(3));
    EXPECT_EQ(book.orderCount(), 0u);
}

TEST_P(OrderBookTest, DuplicateIdDoesNotTrade) {  // scenario E: used to self-trade
    buy(1, 100, 10);
    EXPECT_FALSE(sell(1, 100, 5));
    EXPECT_EQ(book.depthAt(Side::Buy, 100), 10u);
    EXPECT_FALSE(book.bestAsk().has_value());
}

// ---------------------------------------------------------------------------
// Modify price
// ---------------------------------------------------------------------------

TEST_P(OrderBookTest, ModifyPriceMovesBid) {
    buy(1, 100, 10);
    EXPECT_TRUE(book.modifyOrderPrice(1, 98));
    EXPECT_EQ(book.bestBid(), Price{98});
    EXPECT_EQ(book.depthAt(Side::Buy, 100), 0u);
    EXPECT_EQ(book.depthAt(Side::Buy, 98), 10u);
    EXPECT_EQ(book.orderCount(), 1u);
}

TEST_P(OrderBookTest, ModifyPriceMovesAsk) {  // regression: sell side stored Side::Buy
    sell(1, 105, 10);
    EXPECT_TRUE(book.modifyOrderPrice(1, 107));
    EXPECT_EQ(book.bestAsk(), Price{107});
    EXPECT_EQ(book.depthAt(Side::Sell, 105), 0u);
    EXPECT_TRUE(book.cancelOrder(1));
    EXPECT_FALSE(book.bestAsk().has_value());
}

TEST_P(OrderBookTest, ModifyPriceSamePriceKeepsPriority) {
    buy(1, 100, 5);
    buy(2, 100, 5);
    EXPECT_TRUE(book.modifyOrderPrice(1, 100));
    sell(3, 100, 3);  // still hits #1 first
    book.cancelOrder(1);
    EXPECT_EQ(book.depthAt(Side::Buy, 100), 5u);
}

TEST_P(OrderBookTest, ModifyPriceLosesPriority) {
    buy(1, 100, 5);
    buy(2, 100, 5);
    book.modifyOrderPrice(1, 99);
    book.modifyOrderPrice(1, 100);  // back at 100, now behind #2
    sell(3, 100, 3);                // hits #2 first
    book.cancelOrder(2);
    EXPECT_EQ(book.depthAt(Side::Buy, 100), 5u);
}

TEST_P(OrderBookTest, ModifyPriceThatCrossesTrades_Bid) {
    sell(1, 105, 4);
    buy(2, 100, 10);
    EXPECT_TRUE(book.modifyOrderPrice(2, 105));
    EXPECT_FALSE(book.bestAsk().has_value());
    EXPECT_EQ(book.depthAt(Side::Buy, 105), 6u);
    EXPECT_EQ(book.orderCount(), 1u);
}

TEST_P(OrderBookTest, ModifyPriceThatCrossesTrades_Ask) {
    buy(1, 100, 10);
    sell(2, 105, 4);
    EXPECT_TRUE(book.modifyOrderPrice(2, 100));
    EXPECT_FALSE(book.bestAsk().has_value());
    EXPECT_EQ(book.depthAt(Side::Buy, 100), 6u);
    EXPECT_EQ(book.orderCount(), 1u);
}

// ---------------------------------------------------------------------------
// Modify quantity
// ---------------------------------------------------------------------------

TEST_P(OrderBookTest, ModifyQuantityReduceKeepsPriority) {
    sell(1, 101, 5);
    sell(2, 101, 5);
    EXPECT_TRUE(book.modifyOrderQuantity(1, 3));
    EXPECT_EQ(book.depthAt(Side::Sell, 101), 8u);
    buy(3, 101, 2);  // still hits #1 first -> #1 has 1 left
    book.cancelOrder(1);
    EXPECT_EQ(book.depthAt(Side::Sell, 101), 5u);
}

TEST_P(OrderBookTest, ModifyQuantityIncreaseLosesPriority) {
    sell(1, 101, 5);
    sell(2, 101, 5);
    EXPECT_TRUE(book.modifyOrderQuantity(1, 9));
    EXPECT_EQ(book.depthAt(Side::Sell, 101), 14u);
    buy(3, 101, 2);  // #1 moved behind #2 -> hits #2
    book.cancelOrder(2);
    EXPECT_EQ(book.depthAt(Side::Sell, 101), 9u);
}

TEST_P(OrderBookTest, ModifyQuantitySameIsNoOp) {
    buy(1, 100, 5);
    buy(2, 100, 5);
    EXPECT_TRUE(book.modifyOrderQuantity(1, 5));
    sell(3, 100, 2);
    book.cancelOrder(1);
    EXPECT_EQ(book.depthAt(Side::Buy, 100), 5u);
}

TEST_P(OrderBookTest, ModifyQuantityToZeroCancels) {
    buy(1, 100, 5);
    EXPECT_TRUE(book.modifyOrderQuantity(1, 0));
    EXPECT_FALSE(book.bestBid().has_value());
    EXPECT_EQ(book.orderCount(), 0u);
    EXPECT_FALSE(book.cancelOrder(1));
}

TEST_P(OrderBookTest, ModifyQuantityAfterPartialFillUsesRemaining) {
    sell(1, 100, 10);
    buy(2, 100, 6);  // #1 has 4 left
    EXPECT_TRUE(book.modifyOrderQuantity(1, 3));
    EXPECT_EQ(book.depthAt(Side::Sell, 100), 3u);
}

// ---------------------------------------------------------------------------
// Fills: who traded with whom, at what price, in what order.
// Rule: a trade executes at the resting (maker) order's price.
// ---------------------------------------------------------------------------

TEST_P(OrderBookTest, Fills_NoneWhenNothingCrosses) {
    buy(1, 100, 10);
    sell(2, 105, 4);
    book.cancelOrder(1);
    book.modifyOrderQuantity(2, 2);
    book.modifyOrderPrice(2, 106);
    EXPECT_TRUE(book.fills().empty());
}

TEST_P(OrderBookTest, Fills_RejectedOrderDoesNotTrade) {
    buy(1, 100, 10);
    EXPECT_FALSE(sell(1, 100, 5));  // duplicate id
    EXPECT_FALSE(sell(2, 100, 0));  // zero quantity
    EXPECT_TRUE(book.fills().empty());
}

TEST_P(OrderBookTest, Fills_ExactFill_BuyTaker) {
    sell(1, 100, 5);
    buy(2, 100, 5);
    EXPECT_EQ(book.fills(), (Fills{{1, 2, 100, 5}}));
}

TEST_P(OrderBookTest, Fills_ExactFill_SellTaker) {
    buy(1, 100, 5);
    sell(2, 100, 5);
    EXPECT_EQ(book.fills(), (Fills{{1, 2, 100, 5}}));
}

TEST_P(OrderBookTest, Fills_TradeAtMakerPrice_BuyTaker) {
    sell(1, 100, 10);
    buy(2, 103, 4);  // willing to pay 103, but the ask is 100
    EXPECT_EQ(book.fills(), (Fills{{1, 2, 100, 4}}));
}

TEST_P(OrderBookTest, Fills_TradeAtMakerPrice_SellTaker) {
    buy(1, 100, 10);
    sell(2, 95, 4);  // willing to sell at 95, but the bid is 100
    EXPECT_EQ(book.fills(), (Fills{{1, 2, 100, 4}}));
}

TEST_P(OrderBookTest, Fills_TakerRemainderTradesOnlyWhatIsAvailable) {
    sell(1, 100, 4);
    buy(2, 100, 10);  // 6 rest, only 4 trade
    EXPECT_EQ(book.fills(), (Fills{{1, 2, 100, 4}}));
}

TEST_P(OrderBookTest, Fills_FifoWithinLevel_BuyTaker) {
    sell(1, 100, 2);
    sell(2, 100, 2);
    sell(3, 100, 2);
    buy(4, 100, 5);
    EXPECT_EQ(book.fills(), (Fills{{1, 4, 100, 2}, {2, 4, 100, 2}, {3, 4, 100, 1}}));
}

TEST_P(OrderBookTest, Fills_FifoWithinLevel_SellTaker) {
    buy(1, 100, 2);
    buy(2, 100, 2);
    buy(3, 100, 2);
    sell(4, 100, 5);
    EXPECT_EQ(book.fills(), (Fills{{1, 4, 100, 2}, {2, 4, 100, 2}, {3, 4, 100, 1}}));
}

TEST_P(OrderBookTest, Fills_SweepIsPriceThenTime_BuyTaker) {
    sell(1, 102, 5);  // inserted out of price order on purpose
    sell(2, 101, 5);
    sell(3, 102, 5);
    sell(4, 104, 5);
    buy(5, 103, 12);
    EXPECT_EQ(book.fills(), (Fills{{2, 5, 101, 5}, {1, 5, 102, 5}, {3, 5, 102, 2}}));
}

TEST_P(OrderBookTest, Fills_SweepIsPriceThenTime_SellTaker) {
    buy(1, 102, 5);
    buy(2, 103, 5);
    buy(3, 102, 5);
    buy(4, 100, 5);
    sell(5, 101, 12);
    EXPECT_EQ(book.fills(), (Fills{{2, 5, 103, 5}, {1, 5, 102, 5}, {3, 5, 102, 2}}));
}

TEST_P(OrderBookTest, Fills_PartiallyFilledMakerKeepsPriority) {
    sell(1, 100, 10);
    sell(2, 100, 10);
    buy(3, 100, 4);  // #1 -> 6 left, still first in line
    buy(4, 100, 8);  // finishes #1, then 2 from #2
    EXPECT_EQ(book.fills(), (Fills{{1, 3, 100, 4}, {1, 4, 100, 6}, {2, 4, 100, 2}}));
}

TEST_P(OrderBookTest, Fills_AccumulateAcrossOrders) {
    sell(1, 100, 3);
    buy(2, 100, 3);
    buy(3, 99, 4);
    sell(4, 99, 4);
    EXPECT_EQ(book.fills(), (Fills{{1, 2, 100, 3}, {3, 4, 99, 4}}));
}

TEST_P(OrderBookTest, Fills_CrossingModifyPriceIsTaker) {
    buy(1, 100, 10);
    sell(2, 105, 4);
    book.modifyOrderPrice(2, 99);  // #2 now crosses and is the aggressor
    EXPECT_EQ(book.fills(), (Fills{{1, 2, 100, 4}}));
}

TEST_P(OrderBookTest, Fills_ModifyPriceLosesPriority) {
    buy(1, 100, 5);
    buy(2, 100, 5);
    book.modifyOrderPrice(1, 99);
    book.modifyOrderPrice(1, 100);  // back at 100, behind #2
    sell(3, 100, 7);
    EXPECT_EQ(book.fills(), (Fills{{2, 3, 100, 5}, {1, 3, 100, 2}}));
}

TEST_P(OrderBookTest, Fills_ModifyQuantityIncreaseLosesPriority) {
    sell(1, 101, 5);
    sell(2, 101, 5);
    book.modifyOrderQuantity(1, 9);  // moves behind #2
    buy(3, 101, 7);
    EXPECT_EQ(book.fills(), (Fills{{2, 3, 101, 5}, {1, 3, 101, 2}}));
}

TEST_P(OrderBookTest, Fills_ModifyQuantityReduceKeepsPriority) {
    sell(1, 101, 5);
    sell(2, 101, 5);
    book.modifyOrderQuantity(1, 3);  // stays in front
    buy(3, 101, 4);
    EXPECT_EQ(book.fills(), (Fills{{1, 3, 101, 3}, {2, 3, 101, 1}}));
}

INSTANTIATE_TEST_SUITE_P(Memory, OrderBookTest, ::testing::Values(Memory::Malloc, Memory::Pool),
                         [](const ::testing::TestParamInfo<Memory>& info) {
                             return info.param == Memory::Pool ? "Pool" : "Malloc";
                         });

}  // namespace
