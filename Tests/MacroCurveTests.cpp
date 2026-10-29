// Stage 3: macro curves of the three presets and the layer merge.

#include "FxTestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace jackal;
using Catch::Approx;

namespace
{
    bool inRanges (const FxState& s)
    {
        return s.driveDb >= 0.f && s.driveDb <= 36.f
            && s.sp12FilterHz >= 3000.f && s.sp12FilterHz <= 20000.f
            && s.decimRateHz >= 500.f && s.decimRateHz <= 48000.f
            && s.crushBits >= 1.f && s.crushBits <= 16.f
            && s.flipProb >= 0.f && s.flipProb <= 1.f && s.flipBit >= 0 && s.flipBit <= 15
            && s.glitchProb >= 0.f && s.glitchProb <= 1.f && s.glitchLenMs >= 5.f && s.glitchLenMs <= 200.f;
    }
} // namespace

TEST_CASE ("U1: macro curves", "[macro]")
{
    for (auto preset : { ShakalPreset::Iron, ShakalPreset::Net, ShakalPreset::Mem })
    {
        REQUIRE (MacroCurves::resolve (preset, 0.f) == kNeutral);
        FxState prev = MacroCurves::resolve (preset, 0.f);
        for (int i = 1; i <= 100; ++i)
        {
            const float m = static_cast<float> (i) / 100.f;
            const FxState s = MacroCurves::resolve (preset, m);
            INFO ("preset " << static_cast<int> (preset) << " m " << m);
            REQUIRE (inRanges (s));

            // once on, stays on; amounts only get worse while on
            const bool was[] = { prev.driveOn, prev.sp12On, prev.decimOn, prev.crushOn, prev.flipOn, prev.glitchOn };
            const bool now[] = { s.driveOn, s.sp12On, s.decimOn, s.crushOn, s.flipOn, s.glitchOn };
            for (int e = 0; e < 6; ++e)
                REQUIRE ((! was[e] || now[e]));
            if (prev.crushOn)  REQUIRE (s.crushBits <= prev.crushBits);
            if (prev.decimOn)  REQUIRE (s.decimRateHz <= prev.decimRateHz + 1e-3f);
            if (prev.sp12On)   REQUIRE (s.sp12FilterHz <= prev.sp12FilterHz + 1e-3f);
            if (prev.driveOn)  REQUIRE (s.driveDb >= prev.driveDb);
            if (prev.flipOn)   REQUIRE (s.flipProb >= prev.flipProb);
            if (prev.glitchOn) REQUIRE (s.glitchProb >= prev.glitchProb);
            prev = s;
        }
    }

    const auto iron = MacroCurves::resolve (ShakalPreset::Iron, 1.f);
    REQUIRE ((iron.sp12On && iron.crushOn && iron.decimOn && iron.driveOn && iron.flipOn && ! iron.glitchOn));
    const auto net = MacroCurves::resolve (ShakalPreset::Net, 1.f);
    REQUIRE ((net.decimOn && net.crushOn && net.crushMulaw && net.glitchOn && net.flipOn));
    const auto mem = MacroCurves::resolve (ShakalPreset::Mem, 1.f);
    REQUIRE ((mem.glitchOn && mem.flipOn && mem.crushOn && mem.driveOn && mem.driveWrap));

    REQUIRE (MacroCurves::resolve (ShakalPreset::Iron, 0.6f).crushBits == Approx (8.0).margin (0.01));
    REQUIRE (MacroCurves::resolve (ShakalPreset::Net, 0.5f).decimRateHz == Approx (8000.0).margin (1.0));
    REQUIRE (MacroCurves::resolve (ShakalPreset::Mem, 1.0f).glitchLenMs == Approx (15.0).margin (0.01));
    REQUIRE (MacroCurves::resolve (ShakalPreset::Iron, 0.85f).driveWrap);
}

TEST_CASE ("U2: layer merge", "[macro]")
{
    FxState manual, curve;

    manual.crushOn = true; manual.crushBits = 6.f;
    curve.crushOn = true;  curve.crushBits = 10.f;
    REQUIRE (combineLayers (manual, curve).crushBits == 6.f);
    manual.crushOn = false;
    REQUIRE (combineLayers (manual, curve).crushBits == 10.f);

    manual = {}; curve = {};
    manual.driveOn = true; manual.driveDb = 3.f; manual.driveWrap = true;
    curve.driveOn = true;  curve.driveDb = 12.f; curve.driveWrap = false;
    auto s = combineLayers (manual, curve);
    REQUIRE (s.driveDb == 12.f);
    REQUIRE (s.driveWrap);
    manual.driveOn = false;
    REQUIRE (! combineLayers (manual, curve).driveWrap);

    manual = {}; curve = {};
    manual.glitchOn = true; manual.glitchLenMs = 120.f;
    curve.glitchOn = true;  curve.glitchLenMs = 20.f;
    REQUIRE (combineLayers (manual, curve).glitchLenMs == 120.f);

    manual = {}; curve = {};
    manual.flipProb = 0.7f; curve.flipProb = 0.3f;   // both off
    REQUIRE (combineLayers (manual, curve) == kNeutral);
}
