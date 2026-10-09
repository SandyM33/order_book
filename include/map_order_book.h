#pragma once

#include <map>
#include "order_book.h"
#include "structs.h"

class MapOrderBook final : public OrderBook {
public:
    explicit MapOrderBook(std::pmr::memory_resource* memory = std::pmr::new_delete_resource());

    bool placeOrder(Order order) override;
    bool cancelOrder(OrderId orderId) override;
    bool modifyOrderPrice(OrderId orderId, Price newPrice) override;
    bool modifyOrderQuantity(OrderId orderId, Quantity newQuantity) override;
    [[nodiscard]] std::optional<Price> bestBid() const override;
    [[nodiscard]] std::optional<Price> bestAsk() const override;
    [[nodiscard]] unsigned long depthAt(Side side, Price price) const override;
    [[nodiscard]] std::size_t orderCount() const override;
    [[nodiscard]] const std::vector<Fill>& fills() const override { return _fills; }
private:
    std::vector<Fill> _fills;
    bool placeBuyOrder(Order order);
    bool placeSellOrder(Order order);
    std::pmr::map<Price, std::pmr::list<Order>> asks; //All these sell orders are sorted low to high by price
    std::pmr::map<Price, std::pmr::list<Order>, std::greater<>> bids; //All these buy orders are sorted high to low by price
    std::pmr::unordered_map<OrderId, OrderLocation> _orders;
};
