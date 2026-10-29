#include "Engine/LevelMeter.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <numbers>
#include <vector>

using namespace jackal;

TEST_CASE ("T8: sine 0.5 gives peak 0.5 and rms 0.3536", "[meter]")
{
    const int n = 48000;
    std::vector<float> sine (static_cast<std::size_t> (n));
    for (int i = 0; i < n; ++i)
        sine[static_cast<std::size_t> (i)] = 0.5f * static_cast<float> (std::sin (2.0 * std::numbers::pi * 1000.0 * i / 48000.0 + 0.5 * std::numbers::pi));

    const float* ch[1] = { sine.data() };
    LevelMeter meter;
    meter.update (ch, 1, n);

    REQUIRE (meter.peak() == Catch::Approx (0.5f).margin (1e-3));
    REQUIRE (meter.rms() == Catch::Approx (0.3536f).margin (1e-3));
}

TEST_CASE ("T8: silence gives zero", "[meter]")
{
    std::vector<float> zeros (512, 0.f);
    const float* ch[1] = { zeros.data() };
    LevelMeter meter;
    meter.update (ch, 1, 512);
    REQUIRE (meter.peak() == 0.f);
    REQUIRE (meter.rms() == 0.f);

    meter.update (nullptr, 0, 512);
    REQUIRE (meter.peak() == 0.f);
}
