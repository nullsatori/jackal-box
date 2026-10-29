#pragma once

#include "../ParamIds.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace jackal
{

enum class ShakalPreset : uint8_t { Iron, Net, Mem };

// Resolved values for one 32-sample control block.
struct FxState
{
    bool bypass = false;
    bool driveOn = false;  float driveDb = 0.f;          bool driveWrap = false;
    bool sp12On = false;   float sp12FilterHz = 10000.f;
    bool decimOn = false;  float decimRateHz = 48000.f;
    bool crushOn = false;  float crushBits = 16.f;       bool crushMulaw = false;
    bool flipOn = false;   float flipProb = 0.f;         int flipBit = 12;  bool flipBurst = false;
    bool glitchOn = false; float glitchProb = 0.f;       float glitchLenMs = 50.f;

    bool operator== (const FxState&) const = default;
};

constexpr FxState kNeutral {};

// Order of the chain (stage 3 contract §1); also the bit order of engagedMask.
enum class FxSlot : uint8_t { Drive, Sp12, Decim, Crush, Flip, Glitch, Count };

// Everything ShakalChain hands to an effect for one control segment.
struct FxContext
{
    double sampleRate = 48000.0;
    int chainIndex = 0;
    uint32_t seed = 0;
    int64_t pos = 0;                 // stream position of the first sample
    bool playing = false;
    double samplesPerBeat = 24000.0;
    int64_t cellStartSample = 0;     // glitch grid (1/16 while playing, sr/8 while stopped)
    int64_t cellIndex = 0;
    int cellLength = 6000;
    float driveGainStart = 1.f, driveGainEnd = 1.f;
    int rampPhase = 0, rampLength = 32;   // where this segment sits inside the drive gain ramp
};

inline bool fxFlag (float v) noexcept { return v >= 0.5f; }

// Manual layer straight from the table (one chain).
inline FxState manualFxState (const ParamTable& t, int chain) noexcept
{
    auto g = [&] (FxParam p) { return t.get (fxParam (chain, p)); };
    FxState s;
    s.bypass = fxFlag (g (FxParam::Bypass));
    s.driveOn = fxFlag (g (FxParam::DriveOn));
    s.driveDb = std::clamp (g (FxParam::DriveDb), 0.f, 36.f);
    s.driveWrap = fxFlag (g (FxParam::DriveMode));
    s.sp12On = fxFlag (g (FxParam::Sp12On));
    s.sp12FilterHz = std::clamp (g (FxParam::Sp12FilterHz), 3000.f, 20000.f);
    s.decimOn = fxFlag (g (FxParam::DecimOn));
    s.decimRateHz = std::clamp (g (FxParam::DecimRateHz), 500.f, 48000.f);
    s.crushOn = fxFlag (g (FxParam::CrushOn));
    s.crushBits = std::clamp (g (FxParam::CrushBits), 1.f, 16.f);
    s.crushMulaw = fxFlag (g (FxParam::CrushMulaw));
    s.flipOn = fxFlag (g (FxParam::FlipOn));
    s.flipProb = std::clamp (g (FxParam::FlipProb), 0.f, 1.f);
    s.flipBit = std::clamp (static_cast<int> (std::lround (g (FxParam::FlipBit))), 0, 15);
    s.flipBurst = fxFlag (g (FxParam::FlipMode));
    s.glitchOn = fxFlag (g (FxParam::GlitchOn));
    s.glitchProb = std::clamp (g (FxParam::GlitchProb), 0.f, 1.f);
    s.glitchLenMs = std::clamp (g (FxParam::GlitchLenMs), 5.f, 200.f);
    return s;
}

// Layer merge (§5.9): amounts take the "worse" side, selectors come from the manual layer when it is on.
inline FxState combineLayers (const FxState& m, const FxState& c) noexcept
{
    FxState s;
    s.bypass = m.bypass;

    s.driveOn = m.driveOn || c.driveOn;
    if (s.driveOn)
    {
        s.driveDb = m.driveOn && c.driveOn ? std::max (m.driveDb, c.driveDb) : (m.driveOn ? m.driveDb : c.driveDb);
        s.driveWrap = m.driveOn ? m.driveWrap : c.driveWrap;
    }

    s.sp12On = m.sp12On || c.sp12On;
    if (s.sp12On)
        s.sp12FilterHz = m.sp12On && c.sp12On ? std::min (m.sp12FilterHz, c.sp12FilterHz) : (m.sp12On ? m.sp12FilterHz : c.sp12FilterHz);

    s.decimOn = m.decimOn || c.decimOn;
    if (s.decimOn)
        s.decimRateHz = m.decimOn && c.decimOn ? std::min (m.decimRateHz, c.decimRateHz) : (m.decimOn ? m.decimRateHz : c.decimRateHz);

    s.crushOn = m.crushOn || c.crushOn;
    if (s.crushOn)
    {
        s.crushBits = m.crushOn && c.crushOn ? std::min (m.crushBits, c.crushBits) : (m.crushOn ? m.crushBits : c.crushBits);
        s.crushMulaw = m.crushOn ? m.crushMulaw : c.crushMulaw;
    }

    s.flipOn = m.flipOn || c.flipOn;
    if (s.flipOn)
    {
        s.flipProb = m.flipOn && c.flipOn ? std::max (m.flipProb, c.flipProb) : (m.flipOn ? m.flipProb : c.flipProb);
        s.flipBit = m.flipOn ? m.flipBit : c.flipBit;
        s.flipBurst = m.flipOn ? m.flipBurst : c.flipBurst;
    }

    s.glitchOn = m.glitchOn || c.glitchOn;
    if (s.glitchOn)
    {
        s.glitchProb = m.glitchOn && c.glitchOn ? std::max (m.glitchProb, c.glitchProb) : (m.glitchOn ? m.glitchProb : c.glitchProb);
        s.glitchLenMs = m.glitchOn ? m.glitchLenMs : c.glitchLenMs;
    }
    return s;
}

} // namespace jackal
