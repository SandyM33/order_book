#include <gtest/gtest.h>

#include <random>
#include <vector>
#include "matching_engine.h"

#include "map_order_book.h"

namespace {

// Each test owns the book and hands it to the engine. Declare the book first:
// members/locals are destroyed in reverse order, so the engine (whose
// destructor stops the thread that uses the book) goes away before the book.
// Tests read the book only after stop(), which joins the engine thread.

// ---------------------------------------------------------------------------
// Lifetime
// ---------------------------------------------------------------------------

TEST(MatchingEngineTest, StopWithoutStartIsSafe) {
    MapOrderBook book;
    MatchingEngine engine(book);
    engine.stop();
    engine.stop();
}

TEST(MatchingEngineTest, DestructorStopsARunningEngine) {
    MapOrderBook book;
    MatchingEngine engine(book);
    engine.start();
    engine.submitAdd(Order{1, Side::Buy, 100, 5});
    // No stop(): the destructor must stop and join instead of hanging or terminating.
}

TEST(MatchingEngineTest, CanRestartAfterStop) {
    MapOrderBook book;
    MatchingEngine engine(book);
    engine.start();
    engine.submitAdd(Order{1, Side::Sell, 100, 5});
    engine.stop();
    engine.start();
    engine.submitAdd(Order{2, Side::Buy, 100, 5});
    engine.stop();
    ASSERT_EQ(book.fills().size(), 1u);
    EXPECT_TRUE(book.fills()[0] == (Fill{1, 2, 100, 5}));
}

TEST(MatchingEngineTest, CommandsSubmittedBeforeStartAreProcessed) {
    MapOrderBook book;
    MatchingEngine engine(book);
    engine.submitAdd(Order{1, Side::Buy, 100, 5});  // queued; fewer than the queue's capacity
    engine.submitAdd(Order{2, Side::Buy, 99, 5});
    engine.start();
    engine.stop();
    EXPECT_EQ(book.orderCount(), 2u);
    EXPECT_EQ(book.bestBid(), Price{100});
}

// ---------------------------------------------------------------------------
// Every command type reaches the right book method
// ---------------------------------------------------------------------------

TEST(MatchingEngineTest, AppliesEveryCommandType) {
    MapOrderBook book;
    MatchingEngine engine(book);
    engine.start();
    engine.submitAdd(Order{1, Side::Sell, 100, 10});
    engine.submitAdd(Order{2, Side::Sell, 101, 5});
    engine.submitModifyPrice(2, 100);     // #2 moves to 100, behind #1
    engine.submitModifyQuantity(1, 4);    // #1 reduced in place, keeps priority
    engine.submitAdd(Order{3, Side::Buy, 100, 6});  // takes 4 from #1, then 2 from #2
    engine.submitCancel(2);               // removes #2's remaining 3
    engine.stop();

    const std::vector<Fill> expected{{1, 3, 100, 4}, {2, 3, 100, 2}};
    EXPECT_TRUE(book.fills() == expected);
    EXPECT_EQ(book.orderCount(), 0u);
    EXPECT_FALSE(book.bestAsk().has_value());
    EXPECT_FALSE(book.bestBid().has_value());
}

// Random mix of all four command types, applied through the engine (two
// threads, through the queue) and directly to a second book: the results
// must be identical.
TEST(MatchingEngineTest, MatchesDirectBookOnRandomCommands) {
    MapOrderBook book;
    MatchingEngine engine(book);
    MapOrderBook direct;
    std::mt19937_64 rng(5);
    OrderId next = 0;

    engine.start();
    for (int i = 0; i < 200'000; ++i) {
        const auto roll = rng() % 100;
        const OrderId someId = next ? rng() % next : 0;
        if (roll < 50) {
            const Order o{next++, rng() % 2 ? Side::Buy : Side::Sell, 95 + rng() % 11,
                          static_cast<Quantity>(1 + rng() % 50)};
            engine.submitAdd(o);
            direct.placeOrder(o);
        } else if (roll < 80) {
            engine.submitCancel(someId);
            direct.cancelOrder(someId);
        } else if (roll < 90) {
            const Price p = 95 + rng() % 11;
            engine.submitModifyPrice(someId, p);
            direct.modifyOrderPrice(someId, p);
        } else {
            const auto q = static_cast<Quantity>(rng() % 60);
            engine.submitModifyQuantity(someId, q);
            direct.modifyOrderQuantity(someId, q);
        }
    }
    engine.stop();

    EXPECT_GT(direct.fills().size(), 0u);
    EXPECT_TRUE(book.fills() == direct.fills());
    EXPECT_EQ(book.orderCount(), direct.orderCount());
    EXPECT_EQ(book.bestBid(), direct.bestBid());
    EXPECT_EQ(book.bestAsk(), direct.bestAsk());
}

}  // namespace
