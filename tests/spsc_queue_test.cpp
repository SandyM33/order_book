#include <gtest/gtest.h>

#include <memory>
#include <thread>
#include <variant>
#include "spsc_queue.h"

namespace {

constexpr std::size_t kCapacity = SpscQueue::kCapacity;

Command cancelOf(OrderId id) { return CancelCommand{id}; }

OrderId idOf(const Command& c) {
    const auto* cancel = std::get_if<CancelCommand>(&c);
    return cancel ? cancel->orderId : ~OrderId{0};
}

// The queue holds a 40 KB buffer; keep it off the test's stack.
class SpscQueueTest : public ::testing::Test {
protected:
    std::unique_ptr<SpscQueue> queue = std::make_unique<SpscQueue>();
};

// ---------------------------------------------------------------------------
// Single thread
// ---------------------------------------------------------------------------

TEST_F(SpscQueueTest, PopFromEmptyFails) {
    Command out;
    EXPECT_FALSE(queue->tryPop(out));
}

TEST_F(SpscQueueTest, PopReturnsItemsInPushOrder) {
    for (OrderId i = 0; i < 10; ++i)
        ASSERT_TRUE(queue->tryPush(cancelOf(i)));
    Command out;
    for (OrderId i = 0; i < 10; ++i) {
        ASSERT_TRUE(queue->tryPop(out));
        EXPECT_EQ(idOf(out), i);
    }
    EXPECT_FALSE(queue->tryPop(out));
}

TEST_F(SpscQueueTest, AcceptsExactlyCapacityItems) {  // regression: full check was inverted
    std::size_t accepted = 0;
    for (OrderId i = 0; i < kCapacity + 500; ++i)
        accepted += queue->tryPush(cancelOf(i)) ? 1 : 0;
    EXPECT_EQ(accepted, kCapacity);

    Command out;  // nothing was overwritten: the oldest item is still first
    ASSERT_TRUE(queue->tryPop(out));
    EXPECT_EQ(idOf(out), 0u);
}

TEST_F(SpscQueueTest, PopFreesASlot) {
    for (OrderId i = 0; i < kCapacity; ++i)
        ASSERT_TRUE(queue->tryPush(cancelOf(i)));
    EXPECT_FALSE(queue->tryPush(cancelOf(9999)));
    Command out;
    ASSERT_TRUE(queue->tryPop(out));
    EXPECT_TRUE(queue->tryPush(cancelOf(9999)));
    EXPECT_FALSE(queue->tryPush(cancelOf(10000)));
}

TEST_F(SpscQueueTest, WrapsAroundManyTimes) {
    Command out;
    OrderId next = 0;
    OrderId expected = 0;
    for (int round = 0; round < 50; ++round) {  // ~37 laps of the ring
        for (int i = 0; i < 700; ++i)
            ASSERT_TRUE(queue->tryPush(cancelOf(next++)));
        for (int i = 0; i < 700; ++i) {
            ASSERT_TRUE(queue->tryPop(out));
            ASSERT_EQ(idOf(out), expected++);
        }
    }
    EXPECT_FALSE(queue->tryPop(out));
}

TEST_F(SpscQueueTest, CarriesEveryCommandType) {
    ASSERT_TRUE(queue->tryPush(AddCommand{Order{7, Side::Sell, 105, 3}}));
    ASSERT_TRUE(queue->tryPush(CancelCommand{8}));
    ASSERT_TRUE(queue->tryPush(ModifyPriceCommand{9, 101}));
    ASSERT_TRUE(queue->tryPush(ModifyQuantityCommand{10, 4}));
    ASSERT_TRUE(queue->tryPush(StopCommand{}));

    Command out;
    ASSERT_TRUE(queue->tryPop(out));
    const auto* add = std::get_if<AddCommand>(&out);
    ASSERT_NE(add, nullptr);
    EXPECT_EQ(add->order.orderId, 7u);
    EXPECT_EQ(add->order.side, Side::Sell);
    EXPECT_EQ(add->order.price, 105u);
    EXPECT_EQ(add->order.quantity, 3u);

    ASSERT_TRUE(queue->tryPop(out));
    ASSERT_TRUE(std::holds_alternative<CancelCommand>(out));
    EXPECT_EQ(std::get<CancelCommand>(out).orderId, 8u);

    ASSERT_TRUE(queue->tryPop(out));
    ASSERT_TRUE(std::holds_alternative<ModifyPriceCommand>(out));
    EXPECT_EQ(std::get<ModifyPriceCommand>(out).orderId, 9u);
    EXPECT_EQ(std::get<ModifyPriceCommand>(out).newPrice, 101u);

    ASSERT_TRUE(queue->tryPop(out));
    ASSERT_TRUE(std::holds_alternative<ModifyQuantityCommand>(out));
    EXPECT_EQ(std::get<ModifyQuantityCommand>(out).orderId, 10u);
    EXPECT_EQ(std::get<ModifyQuantityCommand>(out).newQuantity, 4u);

    ASSERT_TRUE(queue->tryPop(out));
    EXPECT_TRUE(std::holds_alternative<StopCommand>(out));
}

// ---------------------------------------------------------------------------
// Two threads: the property the queue exists for. Run under the tsan preset
// to also check the acquire/release ordering for data races.
// ---------------------------------------------------------------------------

TEST_F(SpscQueueTest, TwoThreadsReceiveEveryItemInOrder) {
    constexpr OrderId kItems = 1'000'000;
    std::thread producer([this] {
        for (OrderId i = 0; i < kItems; ++i)
            while (!queue->tryPush(cancelOf(i))) {}
    });

    Command out;
    OrderId mismatches = 0;
    for (OrderId expected = 0; expected < kItems; ++expected) {
        while (!queue->tryPop(out)) {}
        if (idOf(out) != expected) ++mismatches;
    }
    producer.join();

    EXPECT_EQ(mismatches, 0u);
    EXPECT_FALSE(queue->tryPop(out));
}

}  // namespace
