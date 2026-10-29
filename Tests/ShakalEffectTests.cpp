// Stage 3: the six SHAKAL effects one by one (offline buffers only, no audio device).

#include "FxTestHelpers.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <set>

using namespace jackal;
using namespace jackal::test;
using Catch::Approx;

namespace
{
    constexpr double kSr = 48000.0;

    FxContext ctxAt (int64_t pos = 0)
    {
        FxContext c;
        c.sampleRate = kSr;
        c.pos = pos;
        return c;
    }

    int countChanges (const Signal& s)
    {
        int n = 0;
        for (std::size_t i = 1; i < s.size(); ++i)
            n += s[i] != s[i - 1] ? 1 : 0;
        return n;
    }

    // every sample a different quantised level (so every latch shows up as a change)
    Signal distinctLevels (int n, int offset = 0)
    {
        Signal s (static_cast<std::size_t> (n));
        for (int i = 0; i < n; ++i)
            s[static_cast<std::size_t> (i)] = static_cast<float> (((i + offset) % 4000) - 2000) / 2048.f;
        return s;
    }
} // namespace

TEST_CASE ("X1: drive clip and wrap", "[fx]")
{
    DriveFx fx;
    FxState s;
    s.driveOn = true;
    s.driveDb = 6.0206f;
    FxContext c = ctxAt();
    c.driveGainStart = c.driveGainEnd = std::pow (10.f, s.driveDb / 20.f);

    auto one = [&] (float x) {
        Signal l { x }, r { x };
        fx.process (l.data(), r.data(), 1, s, c);
        return l[0];
    };

    REQUIRE (one (0.75f) == Approx (1.0).margin (1e-6));
    REQUIRE (one (0.25f) == Approx (0.5).margin (1e-6));

    s.driveWrap = true;
    REQUIRE (one (0.75f) == Approx (-0.5).margin (1e-6));
    REQUIRE (one (1.25f) == Approx (0.5).margin (1e-6));
    REQUIRE (one (-0.6f) == Approx (0.8).margin (1e-6));

    // 0 dB clip inside [-1, 1] is the identity
    s.driveWrap = false;
    s.driveDb = 0.f;
    c.driveGainStart = c.driveGainEnd = std::pow (10.f, 0.f);
    const auto ramp = makeRampSignal (1000);
    expectBitEqual (runFx (fx, s, c, ramp, ramp, 64).first, ramp);

    // off: untouched whatever the values
    s.driveOn = false;
    s.driveDb = 36.f;
    c.driveGainStart = c.driveGainEnd = 63.f;
    const auto noise = makeNoise (1000, 3, 4.f);
    expectBitEqual (runFx (fx, s, c, noise, noise, 64).first, noise);

    // gain ramp inside a control block
    s.driveOn = true;
    s.driveDb = 6.f;
    c.driveGainStart = 1.f;
    c.driveGainEnd = 2.f;
    c.rampLength = 32;
    c.rampPhase = 0;
    Signal l = makeDcSignal (32, 0.25f), r = l;
    fx.process (l.data(), r.data(), 32, s, c);
    for (int i = 0; i < 32; ++i)
        REQUIRE (l[static_cast<std::size_t> (i)] == Approx (0.25 * (1.0 + (i + 1) / 32.0)).margin (1e-6));
    REQUIRE (l[31] == Approx (0.5).margin (1e-6));
}

TEST_CASE ("X2: SP-12 hold, 12 bit, output filter", "[fx]")
{
    Sp12Fx fx;
    fx.prepare (kSr, 1024);
    FxState s;
    s.sp12On = true;
    s.sp12FilterHz = 20000.f;   // filter off

    const auto in = distinctLevels (48000);
    const auto out = runFx (fx, s, ctxAt(), in, in, 512).first;
    REQUIRE (std::abs (countChanges (out) - 26040) <= 1);
    for (float v : out)
        REQUIRE (std::round (v * 2048.f) == v * 2048.f);

    fx.reset();
    Signal hot = makeDcSignal (8, 1.5f);
    REQUIRE (runFx (fx, s, ctxAt(), hot, hot, 8).first[7] == 2047.f / 2048.f);

    // two RBJ sections as a 10 kHz Butterworth LP
    auto filt = [] (const Signal& x) {
        Biquad a, b;
        a.setLowpass (kSr, 10000.0, 0.5412);
        b.setLowpass (kSr, 10000.0, 1.3066);
        Signal y (x.size());
        for (std::size_t i = 0; i < x.size(); ++i)
            y[i] = b.process (a.process (x[i]));
        return y;
    };
    auto peakAfter = [] (const Signal& y, int from) {
        float p = 0.f;
        for (std::size_t i = static_cast<std::size_t> (from); i < y.size(); ++i)
            p = std::max (p, std::fabs (y[i]));
        return p;
    };
    const int settle = static_cast<int> (0.05 * kSr);
    REQUIRE (peakAfter (filt (makeSine (48000, 1000.0, kSr)), settle) == Approx (1.0).margin (0.012));
    REQUIRE (20.0 * std::log10 (peakAfter (filt (makeSine (48000, 20000.0, kSr)), settle)) <= -22.0);
    Signal imp (4800, 0.f);
    imp[0] = 1.f;
    expectNoNanInf (filt (imp));

    // denormal flush at control boundaries: after a second of silence the state is exactly zero
    Sp12Fx f2;
    f2.prepare (kSr, 1024);
    s.sp12FilterHz = 10000.f;
    Signal x = makeNoise (4800, 9);
    x.resize (4800 + 48000, 0.f);
    FxContext c = ctxAt();
    for (std::size_t i = 0; i < x.size(); i += 32)
    {
        c.rampPhase = 0;
        f2.process (x.data() + i, x.data() + i, 32, s, c);
        c.pos += 32;
    }
    for (int ch = 0; ch < 2; ++ch)
        for (int sec = 0; sec < 2; ++sec)
            for (int w = 0; w < 2; ++w)
                REQUIRE (f2.filterState (ch, sec, w) == 0.f);
}

TEST_CASE ("X3: decimator", "[fx]")
{
    FxState s;
    s.decimOn = true;

    {
        DecimatorFx fx;
        fx.prepare (kSr, 1024);
        s.decimRateHz = static_cast<float> (kSr / 4.0);
        const auto in = distinctLevels (4800);
        const auto out = runFx (fx, s, ctxAt(), in, in, 100).first;
        // runs between changes, ignoring the first partial run
        std::vector<int> runs;
        int run = 1;
        for (std::size_t i = 1; i < out.size(); ++i)
        {
            if (out[i] != out[i - 1]) { runs.push_back (run); run = 1; }
            else ++run;
        }
        REQUIRE (runs.size() > 100);
        for (std::size_t i = 1; i < runs.size(); ++i)
            REQUIRE (runs[i] == 4);
    }

    for (float rate : { 48000.f, 60000.f })
    {
        DecimatorFx fx;
        fx.prepare (kSr, 1024);
        s.decimRateHz = rate;
        const auto in = makeNoise (5000, 4);
        expectBitEqual (runFx (fx, s, ctxAt(), in, in, 64).first, in);
    }

    auto changes = [&] (float rate, int tol) {
        DecimatorFx fx;
        fx.prepare (kSr, 1024);
        s.decimRateHz = rate;
        const auto inL = distinctLevels (48000), inR = distinctLevels (48000, 777);
        const auto [l, r] = runFx (fx, s, ctxAt(), inL, inR, 333);
        REQUIRE (std::abs (countChanges (l) - static_cast<int> (rate)) <= tol);
        for (std::size_t i = 1; i < l.size(); ++i)
            REQUIRE ((l[i] != l[i - 1]) == (r[i] != r[i - 1]));
    };
    changes (500.f, 1);
    changes (3000.5f, 2);
}

TEST_CASE ("X4: bitcrush linear and mu-law", "[fx]")
{
    BitcrushFx fx;
    FxState s;
    s.crushOn = true;
    const auto noise = makeNoise (20000, 5);

    s.crushBits = 8.f;
    {
        const auto out = runFx (fx, s, ctxAt(), noise, noise, 256).first;
        for (std::size_t i = 0; i < out.size(); ++i)
        {
            REQUIRE (std::round (out[i] * 128.f) == out[i] * 128.f);
            REQUIRE (std::fabs (out[i] - noise[i]) <= 1.f / 256.f + 1e-7f);
        }
    }

    s.crushBits = 16.f;
    {
        const auto out = runFx (fx, s, ctxAt(), noise, noise, 256).first;
        bool anyDiff = false;
        for (std::size_t i = 0; i < out.size(); ++i)
        {
            REQUIRE (std::fabs (out[i] - noise[i]) <= std::exp2 (-15.f));
            anyDiff = anyDiff || out[i] != noise[i];
        }
        REQUIRE (anyDiff);
    }

    s.crushBits = 1.f;
    {
        std::set<float> values;
        for (float v : runFx (fx, s, ctxAt(), noise, noise, 256).first)
            values.insert (v);
        for (float v : values)
            REQUIRE ((v == -1.f || v == 0.f || v == 1.f));
    }

    Signal hot { 3.f, -3.f };
    for (float v : runFx (fx, s, ctxAt(), hot, hot, 2).first)
        REQUIRE (std::fabs (v) <= 1.f);

    // mu-law
    s.crushBits = 8.f;
    s.crushMulaw = true;
    Signal zero { 0.f };
    REQUIRE (runFx (fx, s, ctxAt(), zero, zero, 1).first[0] == 0.f);

    const auto ramp = makeRampSignal (20001);
    const auto mr = runFx (fx, s, ctxAt(), ramp, ramp, 256).first;
    for (std::size_t i = 1; i < mr.size(); ++i)
        REQUIRE (mr[i] >= mr[i - 1]);
    REQUIRE (std::fabs (mr.front()) == Approx (1.0).margin (1e-5));
    REQUIRE (std::fabs (mr.back()) == Approx (1.0).margin (1e-5));

    const auto quiet = makeNoise (20000, 6, 0.02f);
    const auto mu = runFx (fx, s, ctxAt(), quiet, quiet, 256).first;
    FxState lin = s;
    lin.crushMulaw = false;
    const auto li = runFx (fx, lin, ctxAt(), quiet, quiet, 256).first;
    double eMu = 0, eLin = 0;
    for (std::size_t i = 0; i < quiet.size(); ++i)
    {
        eMu += std::fabs (mu[i] - quiet[i]);
        eLin += std::fabs (li[i] - quiet[i]);
    }
    REQUIRE (eMu < eLin);
}

TEST_CASE ("X5: bitflip", "[fx]")
{
    BitflipFx fx;
    FxState s;
    s.flipOn = true;
    FxContext c = ctxAt();
    c.seed = 1234;

    const auto noise = makeNoise (20000, 7);
    s.flipProb = 0.f;
    expectBitEqual (runFx (fx, s, c, noise, noise, 64).first, noise);

    s.flipProb = 1.f;
    s.flipBit = 15;
    Signal half { 0.5f };
    REQUIRE (runFx (fx, s, c, half, half, 1).first[0] == Approx (-0.5).margin (1e-4));
    s.flipBit = 0;
    REQUIRE (runFx (fx, s, c, half, half, 1).first[0] == Approx (16385.0 / 32767.0).margin (1e-6));

    // sample mode: rate, determinism, seed
    s.flipProb = 0.1f;
    s.flipBit = 12;
    const auto big = makeNoise (1000000, 8);
    const auto a = runFx (fx, s, c, big, big, 64).first;
    int changed = 0;
    for (std::size_t i = 0; i < big.size(); ++i)
        changed += a[i] != big[i] ? 1 : 0;
    const double frac = changed / 1e6;
    REQUIRE (frac >= 0.095);
    REQUIRE (frac <= 0.105);
    expectBitEqual (runFx (fx, s, c, big, big, 1024).first, a);
    FxContext c2 = c;
    c2.seed = c.seed + 1;
    REQUIRE (runFx (fx, s, c2, big, big, 64).first != a);

    // burst mode: aligned runs of exactly 64, same positions in L and R
    s.flipBurst = true;
    const auto noiseR = makeNoise (200000, 11);
    const auto inL = makeNoise (200000, 10);
    const auto [l, r] = runFx (fx, s, c, inL, noiseR, 100);
    int bursts = 0;
    for (std::size_t b = 0; b < inL.size() / 64; ++b)
    {
        int n = 0;
        for (std::size_t k = b * 64; k < b * 64 + 64; ++k)
        {
            const bool cl = l[k] != inL[k], cr = r[k] != noiseR[k];
            REQUIRE (cl == cr);
            n += cl ? 1 : 0;
        }
        REQUIRE ((n == 0 || n == 64));
        bursts += n == 64 ? 1 : 0;
    }
    REQUIRE (bursts > 0);
}

TEST_CASE ("X6: glitch buffer cells", "[fx]")
{
    constexpr int C = 6000, L = 2400;   // 120 BPM at 48 kHz, 50 ms
    const auto in = makeNoise (48000 * 2, 12);

    auto rigRender = [&] (int buffer, uint32_t seed, float prob) {
        FxRig rig (120.0);
        rig.seed = seed;
        rig.setManual (FxParam::GlitchOn, 1.f);
        rig.setManual (FxParam::GlitchProb, prob);
        rig.setManual (FxParam::GlitchLenMs, 50.f);
        return rig.renderMono (in, buffer);
    };

    const uint32_t seed = 0x5A4C;
    const auto out = rigRender (1024, seed, 1.f);

    auto checkCell = [&] (const Signal& o, int c, uint32_t sd) {
        const int s0 = c * C;
        const auto type = GlitchBufferFx::typeFor (FxHash::unit (sd, FxHash::kSaltGlitchType, 0, c));
        auto formula = [&] (int k) {
            switch (type)
            {
                case GlitchBufferFx::Type::Stutter: return in[static_cast<std::size_t> (s0 + (k % L))];
                case GlitchBufferFx::Type::Reverse: return in[static_cast<std::size_t> (s0 - 1 - k)];
                case GlitchBufferFx::Type::Repeat:  return in[static_cast<std::size_t> (s0 - C + k)];
            }
            return 0.f;
        };
        for (int k = 0; k < C; ++k)
        {
            const float y = o[static_cast<std::size_t> (s0 + k)], f = formula (k), x = in[static_cast<std::size_t> (s0 + k)];
            if (k >= 64 && k < C - 64)
            {
                INFO ("cell " << c << " k " << k << " type " << static_cast<int> (type));
                REQUIRE (y == f);
            }
            else
            {
                REQUIRE (y >= std::min (x, f) - 1e-6f);
                REQUIRE (y <= std::max (x, f) + 1e-6f);
            }
        }
        return type;
    };

    std::set<int> types;
    for (int c = 1; c <= 7; ++c)
        types.insert (static_cast<int> (checkCell (out, c, seed)));

    expectBitEqual (rigRender (1024, seed, 0.f), in);

    // ring is written while off: switching on after 1 s reads real past input
    {
        FxRig rig (120.0);
        rig.setManual (FxParam::GlitchProb, 1.f);
        rig.setManual (FxParam::GlitchLenMs, 50.f);
        const auto o = rig.render (in, in, 250, [&] (int64_t pos, int) {
            rig.setManual (FxParam::GlitchOn, pos >= 48000 ? 1.f : 0.f);
        }).first;
        for (int i = 0; i < 48000; ++i)
            REQUIRE (o[static_cast<std::size_t> (i)] == in[static_cast<std::size_t> (i)]);
        bool sawReverse = false;
        for (int c = 8; c < 16; ++c)
            sawReverse = (checkCell (o, c, rig.seed) == GlitchBufferFx::Type::Reverse) || sawReverse;
        REQUIRE (sawReverse);
    }

    // the seed changes the type sequence
    bool differs = false;
    for (int c = 0; c < 16; ++c)
        differs = differs || GlitchBufferFx::typeFor (FxHash::unit (seed, FxHash::kSaltGlitchType, 0, c))
                          != GlitchBufferFx::typeFor (FxHash::unit (seed + 1, FxHash::kSaltGlitchType, 0, c));
    REQUIRE (differs);

    expectBitEqual (rigRender (64, seed, 1.f), out);
    expectBitEqual (rigRender (7, seed, 1.f), out);
}
