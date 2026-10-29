#pragma once

#include <cmath>

namespace jackal
{

// Transposed direct form II, RBJ cookbook lowpass.
struct Biquad
{
    float b0 = 1.f, b1 = 0.f, b2 = 0.f, a1 = 0.f, a2 = 0.f;
    float s1 = 0.f, s2 = 0.f;

    void setLowpass (double sampleRate, double hz, double q) noexcept
    {
        constexpr double kPi = 3.14159265358979323846;
        const double w0 = 2.0 * kPi * hz / sampleRate;
        const double cw = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;
        b0 = static_cast<float> ((1.0 - cw) * 0.5 / a0);
        b1 = static_cast<float> ((1.0 - cw) / a0);
        b2 = b0;
        a1 = static_cast<float> (-2.0 * cw / a0);
        a2 = static_cast<float> ((1.0 - alpha) / a0);
    }

    void reset() noexcept { s1 = s2 = 0.f; }

    void flushDenormals() noexcept
    {
        if (std::fabs (s1) < 1e-15f) s1 = 0.f;
        if (std::fabs (s2) < 1e-15f) s2 = 0.f;
    }

    float process (float x) noexcept
    {
        const float y = b0 * x + s1;
        s1 = b1 * x - a1 * y + s2;
        s2 = b2 * x - a2 * y;
        return y;
    }
};

} // namespace jackal
