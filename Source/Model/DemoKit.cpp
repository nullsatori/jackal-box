#include "DemoKit.h"

#include "Slicer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace jackal
{

namespace
{
    constexpr double kTwoPi = 6.283185307179586;
    constexpr double kLn1000 = 6.907755278982137;   // -60 dB

    struct XorShift32
    {
        uint32_t s = 0x1234567u;
        float next() noexcept   // -1..1
        {
            s ^= s << 13;
            s ^= s >> 17;
            s ^= s << 5;
            return static_cast<float> (static_cast<double> (s) / 2147483648.0 - 1.0);
        }
    };

    struct OnePole   // y += a (x - y)
    {
        double a = 1.0, y = 0.0;
        OnePole (double hz, double sr) : a (1.0 - std::exp (-kTwoPi * hz / sr)) {}
        double lp (double x) noexcept { y += a * (x - y); return y; }
        double hp (double x) noexcept { return x - lp (x); }
    };

    std::vector<float> buffer (double seconds, double sr)
    {
        return std::vector<float> (static_cast<std::size_t> (std::llround (seconds * sr)), 0.f);
    }

    void normalize (std::vector<float>& d, float peakTarget = 0.9f)
    {
        float peak = 0.f;
        for (float v : d)
            peak = std::max (peak, std::fabs (v));
        if (peak > 0.f)
            for (float& v : d)
                v *= peakTarget / peak;
    }

    std::vector<float> kick (double sr)
    {
        auto d = buffer (0.350, sr);
        double phase = 0.0;
        for (std::size_t i = 0; i < d.size(); ++i)
        {
            const double t = static_cast<double> (i) / sr;
            const double f = 150.0 * std::pow (45.0 / 150.0, std::min (t, 0.060) / 0.060);
            phase += kTwoPi * f / sr;
            const double env = std::exp (-kLn1000 * t / 0.350);
            d[i] = static_cast<float> (std::tanh (1.5 * std::sin (phase) * env));
        }
        return d;
    }

    std::vector<float> snare (double sr)
    {
        auto d = buffer (0.220, sr);
        XorShift32 rng;
        OnePole hp (1000.0, sr);
        for (std::size_t i = 0; i < d.size(); ++i)
        {
            const double t = static_cast<double> (i) / sr;
            const double body = std::sin (kTwoPi * 190.0 * t) * std::exp (-kLn1000 * t / 0.120);
            const double noise = hp.hp (rng.next()) * std::exp (-kLn1000 * t / 0.180);
            d[i] = static_cast<float> (0.6 * body + 0.9 * noise);
        }
        return d;
    }

    std::vector<float> clap (double sr)
    {
        // 4 bursts of 8 ms every 11 ms, then a 150 ms tail: 33 + 150 = 183 ms
        auto d = buffer (0.183, sr);
        XorShift32 rng;
        OnePole hp (1000.0, sr), lp (4000.0, sr);
        for (std::size_t i = 0; i < d.size(); ++i)
        {
            const double t = static_cast<double> (i) / sr;
            double env = 0.0;
            for (int b = 0; b < 4; ++b)
            {
                const double tb = t - 0.011 * b;
                if (tb >= 0.0 && tb < 0.008)
                    env = std::max (env, 1.0 - tb / 0.008);
            }
            if (t >= 0.033)
                env = std::max (env, 0.7 * std::exp (-kLn1000 * (t - 0.033) / 0.150));
            d[i] = static_cast<float> (lp.lp (hp.hp (rng.next())) * env);
        }
        return d;
    }

    std::vector<float> hat (double sr, double seconds, bool open)
    {
        auto d = buffer (seconds, sr);
        XorShift32 rng;
        OnePole hp (6000.0, sr);
        for (std::size_t i = 0; i < d.size(); ++i)
        {
            const double t = static_cast<double> (i) / sr;
            const double env = open ? std::exp (-kLn1000 * t / seconds) : 1.0 - t / seconds;
            d[i] = static_cast<float> (hp.hp (rng.next()) * env);
        }
        return d;
    }

    std::vector<float> rim (double sr)
    {
        auto d = buffer (0.070, sr);
        for (std::size_t i = 0; i < d.size(); ++i)
        {
            const double t = static_cast<double> (i) / sr;
            const double click = t < 0.001 ? 1.0 - t / 0.001 : 0.0;
            const double tone = std::sin (kTwoPi * 800.0 * t) * std::exp (-kLn1000 * t / 0.070);
            d[i] = static_cast<float> (0.8 * click + 0.7 * tone);
        }
        return d;
    }

    std::vector<float> tom (double sr)
    {
        auto d = buffer (0.280, sr);
        double phase = 0.0;
        for (std::size_t i = 0; i < d.size(); ++i)
        {
            const double t = static_cast<double> (i) / sr;
            const double f = 110.0 * std::pow (70.0 / 110.0, std::min (t, 0.100) / 0.100);
            phase += kTwoPi * f / sr;
            d[i] = static_cast<float> (std::sin (phase) * std::exp (-kLn1000 * t / 0.280));
        }
        return d;
    }

    void addAt (std::vector<float>& dst, const std::vector<float>& src, double beat, double spbSamples, float gain)
    {
        const auto start = static_cast<std::size_t> (std::llround (beat * spbSamples));
        for (std::size_t i = 0; i < src.size() && start + i < dst.size(); ++i)
            dst[start + i] += src[i] * gain;
    }

    std::vector<float> breakLoop (double sr)
    {
        constexpr double bpm = 172.0;
        const double spb = sr * 60.0 / bpm;
        auto d = std::vector<float> (static_cast<std::size_t> (std::llround (8.0 * spb)), 0.f);

        auto k = kick (sr), s = snare (sr), h = hat (sr, 0.060, false);
        normalize (k); normalize (s); normalize (h);

        for (double b : { 0.0, 0.75, 1.5, 4.0, 5.5 })                addAt (d, k, b, spb, 1.0f);
        for (double b : { 1.0, 3.0, 2.75, 5.0, 6.5, 7.25 })          addAt (d, s, b, spb, 0.9f);
        for (double b : { 0.5, 2.25, 4.5, 6.25 })                    addAt (d, s, b, spb, 0.25f);   // ghosts, -12 dB
        for (int i = 0; i < 16; ++i)                                 addAt (d, h, i * 0.5, spb, 0.35f);

        for (float& v : d)
            v = static_cast<float> (std::tanh (1.3 * v));

        // one feedback comb, 7 ms, 0.3: a small room
        const auto delay = static_cast<std::size_t> (std::llround (0.007 * sr));
        for (std::size_t i = delay; i < d.size(); ++i)
            d[i] += 0.3f * d[i - delay];

        return d;
    }

    std::unique_ptr<Sample> wrap (std::vector<float>&& d, double sr)
    {
        normalize (d);
        auto s = std::make_unique<Sample>();
        s->numChannels = 1;
        s->sampleRate = sr;
        s->length = static_cast<int64_t> (d.size());
        s->data[0] = std::move (d);
        return s;
    }
} // namespace

const std::vector<std::string_view>& builtinNames()
{
    static const std::vector<std::string_view> names { "kick", "snare", "clap", "hat_closed", "hat_open", "rim", "tom", "break" };
    return names;
}

std::unique_ptr<Sample> makeBuiltin (std::string_view name, double sr)
{
    if (sr <= 0.0)
        sr = 48000.0;

    if (name == "kick")       return wrap (kick (sr), sr);
    if (name == "snare")      return wrap (snare (sr), sr);
    if (name == "clap")       return wrap (clap (sr), sr);
    if (name == "hat_closed") return wrap (hat (sr, 0.060, false), sr);
    if (name == "hat_open")   return wrap (hat (sr, 0.320, true), sr);
    if (name == "rim")        return wrap (rim (sr), sr);
    if (name == "tom")        return wrap (tom (sr), sr);
    if (name == "break")
    {
        auto s = wrap (breakLoop (sr), sr);
        s->sourceBars = 2.0;
        s->sliceStarts = sliceGrid (s->length, 32);
        return s;
    }
    return nullptr;
}

std::unique_ptr<Project> makeDemoProject (double /*sampleRate*/)
{
    auto p = std::make_unique<Project>();
    p->name = "Demo";
    p->bpm = 172.0;

    const auto& names = builtinNames();
    for (std::size_t i = 0; i < names.size(); ++i)
    {
        auto& slot = p->slots[i];
        slot.used = true;
        slot.source = "builtin:" + std::string (names[i]);
        if (names[i] == "break")
        {
            slot.sliceMode = SliceMode::Grid32;
            slot.sourceBars = 2.0;
        }
    }

    const char* trackNames[kNumTracks] { "KICK", "SNARE", "CLAP", "CH", "OH", "RIM", "TOM", "BREAK" };
    for (int t = 0; t < kNumTracks; ++t)
    {
        auto& tr = p->tracks[static_cast<std::size_t> (t)];
        tr.name = trackNames[t];
        tr.sampleSlot = static_cast<int8_t> (t);
    }
    p->tracks[3].chokeGroup = 1;
    p->tracks[4].chokeGroup = 1;
    p->tracks[7].fitToTempo = true;

    auto on = [] (Pattern& pat, int lane, int slot, int vel = 100) {
        auto& s = pat.lanes[static_cast<std::size_t> (lane)].steps[static_cast<std::size_t> (slot)];
        s.active = true;
        s.velocity = static_cast<uint8_t> (vel);
        return &s;
    };

    Pattern& a = p->patterns[0];
    for (int s : { 0, 7, 10 })  on (a, 0, s);
    for (int s : { 4, 12 })     on (a, 1, s);
    for (int s = 0; s < 16; s += 2) on (a, 3, s, 90);
    for (int s : { 2, 10 })     on (a, 4, s, 80);
    // the break is 2 bars in 32 slices: a 32-step lane walks through all of it
    a.lanes[7].length = 32;
    for (int s = 0; s < 32; ++s) on (a, 7, s);

    Pattern& b = p->patterns[1];
    b = a;
    {
        auto* roll = &b.lanes[7].steps[12];
        roll->ratchet = 4;
        roll->ratchetPitchRamp = 12;
        b.lanes[7].steps[14].reverse = true;
        b.lanes[1].steps[14].active = true;
        b.lanes[1].steps[14].ratchet = 3;
    }

    return p;
}

} // namespace jackal
