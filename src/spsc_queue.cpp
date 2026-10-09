#include "spsc_queue.h"

bool SpscQueue::tryPush(const Command& cmd) {
    auto tail = tail_.load(std::memory_order_relaxed);
    auto head = head_.load(std::memory_order_acquire);
    if (tail - head == kCapacity) return false;
    buffer_[tail & kMask] = cmd;
    tail_.store(tail + 1, std::memory_order_release);
    return true;
}

bool SpscQueue::tryPop(Command& out) {
    auto head = head_.load(std::memory_order_relaxed);
    auto tail = tail_.load(std::memory_order_acquire);
    if (head == tail) return false;
    out = buffer_[head & kMask];
    head_.store(head + 1, std::memory_order_release);
    return true;
}