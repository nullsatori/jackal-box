#include "MacroCurves.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace jackal::MacroCurves
{

namespace
{
    struct Pt { float m, v; };

    template <std::size_t N>
    float interp (const std::array<Pt, N>& pts, float m, bool logScale) noexcept
    {
        if (m <= pts[0].m)
            return pts[0].v;
        for (std::size_t i = 1; i < N; ++i)
        {
            if (m <= pts[i].m)
            {
                const float f = (m - pts[i - 1].m) / (pts[i].m - pts[i - 1].m);
                if (! logScale)
                    return pts[i - 1].v + (pts[i].v - pts[i - 1].v) * f;
                const float a = std::log2 (pts[i - 1].v), b = std::log2 (pts[i].v);
                return std::exp2 (a + (b - a) * f);
            }
        }
        return pts[N - 1].v;
    }

    // value of the last point with point.m <= m
    template <std::size_t N>
    float step (const std::array<Pt, N>& pts, float m) noexcept
    {
        float v = pts[0].v;
        for (const auto& p : pts)
            if (p.m <= m)
                v = p.v;
        return v;
    }

    // Iron: the sampler dies
    constexpr float kIronSp12Th = 0.10f;   constexpr std::array<Pt, 2> kIronSp12 { { { 0.10f, 12000.f }, { 1.00f, 6000.f } } };
    constexpr float kIronCrushTh = 0.25f;  constexpr std::array<Pt, 3> kIronCrush { { { 0.25f, 16.f }, { 0.60f, 8.f }, { 1.00f, 4.f } } };
    constexpr float kIronDecimTh = 0.40f;  constexpr std::array<Pt, 3> kIronDecim { { { 0.40f, 48000.f }, { 0.70f, 11025.f }, { 1.00f, 2000.f } } };
    constexpr float kIronDriveTh = 0.55f;  constexpr std::array<Pt, 3> kIronDrive { { { 0.55f, 0.f }, { 0.80f, 12.f }, { 1.00f, 24.f } } };
                                           constexpr std::array<Pt, 2> kIronDriveMode { { { 0.55f, 0.f }, { 0.85f, 1.f } } };
    constexpr float kIronFlipTh = 0.80f;   constexpr std::array<Pt, 2> kIronFlip { { { 0.80f, 0.f }, { 1.00f, 0.01f } } };

    // Net: bad connection
    constexpr float kNetDecimTh = 0.05f;   constexpr std::array<Pt, 3> kNetDecim { { { 0.05f, 48000.f }, { 0.50f, 8000.f }, { 1.00f, 4000.f } } };
    constexpr float kNetCrushTh = 0.20f;   constexpr std::array<Pt, 3> kNetCrush { { { 0.20f, 16.f }, { 0.50f, 8.f }, { 1.00f, 5.f } } };
    constexpr float kNetGlitchTh = 0.35f;  constexpr std::array<Pt, 3> kNetGlitch { { { 0.35f, 0.f }, { 0.70f, 0.25f }, { 1.00f, 0.60f } } };
    constexpr float kNetFlipTh = 0.60f;    constexpr std::array<Pt, 2> kNetFlip { { { 0.60f, 0.f }, { 1.00f, 0.15f } } };

    // Mem: broken memory, stuck playback
    constexpr float kMemGlitchTh = 0.05f;  constexpr std::array<Pt, 3> kMemGlitch { { { 0.05f, 0.f }, { 0.50f, 0.35f }, { 1.00f, 0.80f } } };
                                           constexpr std::array<Pt, 2> kMemGlitchLen { { { 0.05f, 120.f }, { 1.00f, 15.f } } };
    constexpr float kMemFlipTh = 0.30f;    constexpr std::array<Pt, 3> kMemFlip { { { 0.30f, 0.f }, { 0.60f, 0.003f }, { 1.00f, 0.05f } } };
                                           constexpr std::array<Pt, 3> kMemFlipBit { { { 0.30f, 12.f }, { 0.90f, 15.f }, { 1.00f, 15.f } } };
    constexpr float kMemCrushTh = 0.50f;   constexpr std::array<Pt, 2> kMemCrush { { { 0.50f, 16.f }, { 1.00f, 6.f } } };
    constexpr float kMemDriveTh = 0.75f;   constexpr std::array<Pt, 2> kMemDrive { { { 0.75f, 0.f }, { 1.00f, 18.f } } };
} // namespace

FxState resolve (ShakalPreset preset, float m) noexcept
{
    m = std::clamp (m, 0.f, 1.f);
    FxState s = kNeutral;

    switch (preset)
    {
        case ShakalPreset::Iron:
            if (m >= kIronSp12Th)  { s.sp12On = true; s.sp12FilterHz = interp (kIronSp12, m, true); }
            if (m >= kIronCrushTh) { s.crushOn = true; s.crushBits = interp (kIronCrush, m, false); }
            if (m >= kIronDecimTh) { s.decimOn = true; s.decimRateHz = interp (kIronDecim, m, true); }
            if (m >= kIronDriveTh) { s.driveOn = true; s.driveDb = interp (kIronDrive, m, false); s.driveWrap = step (kIronDriveMode, m) >= 0.5f; }
            if (m >= kIronFlipTh)  { s.flipOn = true; s.flipProb = interp (kIronFlip, m, false); s.flipBit = 10; s.flipBurst = false; }
            break;

        case ShakalPreset::Net:
            if (m >= kNetDecimTh)  { s.decimOn = true; s.decimRateHz = interp (kNetDecim, m, true); }
            if (m >= kNetCrushTh)  { s.crushOn = true; s.crushBits = interp (kNetCrush, m, false); s.crushMulaw = true; }
            if (m >= kNetGlitchTh) { s.glitchOn = true; s.glitchProb = interp (kNetGlitch, m, false); s.glitchLenMs = 20.f; }
            if (m >= kNetFlipTh)   { s.flipOn = true; s.flipProb = interp (kNetFlip, m, false); s.flipBit = 13; s.flipBurst = true; }
            break;

        case ShakalPreset::Mem:
            if (m >= kMemGlitchTh) { s.glitchOn = true; s.glitchProb = interp (kMemGlitch, m, false); s.glitchLenMs = interp (kMemGlitchLen, m, true); }
            if (m >= kMemFlipTh)   { s.flipOn = true; s.flipProb = interp (kMemFlip, m, false); s.flipBit = static_cast<int> (step (kMemFlipBit, m)); s.flipBurst = false; }
            if (m >= kMemCrushTh)  { s.crushOn = true; s.crushBits = interp (kMemCrush, m, false); }
            if (m >= kMemDriveTh)  { s.driveOn = true; s.driveDb = interp (kMemDrive, m, false); s.driveWrap = true; }
            break;
    }
    return s;
}

} // namespace jackal::MacroCurves
