#include "Model/Slicer.h"

#include <catch2/catch_test_macros.hpp>

using namespace jackal;

TEST_CASE ("L1: grid, transients and bar guessing", "[slicer]")
{
    const auto g = sliceGrid (4800, 16);
    REQUIRE (g.size() == 17);
    for (int i = 0; i <= 16; ++i)
        REQUIRE (g[static_cast<std::size_t> (i)] == 300 * i);

    const int64_t len = 2 * 48000;
    std::vector<float> x (static_cast<std::size_t> (len), 0.f);
    for (int h = 0; h < 8; ++h)
        for (int i = 0; i < 960; ++i)   // 20 ms burst every 0.25 s
            x[static_cast<std::size_t> (h * 12000 + i)] = (i % 2 == 0 ? 0.8f : -0.8f) * (1.f - static_cast<float> (i) / 960.f);
    const auto t = sliceTransients (x.data(), len, 48000.0);
    REQUIRE (t.size() == 9);   // 8 slices: 8 starts + end
    REQUIRE (t.front() == 0);
    REQUIRE (t.back() == len);
    for (int h = 1; h < 8; ++h)
        REQUIRE (std::llabs (t[static_cast<std::size_t> (h)] - h * 12000) < 200);

    std::vector<float> silence (static_cast<std::size_t> (len), 0.f);
    REQUIRE (sliceTransients (silence.data(), len, 48000.0) == sliceGrid (len, 16));

    REQUIRE (guessSourceBars (2.79) == 2.0);
    REQUIRE (guessSourceBars (1.39) == 1.0);
    REQUIRE (guessSourceBars (5.58) == 4.0);
}
