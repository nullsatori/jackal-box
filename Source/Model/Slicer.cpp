#include "Slicer.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace jackal
{

std::vector<int64_t> sliceGrid (int64_t length, int n)
{
    std::vector<int64_t> b;
    if (length <= 0 || n <= 0)
        return b;
    n = std::min<int> (n, static_cast<int> (std::min<int64_t> (length, 64)));
    b.reserve (static_cast<std::size_t> (n) + 1);
    for (int i = 0; i <= n; ++i)
        b.push_back (length * i / n);
    return b;
}

std::vector<int64_t> sliceTransients (const float* mono, int64_t length, double sampleRate)
{
    if (mono == nullptr || length <= 0 || sampleRate <= 0.0)
        return sliceGrid (length, 16);

    auto coeff = [sampleRate] (double ms) { return 1.0 - std::exp (-1.0 / (ms * 0.001 * sampleRate)); };
    const double atk = coeff (2.0), rel = coeff (20.0), slowC = coeff (200.0);
    const double floorLin = std::pow (10.0, -40.0 / 20.0);
    const auto minGap = static_cast<int64_t> (0.030 * sampleRate);

    std::vector<int64_t> b { 0 };
    double env = 0.0, slow = 0.0;
    int64_t last = 0;
    bool armed = false;   // edge-triggered: a hit counts once, when env rises above slow (a hit at 0 is boundary 0)

    for (int64_t i = 0; i < length; ++i)
    {
        const double x = std::fabs (static_cast<double> (mono[i]));
        env += (x > env ? atk : rel) * (x - env);
        slow += slowC * (x - slow);

        const bool above = env > 1.8 * slow && env > floorLin;
        if (! above)
        {
            armed = true;
            continue;
        }
        if (armed && i - last >= minGap)
        {
            if (b.size() >= 64)
                break;
            b.push_back (i);
            last = i;
        }
        armed = false;
    }

    // the very first hit at (or near) zero is the 0 boundary itself
    if (b.size() > 1 && b[1] < minGap)
        b.erase (b.begin() + 1);

    if (b.size() < 2)
        return sliceGrid (length, 16);

    b.push_back (length);
    return b;
}

double guessSourceBars (double lengthSeconds)
{
    if (lengthSeconds <= 0.0)
        return 1.0;

    constexpr std::array<double, 4> kBars { 1.0, 2.0, 4.0, 8.0 };
    for (double bars : kBars)
    {
        const double bpm = bars * 4.0 * 60.0 / lengthSeconds;
        if (bpm >= 100.0 && bpm < 200.0)
            return bars;
    }

    double best = 1.0, bestErr = 1e9;
    for (double bars : kBars)
    {
        const double err = std::fabs (std::log ((bars * 4.0 * 60.0 / lengthSeconds) / 150.0));
        if (err < bestErr)
        {
            bestErr = err;
            best = bars;
        }
    }
    return best;
}

void applySlicing (Sample& sample, SampleSlotSpec& spec)
{
    sample.sourceBars = spec.sourceBars;

    switch (spec.sliceMode)
    {
        case SliceMode::Off:
            sample.sliceStarts.clear();
            break;
        case SliceMode::Grid16:
            sample.sliceStarts = sliceGrid (sample.length, 16);
            break;
        case SliceMode::Grid32:
            sample.sliceStarts = sliceGrid (sample.length, 32);
            break;
        case SliceMode::Transient:
        {
            if (spec.transientBoundaries.size() < 2)
            {
                std::vector<float> mono (static_cast<std::size_t> (sample.length));
                for (int64_t i = 0; i < sample.length; ++i)
                {
                    const auto k = static_cast<std::size_t> (i);
                    mono[k] = sample.numChannels > 1 ? 0.5f * (sample.data[0][k] + sample.data[1][k]) : sample.data[0][k];
                }
                spec.transientBoundaries = sliceTransients (mono.data(), sample.length, sample.sampleRate);
            }

            // sanitize: strictly increasing, starts at 0, ends at length
            std::vector<int64_t> b { 0 };
            for (auto v : spec.transientBoundaries)
                if (v > b.back() && v < sample.length && b.size() < 64)
                    b.push_back (v);
            b.push_back (sample.length);
            sample.sliceStarts = b.size() >= 2 && sample.length > 0 ? b : std::vector<int64_t> {};
            break;
        }
    }
}

} // namespace jackal
