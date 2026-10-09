#include "matching_engine.h"

#include <variant>

namespace {
    struct ApplyToBook {
        OrderBook& book;

        bool operator()(const AddCommand& c) const { book.placeOrder(c.order); return true; }
        bool operator()(const CancelCommand& c) const { book.cancelOrder(c.orderId); return true; }
        bool operator()(const ModifyPriceCommand& c) const { book.modifyOrderPrice(c.orderId, c.newPrice); return true; }
        bool operator()(const ModifyQuantityCommand& c) const { book.modifyOrderQuantity(c.orderId, c.newQuantity); return true; }
        bool operator()(const StopCommand&) const { return false; }
    };
}

MatchingEngine::~MatchingEngine() {
    stop();
}

void MatchingEngine::start() {
    worker_ = std::thread([this] {
        run();
    });
}

void MatchingEngine::stop() {
    if (!worker_.joinable())
        return;
    push(StopCommand{});
    worker_.join();
}

void MatchingEngine::submitAdd(const Order& order) {
    push(AddCommand{order});
}

void MatchingEngine::submitCancel(OrderId orderId) {
    push(CancelCommand{orderId});
}

void MatchingEngine::submitModifyPrice(OrderId orderId, Price newPrice) {
    push(ModifyPriceCommand{orderId, newPrice});
}

void MatchingEngine::submitModifyQuantity(OrderId orderId, Quantity newQuantity) {
    push(ModifyQuantityCommand{orderId, newQuantity});
}

void MatchingEngine::push(const Command& command) {
    while (!queue_.tryPush(command)) {}
}

void MatchingEngine::run() {
    Command command;
    while (true) {
        while (!queue_.tryPop(command)) {}
        if (!std::visit(ApplyToBook{book_}, command))
            return;
    }
}
