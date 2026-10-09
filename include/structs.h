#pragma once
#include <list>
#include <memory_resource>
#include <type_traits>
#include <variant>

using Price = unsigned long;
using OrderId = unsigned long;
using Quantity = unsigned int;

enum class Side {
    Buy,
    Sell
};

struct Order {
    OrderId orderId;
    Side side;
    Price price;
    Quantity quantity;
};

using OrderListIterator = std::pmr::list<Order>::iterator;

struct OrderLocation {
    Side side;
    Price price;
    OrderListIterator it;
};

struct AddCommand            { Order order; };
struct CancelCommand         { OrderId orderId; };
struct ModifyPriceCommand    { OrderId orderId; Price newPrice; };
struct ModifyQuantityCommand { OrderId orderId; Quantity newQuantity; };
struct StopCommand           {};

using Command = std::variant<AddCommand, CancelCommand, ModifyPriceCommand,
                             ModifyQuantityCommand, StopCommand>;

struct Fill {
    OrderId  makerId;
    OrderId  takerId;
    Price    price;
    Quantity quantity;

    bool operator==(const Fill& other) const {
        return makerId == other.makerId && takerId == other.takerId &&
               price == other.price && quantity == other.quantity;
    }
};