#pragma once

#include "Engine/EngineCore.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace jackal::test
{

// Click onsets: first sample where |out| > 0 after at least kSilenceRun zero samples.
constexpr int kSilenceRun = 16;

struct OnsetDetector
{
    int zeroRun = kSilenceRun; // stream starts as silence
    int64_t index = 0;
    std::vector<int64_t> onsets;

    void feed (const float* data, int n)
    {
        for (int i = 0; i < n; ++i, ++index)
        {
            if (data[i] != 0.f)
            {
                if (zeroRun >= kSilenceRun)
                    onsets.push_back (index);
                zeroRun = 0;
            }
            else
            {
                ++zeroRun;
            }
        }
    }
};

struct RenderSpec
{
    double bpm = kDefaultBpm;
    double sampleRate = 48000.0;
    int bufferSize = 128;
    double totalSeconds = 30.0;
    int changeAtBlock = -1;   // optional BPM change before this block
    double changeToBpm = 0.0;
};

struct RenderResult
{
    std::vector<int64_t> onsets;
    std::vector<float> output; // channel 0
};

inline RenderResult render (const RenderSpec& spec, bool keepOutput = false)
{
    EngineCore core;
    core.prepare (spec.sampleRate, spec.bufferSize, 0, 2);
    core.params().monitorEnabled = false;
    core.params().metronomeEnabled = true;
    core.params().metronomeGain = 1.0f;

    core.commands().push ({ CommandType::SetBpm, static_cast<float> (spec.bpm), 0 });
    core.commands().push ({ CommandType::Play, 0.f, 0 });

    std::vector<float> left (static_cast<std::size_t> (spec.bufferSize)), right (left.size());
    float* out[2] = { left.data(), right.data() };

    const auto total = static_cast<int64_t> (std::llround (spec.totalSeconds * spec.sampleRate));
    OnsetDetector detector;
    RenderResult result;

    int block = 0;
    for (int64_t done = 0; done < total; ++block)
    {
        if (block == spec.changeAtBlock)
            core.commands().push ({ CommandType::SetBpm, static_cast<float> (spec.changeToBpm), 0 });

        const int n = static_cast<int> (std::min<int64_t> (spec.bufferSize, total - done));
        core.process (nullptr, 0, out, 2, n);
        detector.feed (left.data(), n);

        if (keepOutput)
            result.output.insert (result.output.end(), left.begin(), left.begin() + n);

        done += n;
    }

    result.onsets = std::move (detector.onsets);
    return result;
}

inline std::vector<int64_t> renderClicks (double bpm, double sampleRate, int bufferSize, double totalSeconds)
{
    return render ({ bpm, sampleRate, bufferSize, totalSeconds }).onsets;
}

} // namespace jackal::test
