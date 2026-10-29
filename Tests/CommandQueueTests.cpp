#include "Engine/CommandQueue.h"

#include <catch2/catch_test_macros.hpp>

#include <thread>
#include <vector>

using namespace jackal;

TEST_CASE ("T6: FIFO order", "[queue]")
{
    CommandQueue q;
    for (int i = 0; i < 10; ++i)
        REQUIRE (q.push ({ CommandType::SetInputChannel, 0.f, i }));

    Command c;
    for (int i = 0; i < 10; ++i)
    {
        REQUIRE (q.pop (c));
        REQUIRE (c.ivalue == i);
    }
    REQUIRE_FALSE (q.pop (c));
}

TEST_CASE ("T6: full queue rejects push without corrupting data", "[queue]")
{
    CommandQueue q;
    int pushed = 0;
    while (q.push ({ CommandType::SetInputChannel, 0.f, pushed }))
        ++pushed;

    REQUIRE (pushed == kCommandQueueCapacity - 1);
    REQUIRE_FALSE (q.push ({ CommandType::SetInputChannel, 0.f, 9999 }));

    Command c;
    for (int i = 0; i < pushed; ++i)
    {
        REQUIRE (q.pop (c));
        REQUIRE (c.ivalue == i);
    }
    REQUIRE_FALSE (q.pop (c));
}

TEST_CASE ("T6: one million commands across two threads, no loss, no duplicates", "[queue]")
{
    CommandQueue q;
    constexpr int kCount = 1'000'000;

    std::thread producer ([&q] {
        for (int i = 0; i < kCount; ++i)
            while (! q.push ({ CommandType::SetInputChannel, 0.f, i }))
                std::this_thread::yield();
    });

    int expected = 0;
    bool ordered = true;
    Command c;
    while (expected < kCount)
    {
        if (q.pop (c))
        {
            if (c.ivalue != expected)
                ordered = false;
            ++expected;
        }
        else
        {
            std::this_thread::yield();
        }
    }

    producer.join();
    REQUIRE (ordered);
    REQUIRE (expected == kCount);
    REQUIRE_FALSE (q.pop (c));
}
