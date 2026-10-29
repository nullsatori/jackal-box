#pragma once

// Stage 4 test insert: y = x * gain + offset, then a delay of `delay` samples. Never makes a sound.

#include "Engine/Inserts/InsertProcessor.h"

#include <atomic>
#include <vector>

namespace jackal::test
{

struct FakeInsert : InsertProcessor
{
    float gain = 1.f, offset = 0.f;
    std::atomic<int> delay { 0 };
    int prepareCalls = 0, resetCalls = 0;
    double lastSr = 0.0;
    int lastBlock = 0;

    explicit FakeInsert (float g = 1.f, float o = 0.f, int d = 0) : gain (g), offset (o), delay (d)
    {
        for (auto& r : ring)
            r.assign (kRing, 0.f);
    }

    void prepare (double sampleRate, int maxBlockSize) override
    {
        ++prepareCalls;
        lastSr = sampleRate;
        lastBlock = maxBlockSize;
    }

    void reset() noexcept override
    {
        ++resetCalls;
        for (auto& r : ring)
            std::fill (r.begin(), r.end(), 0.f);
    }

    void process (float* left, float* right, int n, const InsertContext&) noexcept override
    {
        const int d = delay.load();
        float* ch[2] = { left, right };
        for (int i = 0; i < n; ++i)
        {
            for (int c = 0; c < 2; ++c)
            {
                auto& r = ring[static_cast<std::size_t> (c)];
                r[static_cast<std::size_t> (w)] = ch[c][i] * gain + offset;
                ch[c][i] = r[static_cast<std::size_t> ((w - d + kRing) % kRing)];
            }
            w = (w + 1) % kRing;
        }
    }

    int latencySamples() const noexcept override { return delay.load(); }

    static constexpr int kRing = 32768;
    std::vector<float> ring[2];
    int w = 0;
};

} // namespace jackal::test
