#pragma once

#include "Engine/ParamIds.h"

#include <algorithm>
#include <array>
#include <string_view>

namespace jackal
{

struct ParamSpecEntry
{
    std::string_view name;    // JSON / p-lock id
    std::string_view label;   // UI label
    float minValue, maxValue, defaultValue;
    std::string_view unit;
};

constexpr std::array<ParamSpecEntry, kNumTrackParams> kTrackParamSpecs { {
    { "volume", "VOL",   -60.f,    12.f, kTrackParamDefaults[0], "dB" },
    { "pan",    "PAN",    -1.f,     1.f, kTrackParamDefaults[1], "" },
    { "pitch",  "PITCH", -24.f,    24.f, kTrackParamDefaults[2], "st" },
    { "decay",  "DECAY",   5.f,  5000.f, kTrackParamDefaults[3], "ms" },
    { "tone",   "TONE",  200.f, 20000.f, kTrackParamDefaults[4], "Hz" },
    { "start",  "START",   0.f,     1.f, kTrackParamDefaults[5], "" },
    { "end",    "END",     0.f,     1.f, kTrackParamDefaults[6], "" },
    { "shakal", "SHAKAL",  0.f,     1.f, kTrackParamDefaults[7], "" },
} };

constexpr ParamSpecEntry kMasterGainSpec { "masterGain", "MASTER", -24.f, 6.f, kMasterGainDefaultDb, "dB" };

inline const ParamSpecEntry& trackParamSpec (TrackParam p) noexcept
{
    return kTrackParamSpecs[static_cast<std::size_t> (p)];
}

inline float clampTrackParam (TrackParam p, float v) noexcept
{
    const auto& s = trackParamSpec (p);
    return std::clamp (v, s.minValue, s.maxValue);
}

// -1 when the name is unknown
inline int trackParamFromName (std::string_view name) noexcept
{
    for (std::size_t i = 0; i < kTrackParamSpecs.size(); ++i)
        if (kTrackParamSpecs[i].name == name)
            return static_cast<int> (i);
    return -1;
}

} // namespace jackal
