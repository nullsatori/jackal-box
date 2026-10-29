#include "ShakalChain.h"

#include "MacroCurves.h"

#include <algorithm>
#include <cmath>

namespace jackal
{

void ShakalChain::prepare (double sampleRate, int maxBlockSize, int chainIndex)
{
    sampleRate_ = sampleRate;
    chain_ = chainIndex;
    smoothA_ = static_cast<float> (1.0 - std::exp (-static_cast<double> (kControlBlock) / (0.002 * sampleRate)));
    drive_.prepare (sampleRate, maxBlockSize);
    sp12_.prepare (sampleRate, maxBlockSize);
    decim_.prepare (sampleRate, maxBlockSize);
    crush_.prepare (sampleRate, maxBlockSize);
    flip_.prepare (sampleRate, maxBlockSize);
    glitch_.prepare (sampleRate, maxBlockSize);
    reset();
}

void ShakalChain::reset() noexcept
{
    drive_.reset();
    sp12_.reset();
    decim_.reset();
    crush_.reset();
    flip_.reset();
    glitch_.reset();
    numPending_ = 0;
    initialised_ = false;
    snap_ = true;
    overrideOn_ = false;
    override_ = 0.f;
    gainPrev_ = gainCur_ = 1.f;
    engaged_ = 0;
    stoppedCounter_ = 0;
}

void ShakalChain::onTrigger (int offset, float macro) noexcept
{
    if (numPending_ >= kMaxPendingTriggers)
        return;
    // the offset is relative to the next process() call; it becomes a stream position there
    pending_[static_cast<std::size_t> (numPending_++)] = { 0, std::max (0, offset), macro, false };
}

void ShakalChain::cellAt (int64_t pos, bool playing, const Transport& t, int64_t& index, int64_t& start, int64_t& end) const noexcept
{
    if (! playing)
    {
        const int64_t len = std::max<int64_t> (1, std::llround (sampleRate_ / 8.0));
        index = pos / len;
        start = index * len;
        end = start + len;
        return;
    }

    auto at = [&t] (int64_t c) { return t.sampleAtBeat (static_cast<double> (c) / 4.0); };
    int64_t c = static_cast<int64_t> (std::floor (t.beatAtSample (pos) * 4.0));
    while (at (c) > pos) --c;
    while (at (c + 1) <= pos) ++c;
    index = c;
    start = at (c);
    end = at (c + 1);
}

void ShakalChain::controlUpdate (int64_t pos, float knob, ShakalPreset preset, const FxState& manual, bool bypass) noexcept
{
    // p-locks take effect at the first control boundary at or after their trigger
    int keep = 0;
    for (int i = 0; i < numPending_; ++i)
    {
        const auto& e = pending_[static_cast<std::size_t> (i)];
        if (e.resolved && e.pos <= pos)
        {
            overrideOn_ = e.macro >= 0.f;
            override_ = std::clamp (e.macro, 0.f, 1.f);
        }
        else
            pending_[static_cast<std::size_t> (keep++)] = e;
    }
    numPending_ = keep;

    const float target = overrideOn_ ? override_ : std::clamp (knob, 0.f, 1.f);
    if (snap_)
    {
        macro_ = target;
        snap_ = false;
    }
    else
    {
        macro_ += smoothA_ * (target - macro_);
        if (std::fabs (target - macro_) < 1e-6f)
            macro_ = target;
    }

    state_ = combineLayers (manual, MacroCurves::resolve (preset, macro_));
    state_.bypass = bypass;

    gainPrev_ = initialised_ ? gainCur_ : 1.f;
    gainCur_ = state_.driveOn ? std::pow (10.f, state_.driveDb / 20.f) : 1.f;
    if (! initialised_)
        gainPrev_ = gainCur_;
    initialised_ = true;

    uint8_t mask = 0;
    if (! bypass)
    {
        const bool on[] = { state_.driveOn, state_.sp12On, state_.decimOn, state_.crushOn, state_.flipOn, state_.glitchOn };
        for (int i = 0; i < 6; ++i)
            if (on[i])
                mask = static_cast<uint8_t> (mask | (1u << i));
    }
    engaged_ = mask;
}

void ShakalChain::process (float* left, float* right, int n, const ParamTable& params, const Transport& transport,
                           int64_t blockStart, bool playing, uint32_t seed) noexcept
{
    const int64_t pos0 = playing ? blockStart : stoppedCounter_;

    // resolve this block's trigger offsets to stream positions
    for (int i = 0; i < numPending_; ++i)
    {
        auto& e = pending_[static_cast<std::size_t> (i)];
        if (! e.resolved)
        {
            e.pos = pos0 + e.offset;
            e.resolved = true;
        }
    }

    const bool bypass = fxFlag (params.get (fxParam (chain_, FxParam::Bypass)));
    const auto preset = static_cast<ShakalPreset> (std::clamp<long> (std::lround (params.get (fxParam (chain_, FxParam::Preset))), 0, 2));
    const FxState manual = manualFxState (params, chain_);
    const ParamId knobId = chain_ == kMasterChain ? masterParam (MasterParam::Shakal)
                         : chain_ == kGuitarChain ? guitarParam (GuitarParam::Shakal)
                                                  : trackParam (chain_, TrackParam::Shakal);
    const float knob = params.get (knobId);

    FxContext ctx;
    ctx.sampleRate = sampleRate_;
    ctx.chainIndex = chain_;
    ctx.seed = seed;
    ctx.playing = playing;
    ctx.samplesPerBeat = transport.samplesPerBeat();
    ctx.rampLength = kControlBlock;

    int i = 0;
    while (i < n)
    {
        const int64_t p = pos0 + i;
        const int phase = static_cast<int> (p % kControlBlock);
        const int len = std::min (n - i, kControlBlock - phase);

        if (phase == 0 || ! initialised_)
            controlUpdate (p, knob, preset, manual, bypass);

        float* l = left + i;
        float* r = right != nullptr ? right + i : nullptr;

        if (state_.bypass)
        {
            glitch_.writeRing (l, r, len);
        }
        else
        {
            ctx.pos = p;
            ctx.rampPhase = phase;
            ctx.driveGainStart = gainPrev_;
            ctx.driveGainEnd = gainCur_;

            drive_.process (l, r, len, state_, ctx);
            sp12_.process (l, r, len, state_, ctx);
            decim_.process (l, r, len, state_, ctx);
            crush_.process (l, r, len, state_, ctx);
            flip_.process (l, r, len, state_, ctx);

            int j = 0;
            while (j < len)
            {
                int64_t c = 0, cs = 0, ce = 0;
                cellAt (p + j, playing, transport, c, cs, ce);
                const int m = static_cast<int> (std::min<int64_t> (len - j, ce - (p + j)));
                ctx.pos = p + j;
                ctx.cellIndex = c;
                ctx.cellStartSample = cs;
                ctx.cellLength = static_cast<int> (ce - cs);
                glitch_.process (l + j, r != nullptr ? r + j : nullptr, m, state_, ctx);
                j += m;
            }
        }
        i += len;
    }

    if (! playing)
        stoppedCounter_ += n;
}

} // namespace jackal
