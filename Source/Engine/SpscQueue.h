#pragma once

#include <array>
#include <atomic>
#include <cstddef>

namespace jackal
{

// Single-producer / single-consumer ring, atomics only. Holds Capacity - 1 items.
template <typename T, std::size_t Capacity>
class SpscQueue
{
    static_assert ((Capacity & (Capacity - 1)) == 0, "capacity must be a power of two");

public:
    bool push (const T& item) noexcept
    {
        const auto tail = tail_.load (std::memory_order_relaxed);
        const auto next = (tail + 1) & kMask;

        if (next == head_.load (std::memory_order_acquire))
            return false;

        buffer_[tail] = item;
        tail_.store (next, std::memory_order_release);
        return true;
    }

    bool pop (T& item) noexcept
    {
        const auto head = head_.load (std::memory_order_relaxed);

        if (head == tail_.load (std::memory_order_acquire))
            return false;

        item = buffer_[head];
        head_.store ((head + 1) & kMask, std::memory_order_release);
        return true;
    }

    std::size_t sizeApprox() const noexcept
    {
        return (tail_.load (std::memory_order_acquire) - head_.load (std::memory_order_acquire)) & kMask;
    }

private:
    static constexpr std::size_t kMask = Capacity - 1;

    std::array<T, Capacity> buffer_ {};
    std::atomic<std::size_t> head_ { 0 };
    std::atomic<std::size_t> tail_ { 0 };
};

} // namespace jackal
