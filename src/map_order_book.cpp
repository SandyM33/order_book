#include "map_order_book.h"
#include "structs.h"
#include<algorithm>
#include<iterator>

namespace {
    void addToOrders(std::pmr::list<Order>& orders, const Order& order, std::pmr::unordered_map<OrderId, OrderLocation>& orderMap) {
        orders.push_back(order);
        const auto it = std::prev(orders.end());
        orderMap[order.orderId] = {order.side, order.price, it};
    }

    void matchInOrderLists(std::pmr::list<Order>& orderList,
        Order& order,
        std::pmr::unordered_map<OrderId, OrderLocation>& orders,
        std::vector<Fill>& fills) {
        auto it = orderList.begin();
        while (it != orderList.end() && order.quantity > 0) {
            const Quantity traded = std::min(order.quantity, it->quantity);
            order.quantity -= traded;
            it->quantity -= traded;
            fills.emplace_back(Fill {
                    it->orderId,
                    order.orderId,
                    it->price,
                    traded
                }
            );
            if (it->quantity == 0) {
                orders.erase(it->orderId);
                it = orderList.erase(it);
            } else {
                ++it;
            }
        }
    }
}

MapOrderBook::MapOrderBook(std::pmr::memory_resource* memory)
    : asks(memory), bids(memory), _orders(memory) {}

bool MapOrderBook::placeOrder(Order order) {
    if (order.quantity == 0 || _orders.count(order.orderId) != 0)
        return false;
    if (order.side == Side::Buy) {
        return placeBuyOrder(order);
    }
    return placeSellOrder(order);
}

bool MapOrderBook::cancelOrder(OrderId orderId) {
    const auto orderLocationIt = _orders.find(orderId);
    if (orderLocationIt == _orders.end()) { return false; }
    if (const auto [side, price, listIterator] = orderLocationIt->second; side == Side::Buy) {
        const auto level = bids.find(price);
        level->second.erase(listIterator);
        if (level->second.empty())
            bids.erase(level);
    } else {
        const auto level = asks.find(price);
        level->second.erase(listIterator);
        if (level->second.empty())
            asks.erase(level);
    }
    _orders.erase(orderLocationIt);
    return true;
}

bool MapOrderBook::modifyOrderPrice(OrderId orderId, Price newPrice) {
    const auto orderLocationIt = _orders.find(orderId);
    if (orderLocationIt == _orders.end()) { return false; }
    const auto orderLocation = orderLocationIt->second;
    const auto listIterator = orderLocation.it;
    Order order = *listIterator;

    if (order.price == newPrice) { return true;}
    order.price = newPrice;
    cancelOrder(orderId);
    return placeOrder(order);
}

bool MapOrderBook::modifyOrderQuantity(OrderId orderId, Quantity newQuantity) {
    const auto orderLocationIt = _orders.find(orderId);
    if (orderLocationIt == _orders.end()) { return false; }
    if (newQuantity == 0)
        return cancelOrder(orderId);
    const auto orderLocation = orderLocationIt->second;
    const auto listIterator = orderLocation.it;

    if (newQuantity <= listIterator->quantity) {
        listIterator->quantity = newQuantity;
        return true;
    }
    Order order = *listIterator;
    order.quantity = newQuantity;
    cancelOrder(orderId);
    return placeOrder(order);
}

bool MapOrderBook::placeBuyOrder(Order order) {
    while (order.quantity > 0 && !asks.empty() && asks.begin()->first <= order.price) {
        const auto askLevel = asks.begin();
        auto& lists = askLevel->second;
        matchInOrderLists(lists, order, _orders, _fills);
        if (lists.empty())
            asks.erase(askLevel);
    }
    if (order.quantity > 0) {
        auto& levels = bids[order.price];
        addToOrders(levels, order, _orders);
    }
    return true;
}

bool MapOrderBook::placeSellOrder(Order order) {
    while (order.quantity > 0 && !bids.empty() && bids.begin()->first >= order.price) {
        const auto bidLevel = bids.begin();
        auto& lists = bidLevel->second;
        matchInOrderLists(lists, order, _orders, _fills);
        if (lists.empty())
            bids.erase(bidLevel);
    }
    if (order.quantity > 0) {
        auto& levels = asks[order.price];
        addToOrders(levels, order, _orders);
    }
    return true;
}

std::optional<Price> MapOrderBook::bestBid() const {
    return bids.empty() ? std::optional<Price>() : bids.begin()->first;
}

std::optional<Price> MapOrderBook::bestAsk() const {
    return asks.empty() ? std::optional<Price>() : asks.begin()->first;
}

unsigned long MapOrderBook::depthAt(Side side, Price price) const {
    unsigned long quantity = 0;
    if (side == Side::Buy) {
        const auto it = bids.find(price);
        if (it == bids.end()) return 0;
        for (const Order& order : it->second) {
            quantity += order.quantity;
        }
    } else {
        const auto it = asks.find(price);
        if (it == asks.end()) return 0;
        for (const Order& order : it->second) {
            quantity += order.quantity;
        }
    }
    return quantity;
}

std::size_t MapOrderBook::orderCount() const {
    return _orders.size();
}