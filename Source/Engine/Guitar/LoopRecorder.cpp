#include "LoopRecorder.h"

#include "../Sequencer.h"

#include <algorithm>
#include <cmath>

namespace jackal
{

void LoopRecorder::prepare (double sampleRate, int)
{
    sampleRate_ = sampleRate;
    state_ = LoopState::Idle;
    arm_ = {};
}

bool LoopRecorder::arm (const LoopArm& a, const Transport& t, const Sequencer& seq, int64_t blockStart, const Sample* existing) noexcept
{
    if (state_ != LoopState::Idle || a.target == nullptr)
        return false;
    if (a.overdub && (existing == nullptr || existing->length <= 0 || existing->sourceBars <= 0.0))
        return false;

    arm_ = a;
    const double startBeat = seq.nextCycleStartBeat (t.beatAtSample (blockStart));
    start_ = t.sampleAtBeat (startBeat);
    if (a.overdub)
    {
        arm_.bars = static_cast<int> (std::lround (existing->sourceBars));
        length_ = existing->length;
    }
    else
    {
        arm_.bars = std::clamp (a.bars, 1, 4);
        length_ = t.sampleAtBeat (startBeat + arm_.bars * 4.0) - start_;
    }
    const auto capacity = static_cast<int64_t> (std::min (a.target->data[0].size(), a.target->data[1].size()));
    length_ = std::min (length_, capacity);
    recIndex_ = 0;
    comp_ = 0;
    state_ = LoopState::Armed;
    return true;
}

void LoopRecorder::cancel (LoopResult& r) noexcept
{
    if (state_ == LoopState::Idle)
        return;
    r.aborted = arm_.target;
    arm_.target = nullptr;
    state_ = LoopState::Idle;
}

void LoopRecorder::process (const float* tapL, const float* tapR, int n, int64_t blockStart, int comp, const Sample* existing, LoopResult& r) noexcept
{
    if (state_ == LoopState::Idle)
        return;

    if (state_ == LoopState::Armed)
    {
        // the block where the first loop sample arrives fixes the compensation
        if (blockStart + n - 1 - comp - start_ < 0)
            return;
        comp_ = comp;
        state_ = LoopState::Recording;
    }

    Sample& s = *arm_.target;
    const bool dub = arm_.overdub && existing != nullptr;
    for (int k = 0; k < n; ++k)
    {
        const int64_t i = blockStart + k - comp_ - start_;
        if (i < 0 || i >= length_)
            continue;
        const auto ui = static_cast<std::size_t> (i);
        const float l = tapL != nullptr ? tapL[k] : 0.f;
        const float rr = tapR != nullptr ? tapR[k] : l;
        if (dub && i < existing->length)
        {
            s.data[0][ui] = existing->channel (0)[i] + l;
            s.data[1][ui] = existing->channel (1)[i] + rr;
        }
        else
        {
            s.data[0][ui] = l;
            s.data[1][ui] = rr;
        }
        recIndex_ = i + 1;
    }

    if (blockStart + n - comp_ - start_ >= length_)
    {
        s.length = length_;
        s.sourceBars = static_cast<double> (arm_.bars);
        s.sampleRate = sampleRate_;
        s.numChannels = 2;
        s.sliceStarts.clear();   // capacity-preserving, no deallocation on the audio thread
        r.finished = arm_.target;
        arm_.target = nullptr;
        state_ = LoopState::Idle;
    }
}

} // namespace jackal
