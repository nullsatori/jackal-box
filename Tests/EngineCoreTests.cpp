#include "Engine/EngineCore.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <vector>

using namespace jackal;

namespace
{
    std::vector<float> run (float monitorDb, float clickGain)
    {
        EngineCore core;
        const int block = 256;
        core.prepare (48000.0, block, 1, 2);
        core.params().monitorGainDb = monitorDb;
        core.params().metronomeGain = clickGain;
        core.commands().push ({ CommandType::SetInputChannel, 0.f, 0 });
        core.commands().push ({ CommandType::Play, 0.f, 0 });

        std::vector<float> input (block), l (block), r (block), all;
        const float* in[1] = { input.data() };
        float* out[2] = { l.data(), r.data() };

        for (int b = 0; b < 400; ++b)
        {
            for (int i = 0; i < block; ++i)
                input[static_cast<std::size_t> (i)] = ((b * block + i) % 2 == 0) ? 1.0f : -1.0f;
            core.process (in, 1, out, 2, block);
            all.insert (all.end(), l.begin(), l.end());
            all.insert (all.end(), r.begin(), r.end());
        }
        return all;
    }
} // namespace

TEST_CASE ("T9: deterministic, finite and bounded output", "[core]")
{
    const auto a = run (12.f, 1.0f);
    const auto b = run (12.f, 1.0f);
    REQUIRE (a == b);

    for (float v : a)
    {
        REQUIRE (std::isfinite (v));
        REQUIRE (std::fabs (v) <= 4.0f + 1.0f);
    }
}

TEST_CASE ("T9: process with no inputs does not crash", "[core]")
{
    EngineCore core;
    core.prepare (48000.0, 128, 0, 2);
    std::vector<float> l (128), r (128);
    float* out[2] = { l.data(), r.data() };
    core.commands().push ({ CommandType::Play, 0.f, 0 });
    core.process (nullptr, 0, out, 2, 128);
    REQUIRE (core.inputMeter().peak() == 0.f);
}
