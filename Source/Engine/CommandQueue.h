#pragma once

#include "EngineTypes.h"

#include <array>
#include <atomic>
#include <cstddef>

namespace jackal
{

// Single-producer (UI) / single-consumer (audio) ring buffer, atomics only, no locks.
// Holds up to kCommandQueueCapacity - 1 commands.
class CommandQueue
{
public:
    bool push (const Command& command) noexcept
    {
        const auto tail = tail_.load (std::memory_order_relaxed);
        const auto next = (tail + 1) & kMask;

        if (next == head_.load (std::memory_order_acquire))
            return false; // full: caller decides what to do, never blocks

        buffer_[tail] = command;
        tail_.store (next, std::memory_order_release);
        return true;
    }

    bool pop (Command& command) noexcept
    {
        const auto head = head_.load (std::memory_order_relaxed);

        if (head == tail_.load (std::memory_order_acquire))
            return false;

        command = buffer_[head];
        head_.store ((head + 1) & kMask, std::memory_order_release);
        return true;
    }

private:
    static constexpr std::size_t kCapacity = static_cast<std::size_t> (kCommandQueueCapacity);
    static_assert ((kCapacity & (kCapacity - 1)) == 0, "capacity must be a power of two");
    static constexpr std::size_t kMask = kCapacity - 1;

    std::array<Command, kCapacity> buffer_ {};
    std::atomic<std::size_t> head_ { 0 };
    std::atomic<std::size_t> tail_ { 0 };
};

} // namespace jackal
