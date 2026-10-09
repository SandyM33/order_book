#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include "structs.h"

class SpscQueue {
public:
    static constexpr std::size_t kCapacity = 1024;

    bool tryPush(const Command& cmd);
    bool tryPop(Command& out);
private:
    static constexpr std::size_t kMask = kCapacity - 1;

    std::array<Command, kCapacity> buffer_{};
    alignas(64) std::atomic<std::size_t> head_{0};
    alignas(64) std::atomic<std::size_t> tail_{0};
};
