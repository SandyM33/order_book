#pragma once
#include <cstddef>
#include <optional>
#include <vector>
#include "structs.h"

class OrderBook {
public:
    virtual ~OrderBook() = default;
    virtual bool placeOrder(Order order) = 0;
    virtual bool cancelOrder(OrderId orderId) = 0;
    virtual bool modifyOrderPrice(OrderId orderId, Price newPrice) = 0;
    virtual bool modifyOrderQuantity(OrderId orderId, Quantity newQuantity) = 0;
    [[nodiscard]] virtual std::optional<Price> bestBid() const = 0;
    [[nodiscard]] virtual std::optional<Price> bestAsk() const = 0;
    [[nodiscard]] virtual unsigned long depthAt(Side side, Price price) const = 0;
    [[nodiscard]] virtual std::size_t orderCount() const = 0;
    [[nodiscard]] virtual const std::vector<Fill>& fills() const = 0;
};


