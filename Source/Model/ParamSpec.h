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

// ---- SHAKAL chains (stage 3)

enum class ParamScale : uint8_t { Linear, Log, Toggle, Choice };

struct FxParamSpecEntry
{
    std::string_view name, label;
    float minValue, maxValue, defaultValue;
    std::string_view unit;
    ParamScale scale;
    std::array<std::string_view, 3> choices;   // JSON strings for Choice params
};

constexpr std::array<FxParamSpecEntry, kNumFxParams> kFxParamSpecs { {
    { "fxBypass",     "BYP",   0.f,     1.f,     kFxParamDefaults[0],  "",   ParamScale::Toggle, {} },
    { "fxPreset",     "PRESET",0.f,     2.f,     kFxParamDefaults[1],  "",   ParamScale::Choice, { "iron", "net", "mem" } },
    { "driveOn",      "ON",    0.f,     1.f,     kFxParamDefaults[2],  "",   ParamScale::Toggle, {} },
    { "driveDb",      "dB",    0.f,     36.f,    kFxParamDefaults[3],  "dB", ParamScale::Linear, {} },
    { "driveMode",    "MODE",  0.f,     1.f,     kFxParamDefaults[4],  "",   ParamScale::Choice, { "clip", "wrap" } },
    { "sp12On",       "ON",    0.f,     1.f,     kFxParamDefaults[5],  "",   ParamScale::Toggle, {} },
    { "sp12FilterHz", "FILT",  3000.f,  20000.f, kFxParamDefaults[6],  "Hz", ParamScale::Log,    {} },
    { "decimOn",      "ON",    0.f,     1.f,     kFxParamDefaults[7],  "",   ParamScale::Toggle, {} },
    { "decimRateHz",  "RATE",  500.f,   48000.f, kFxParamDefaults[8],  "Hz", ParamScale::Log,    {} },
    { "crushOn",      "ON",    0.f,     1.f,     kFxParamDefaults[9],  "",   ParamScale::Toggle, {} },
    { "crushBits",    "BITS",  1.f,     16.f,    kFxParamDefaults[10], "",   ParamScale::Linear, {} },
    { "crushMulaw",   "LAW",   0.f,     1.f,     kFxParamDefaults[11], "",   ParamScale::Toggle, {} },
    { "flipOn",       "ON",    0.f,     1.f,     kFxParamDefaults[12], "",   ParamScale::Toggle, {} },
    { "flipProb",     "PROB",  0.f,     1.f,     kFxParamDefaults[13], "",   ParamScale::Linear, {} },
    { "flipBit",      "BIT",   0.f,     15.f,    kFxParamDefaults[14], "",   ParamScale::Linear, {} },
    { "flipMode",     "MODE",  0.f,     1.f,     kFxParamDefaults[15], "",   ParamScale::Choice, { "sample", "burst" } },
    { "glitchOn",     "ON",    0.f,     1.f,     kFxParamDefaults[16], "",   ParamScale::Toggle, {} },
    { "glitchProb",   "PROB",  0.f,     1.f,     kFxParamDefaults[17], "",   ParamScale::Linear, {} },
    { "glitchLenMs",  "LEN",   5.f,     200.f,   kFxParamDefaults[18], "ms", ParamScale::Log,    {} },
} };

constexpr ParamSpecEntry kMasterShakalSpec { "masterShakal", "SHAKAL", 0.f, 1.f, 0.f, "" };

inline const FxParamSpecEntry& fxParamSpec (FxParam p) noexcept
{
    return kFxParamSpecs[static_cast<std::size_t> (p)];
}

inline float clampFxParam (FxParam p, float v) noexcept
{
    const auto& s = fxParamSpec (p);
    return std::clamp (v, s.minValue, s.maxValue);
}

// -1 when unknown
inline int fxParamFromName (std::string_view name) noexcept
{
    for (std::size_t i = 0; i < kFxParamSpecs.size(); ++i)
        if (kFxParamSpecs[i].name == name)
            return static_cast<int> (i);
    return -1;
}

inline const ParamSpecEntry& trackParamSpec (TrackParam p) noexcept
{
    return kTrackParamSpecs[static_cast<std::size_t> (p)];
}

inline float clampTrackParam (TrackParam p, float v) noexcept
{
    const auto& s = trackParamSpec (p);
    return std::clamp (v, s.minValue, s.maxValue);
}

// Guitar strip (stage 4), ids guitarParam(...)
constexpr std::array<ParamSpecEntry, kNumGuitarParams> kGuitarParamSpecs { {
    { "shakal",     "SHAKAL",  0.f,   1.f, kGuitarParamDefaults[0], "" },
    { "gateDb",     "GATE",  -80.f,   0.f, kGuitarParamDefaults[1], "dB" },
    { "hpfHz",      "LOW CUT", 20.f, 300.f, kGuitarParamDefaults[2], "Hz" },
    { "loopGainDb", "LOOP",  -60.f,  12.f, kGuitarParamDefaults[3], "dB" },
    { "loopTap",    "TAP",     0.f,   1.f, kGuitarParamDefaults[4], "" },   // 0 DI, 1 AMP
} };

// Insert slots (stage 4), ids insertParam(chain, slot, ...): bypass 0/1
constexpr std::array<std::string_view, kSlotsPerChain> kInsertBypassNames { "slot1Bypass", "slot2Bypass" };

inline int guitarParamFromName (std::string_view name) noexcept
{
    for (std::size_t i = 0; i < kGuitarParamSpecs.size(); ++i)
        if (kGuitarParamSpecs[i].name == name)
            return static_cast<int> (i);
    return -1;
}

inline float clampGuitarParam (GuitarParam p, float v) noexcept
{
    const auto& s = kGuitarParamSpecs[static_cast<std::size_t> (p)];
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

// Name of any ParamId (track, master, fx); empty when the id is unused. For JSON and MIDI learn.
inline std::string_view paramName (ParamId id) noexcept
{
    if (id == masterParam (MasterParam::Gain))   return kMasterGainSpec.name;
    if (id == masterParam (MasterParam::Shakal)) return kMasterShakalSpec.name;
    if (id >= kTrackBase && id < kTrackBase + kNumTracks * kTrackStride)
    {
        const int p = (id - kTrackBase) % kTrackStride;
        return p < kNumTrackParams ? kTrackParamSpecs[static_cast<std::size_t> (p)].name : std::string_view {};
    }
    if (id >= kFxBase && id < kFxBase + kNumChains * kFxStride)
    {
        const int p = (id - kFxBase) % kFxStride;
        return p < kNumFxParams ? kFxParamSpecs[static_cast<std::size_t> (p)].name : std::string_view {};
    }
    if (id >= kGuitarBase && id < kGuitarBase + kNumGuitarParams)
        return kGuitarParamSpecs[static_cast<std::size_t> (id - kGuitarBase)].name;
    if (id >= kInsertBase && id < kInsertBase + kNumChains * kSlotsPerChain)
        return kInsertBypassNames[static_cast<std::size_t> ((id - kInsertBase) % kSlotsPerChain)];
    return {};
}

// chain 0..7 = track, kMasterChain = master, kGuitarChain = guitar. Track and fx names both resolve; -1 when unknown.
inline int paramIdByName (int chain, std::string_view name) noexcept
{
    for (int s = 0; s < kSlotsPerChain; ++s)
        if (name == kInsertBypassNames[static_cast<std::size_t> (s)])
            return insertParam (chain, s, InsertParam::Bypass);
    if (const int f = fxParamFromName (name); f >= 0)
        return fxParam (chain, static_cast<FxParam> (f));
    if (chain == kGuitarChain)
    {
        const int g = guitarParamFromName (name);
        return g >= 0 ? guitarParam (static_cast<GuitarParam> (g)) : -1;
    }
    if (chain == kMasterChain)
    {
        if (name == kMasterGainSpec.name)   return masterParam (MasterParam::Gain);
        if (name == kMasterShakalSpec.name || name == "shakal") return masterParam (MasterParam::Shakal);
        return -1;
    }
    if (const int t = trackParamFromName (name); t >= 0)
        return trackParam (chain, static_cast<TrackParam> (t));
    return -1;
}

} // namespace jackal
