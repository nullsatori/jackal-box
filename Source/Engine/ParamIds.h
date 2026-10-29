#pragma once

#include <array>
#include <atomic>
#include <cstdint>

namespace jackal
{

using ParamId = uint16_t;

enum class TrackParam : uint8_t { Volume, Pan, Pitch, Decay, Tone, Start, End, Shakal /* stage 3 */, Count };
enum class MasterParam : uint8_t { Gain, Shakal /* stage 3 */, Count };

constexpr ParamId kMasterBase = 0x0000, kTrackBase = 0x0100, kTrackStride = 0x20;
constexpr ParamId kGlitchBase = 0x0200, kFxBase = 0x0300;   // reserved
constexpr int kNumParams = 0x0400;
constexpr int kNumTrackParams = static_cast<int> (TrackParam::Count);

constexpr ParamId trackParam (int track, TrackParam p) noexcept
{
    return static_cast<ParamId> (kTrackBase + track * kTrackStride + static_cast<int> (p));
}

constexpr ParamId masterParam (MasterParam p) noexcept
{
    return static_cast<ParamId> (kMasterBase + static_cast<int> (p));
}

// Defaults live here so the engine is usable without the model library.
// Model/ParamSpec.h adds names, ranges and units on top of these.
constexpr std::array<float, kNumTrackParams> kTrackParamDefaults { 0.f, 0.f, 0.f, 5000.f, 20000.f, 0.f, 1.f, 0.f };
constexpr float kMasterGainDefaultDb = -6.f;

// UI writes, audio reads relaxed (once per block, or at trigger time for p-locks).
struct ParamTable
{
    ParamTable() noexcept { resetToDefaults(); }

    void resetToDefaults() noexcept
    {
        for (auto& v : values)
            v.store (0.f, std::memory_order_relaxed);
        for (int t = 0; t < 8; ++t)
            for (int p = 0; p < kNumTrackParams; ++p)
                set (trackParam (t, static_cast<TrackParam> (p)), kTrackParamDefaults[static_cast<std::size_t> (p)]);
        set (masterParam (MasterParam::Gain), kMasterGainDefaultDb);
    }

    float get (ParamId id) const noexcept
    {
        return id < kNumParams ? values[id].load (std::memory_order_relaxed) : 0.f;
    }

    void set (ParamId id, float v) noexcept
    {
        if (id < kNumParams)
            values[id].store (v, std::memory_order_relaxed);
    }

    std::array<std::atomic<float>, kNumParams> values;
};

} // namespace jackal
