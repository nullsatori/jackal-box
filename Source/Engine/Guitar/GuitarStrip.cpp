#include "GuitarStrip.h"

#include <algorithm>
#include <cmath>

namespace jackal
{

void GuitarStrip::prepare (double sampleRate, int maxBlockSize, int)
{
    sampleRate_ = sampleRate;
    monitor_.prepare (sampleRate);
    gate_.prepare (sampleRate);
    const auto n = static_cast<std::size_t> (std::max (1, maxBlockSize));
    for (auto* v : { &mono_, &l_, &r_ })
        v->assign (n, 0.f);
    reset();
}

void GuitarStrip::reset() noexcept
{
    gate_.reset();
    hpf_.reset();
}

void GuitarStrip::process (float* masterL, float* masterR, int n, const ParamTable& params, const EngineParams& ep,
                           const Transport& transport, const SampleBank& bank, const GuitarContext& ctx,
                           ShakalChain& chain9, InsertChain& inserts9, LoopRecorder& recorder, LoopPlayer& player,
                           LoopResult& result) noexcept
{
    const float monitorDb = ep.monitorGainDb.load (std::memory_order_relaxed);
    const float monitorGain = monitorDb <= -60.f ? 0.f : std::pow (10.f, monitorDb / 20.f);
    const bool monitorOn = ep.monitorEnabled.load (std::memory_order_relaxed);
    const float gateDb = params.get (guitarParam (GuitarParam::GateDb));
    const float hpfHz = std::clamp (params.get (guitarParam (GuitarParam::HpfHz)), 20.f, 300.f);
    const float loopDb = params.get (guitarParam (GuitarParam::LoopGainDb));
    const bool tapAmp = params.get (guitarParam (GuitarParam::LoopTap)) >= 0.5f;

    // (1) monitor of the selected input
    std::fill (mono_.begin(), mono_.begin() + n, 0.f);
    float* monoOut[1] = { mono_.data() };
    monitor_.process (ctx.in, ctx.numIn, monoOut, 1, n, monitorGain, monitorOn);

    // (2) gate, low cut
    gate_.process (mono_.data(), n, gateDb);
    if (hpfHz > 20.f)
    {
        if (hpfHz != hpfHz_)
        {
            hpfHz_ = hpfHz;
            hpf_.setHighpass (sampleRate_, hpfHz, 0.7071);
        }
        hpf_.flushDenormals();
        for (int i = 0; i < n; ++i)
            mono_[static_cast<std::size_t> (i)] = hpf_.process (mono_[static_cast<std::size_t> (i)]);
    }

    // (3) DI point
    std::copy (mono_.begin(), mono_.begin() + n, l_.begin());
    std::copy (mono_.begin(), mono_.begin() + n, r_.begin());

    const int lGuitar = inserts9.latencySamples();
    const int comp = ctx.loopCompDevice + ctx.lMasterPath + (tapAmp ? lGuitar : 0);
    lastComp_ = comp;

    // The recorder takes the tap (live signal only) before the loop is mixed in; a loop that finishes in this
    // block plays from this block on, continuing at the same index (contract stage 4, §5.4).
    const int slot = recorder.slot();
    auto tapPoint = [&] {
        recorder.process (l_.data(), r_.data(), n, ctx.blockStart, comp, bank.get (slot), result);
        player.render (l_.data(), r_.data(), n, ctx.blockStart, loopDb, transport, bank, slot, result.finished);
    };

    if (! tapAmp)
        tapPoint();

    // (4) inserts
    inserts9.process (l_.data(), r_.data(), n, params, ctx.insertCtx);

    // (5) AMP point
    if (tapAmp)
        tapPoint();

    // (6) SHAKAL of the guitar, (7) into the master
    chain9.process (l_.data(), r_.data(), n, params, transport, ctx.blockStart, ctx.playing, ctx.seed);
    for (int i = 0; i < n; ++i)
    {
        masterL[i] += l_[static_cast<std::size_t> (i)];
        masterR[i] += r_[static_cast<std::size_t> (i)];
    }
}

} // namespace jackal
