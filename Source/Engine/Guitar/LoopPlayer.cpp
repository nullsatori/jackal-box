#include "LoopPlayer.h"

#include <algorithm>
#include <cmath>

namespace jackal
{

void LoopPlayer::prepare (double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    gainA_ = static_cast<float> (1.0 - std::exp (-1.0 / (0.005 * sampleRate_)));
    reset();
}

void LoopPlayer::reset() noexcept
{
    playing_ = false;
    cur_ = {};
    prev_ = {};
    playIndex_ = -1;
    gainInit_ = false;
}

void LoopPlayer::update (bool playing, const LoopPatternInfo& current, const LoopPatternInfo& previous, int startOffset) noexcept
{
    playing_ = playing;
    if (previous.valid)
        prev_ = previous;
    cur_ = current;
    startOffset_ = std::max (0, startOffset);
}

void LoopPlayer::render (float* left, float* right, int n, int64_t blockStart, float gainDb, const Transport& t, const SampleBank& bank,
                         int freshSlot, const Sample* fresh) noexcept
{
    const float target = gainDb <= -60.f ? 0.f : std::pow (10.f, gainDb / 20.f);
    if (! gainInit_)
    {
        gain_ = target;
        gainInit_ = true;
    }

    playIndex_ = -1;
    if (! playing_ || ! cur_.valid)
    {
        gain_ = target;
        return;
    }

    // cached cycle of the pattern in use
    const LoopPatternInfo* info = nullptr;
    const Sample* s = nullptr;
    double rate = 1.0;
    int64_t cycleStart = 0, cycleEnd = -1;

    for (int i = 0; i < n; ++i)
    {
        gain_ += gainA_ * (target - gain_);
        if (std::fabs (target - gain_) < 1e-6f)
            gain_ = target;

        const int64_t qm = blockStart + i - startOffset_;   // musical position of this output sample
        const LoopPatternInfo* want = qm >= cur_.startSample ? &cur_ : (prev_.valid && qm >= prev_.startSample ? &prev_ : nullptr);
        if (want == nullptr || want->loopSlot < 8)
            continue;

        if (want != info || qm >= cycleEnd || qm < cycleStart)
        {
            info = want;
            s = (fresh != nullptr && info->loopSlot == freshSlot) ? fresh : bank.get (info->loopSlot);
            if (s == nullptr || s->sourceBars <= 0.0 || s->length <= 0)
            {
                s = nullptr;
                cycleEnd = -1;
                continue;
            }
            const double cycleBeats = s->sourceBars * 4.0;
            const double m = std::floor ((t.beatAtSample (qm) - info->startBeat) / cycleBeats + 1e-9);
            double b0 = info->startBeat + m * cycleBeats;
            cycleStart = t.sampleAtBeat (b0);
            if (cycleStart > qm) { b0 -= cycleBeats; cycleStart = t.sampleAtBeat (b0); }
            cycleEnd = t.sampleAtBeat (b0 + cycleBeats);
            if (cycleEnd <= qm) { b0 += cycleBeats; cycleStart = cycleEnd; cycleEnd = t.sampleAtBeat (b0 + cycleBeats); }

            rate = (static_cast<double> (s->length) / s->sampleRate) * t.bpm() / (s->sourceBars * 4.0 * 60.0);
            if (std::fabs (rate - 1.0) < 1e-6)
                rate = 1.0;
        }
        if (s == nullptr)
            continue;

        const int64_t d = qm - cycleStart;
        float vl = 0.f, vr = 0.f;
        if (rate == 1.0)
        {
            const int64_t idx = d % s->length;
            vl = s->channel (0)[idx];
            vr = s->channel (1)[idx];
            playIndex_ = idx;
        }
        else
        {
            const double pos = std::fmod (static_cast<double> (d) * rate, static_cast<double> (s->length));
            const auto i0 = static_cast<int64_t> (pos);
            const int64_t i1 = (i0 + 1) % s->length;
            const auto f = static_cast<float> (pos - static_cast<double> (i0));
            vl = s->channel (0)[i0] + (s->channel (0)[i1] - s->channel (0)[i0]) * f;
            vr = s->channel (1)[i0] + (s->channel (1)[i1] - s->channel (1)[i0]) * f;
            playIndex_ = i0;
        }
        left[i] += vl * gain_;
        right[i] += vr * gain_;
    }
}

} // namespace jackal
