#pragma once

#include <thread>
#include "spsc_queue.h"
#include "order_book.h"

// Runs an order book on its own thread, fed through a single-producer /
// single-consumer queue. Only ONE thread may call the submit* functions.
//
// The engine does not own the book. The caller keeps it alive for the
// engine's whole lifetime and must not touch it while the engine is running
// (the engine thread is writing to it). After stop() it is safe to read:
// stop() joins the engine thread, so everything it did is visible.
class MatchingEngine {
public:
    explicit MatchingEngine(OrderBook& book) : book_(book) {}

    ~MatchingEngine();

    void start();

    void stop();

    void submitAdd(const Order& order);

    void submitCancel(OrderId orderId);

    void submitModifyPrice(OrderId orderId, Price newPrice);

    void submitModifyQuantity(OrderId orderId, Quantity newQuantity);
private:
    void push(const Command& command);
    void run();
    SpscQueue queue_;
    OrderBook& book_;
    std::thread worker_;
};
