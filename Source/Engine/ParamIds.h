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
constexpr ParamId kGlitchBase = 0x0200, kFxBase = 0x0300;
constexpr int kNumParams = 0x0600;
constexpr int kNumTrackParams = static_cast<int> (TrackParam::Count);

constexpr ParamId trackParam (int track, TrackParam p) noexcept
{
    return static_cast<ParamId> (kTrackBase + track * kTrackStride + static_cast<int> (p));
}

constexpr ParamId masterParam (MasterParam p) noexcept
{
    return static_cast<ParamId> (kMasterBase + static_cast<int> (p));
}

// SHAKAL chain parameters (stage 3): kFxBase + chain * kFxStride + param.
enum class FxParam : uint8_t
{
    Bypass, Preset,
    DriveOn, DriveDb, DriveMode,
    Sp12On, Sp12FilterHz,
    DecimOn, DecimRateHz,
    CrushOn, CrushBits, CrushMulaw,
    FlipOn, FlipProb, FlipBit, FlipMode,
    GlitchOn, GlitchProb, GlitchLenMs,
    Count
};
constexpr int kNumFxParams = static_cast<int> (FxParam::Count);   // 19
constexpr int kNumChains = 10, kMasterChain = 8, kGuitarChain = 9;  // 0..7 tracks, 8 master, 9 guitar
constexpr int kSlotsPerChain = 2;
constexpr int kMaxPdcSamples = 16384;
constexpr ParamId kFxStride = 0x20;

constexpr ParamId fxParam (int chain, FxParam p) noexcept
{
    return static_cast<ParamId> (kFxBase + chain * kFxStride + static_cast<int> (p));
}

constexpr std::array<float, kNumFxParams> kFxParamDefaults {
    0.f, 0.f,
    0.f, 0.f, 0.f,
    0.f, 10000.f,
    0.f, 48000.f,
    0.f, 16.f, 0.f,
    0.f, 0.f, 12.f, 0.f,
    0.f, 0.f, 50.f };

// Guitar strip and insert slots (stage 4).
enum class GuitarParam : uint8_t { Shakal, GateDb, HpfHz, LoopGainDb, LoopTap, Count };
enum class InsertParam : uint8_t { Bypass, Count };
constexpr int kNumGuitarParams = static_cast<int> (GuitarParam::Count);
constexpr ParamId kGuitarBase = 0x0500, kInsertBase = 0x0520;
constexpr std::array<float, kNumGuitarParams> kGuitarParamDefaults { 0.f, -80.f, 20.f, 0.f, 1.f };

constexpr ParamId guitarParam (GuitarParam p) noexcept
{
    return static_cast<ParamId> (kGuitarBase + static_cast<int> (p));
}

constexpr ParamId insertParam (int chain, int slot, InsertParam) noexcept
{
    return static_cast<ParamId> (kInsertBase + chain * kSlotsPerChain + slot);
}

// Defaults live here so the engine is usable without the model library.
// Model/ParamSpec.h adds names, ranges and units on top of these.
constexpr std::array<float, kNumTrackParams> kTrackParamDefaults { 0.f, 0.f, 0.f, 5000.f, 20000.f, 0.f, 1.f, 0.f };
constexpr float kMasterGainDefaultDb = -6.f;
constexpr float kMasterGainNewProjectDb = -40.f;   // 1% amplitude: a new project starts quiet (the engine keeps -6 dB)

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
        for (int c = 0; c < kNumChains; ++c)
            for (int p = 0; p < kNumFxParams; ++p)
                set (fxParam (c, static_cast<FxParam> (p)), kFxParamDefaults[static_cast<std::size_t> (p)]);
        for (int p = 0; p < kNumGuitarParams; ++p)
            set (guitarParam (static_cast<GuitarParam> (p)), kGuitarParamDefaults[static_cast<std::size_t> (p)]);
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
