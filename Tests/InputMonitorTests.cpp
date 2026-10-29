#include "Engine/InputMonitor.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace jackal;

namespace
{
    struct Buffers
    {
        std::vector<float> in0, in1, out0, out1;
        const float* in[2];
        float* out[2];

        explicit Buffers (int n, float value)
            : in0 (static_cast<std::size_t> (n), value), in1 (static_cast<std::size_t> (n), 0.f),
              out0 (static_cast<std::size_t> (n), 0.f), out1 (static_cast<std::size_t> (n), 0.f)
        {
            in[0] = in0.data(); in[1] = in1.data();
            out[0] = out0.data(); out[1] = out1.data();
        }

        void clearOut()
        {
            std::fill (out0.begin(), out0.end(), 0.f);
            std::fill (out1.begin(), out1.end(), 0.f);
        }
    };
} // namespace

TEST_CASE ("T7: 0 dB copies input into all outputs", "[monitor]")
{
    InputMonitor m;
    m.prepare (48000.0);
    m.setInputChannel (0);
    Buffers b (64, 0.25f);
    m.process (b.in, 2, b.out, 2, 64, 1.0f, true);

    for (int i = 0; i < 64; ++i)
    {
        REQUIRE (b.out0[static_cast<std::size_t> (i)] == 0.25f);
        REQUIRE (b.out1[static_cast<std::size_t> (i)] == 0.25f);
    }
}

TEST_CASE ("T7: disabled or invalid channel gives silence", "[monitor]")
{
    InputMonitor m;
    m.prepare (48000.0);
    Buffers b (64, 0.5f);

    m.setInputChannel (0);
    m.process (b.in, 2, b.out, 2, 64, 1.0f, false);
    for (float v : b.out0) REQUIRE (v == 0.f);

    for (int ch : { -1, 2, 100 })
    {
        b.clearOut();
        m.setInputChannel (ch);
        m.process (b.in, 2, b.out, 2, 64, 1.0f, true);
        for (float v : b.out0) REQUIRE (v == 0.f);
        for (float v : b.out1) REQUIRE (v == 0.f);
    }

    b.clearOut();
    m.setInputChannel (0);
    m.process (nullptr, 0, b.out, 2, 64, 1.0f, true);
    for (float v : b.out0) REQUIRE (v == 0.f);
}

TEST_CASE ("T7: gain jump 0 -> +12 dB is smoothed", "[monitor]")
{
    InputMonitor m;
    m.prepare (48000.0);
    m.setInputChannel (0);
    const int n = 4800;
    Buffers b (n, 0.1f);

    m.process (b.in, 2, b.out, 2, n, 1.0f, true);
    float last = b.out0.back();

    b.clearOut();
    const float plus12 = std::pow (10.f, 12.f / 20.f);
    m.process (b.in, 2, b.out, 2, n, plus12, true);

    float prev = last;
    for (float v : b.out0)
    {
        REQUIRE (std::fabs (v - prev) <= 0.05f);
        prev = v;
    }
    REQUIRE (std::fabs (b.out0.back() - 0.1f * plus12) < 1e-3f);
}
