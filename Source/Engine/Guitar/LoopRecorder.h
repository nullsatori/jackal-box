#pragma once

#include "../EngineTypes.h"
#include "../Sample.h"
#include "../Transport.h"

#include <cstdint>

namespace jackal
{

class Sequencer;

struct LoopArm
{
    int slot = 8;
    int bars = 1;
    bool overdub = false;
    Sample* target = nullptr;
};

struct LoopResult
{
    Sample* finished = nullptr;
    Sample* aborted = nullptr;
};

// Records one loop at the next pattern cycle, compensated for the round trip (contract stage 4, §5.5).
class LoopRecorder
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    bool arm (const LoopArm&, const Transport&, const Sequencer&, int64_t blockStart, const Sample* existing) noexcept;
    void cancel (LoopResult&) noexcept;
    void abort (LoopResult& r) noexcept { cancel (r); }
    void process (const float* tapL, const float* tapR, int n, int64_t blockStart, int comp, const Sample* existing, LoopResult&) noexcept;

    LoopState state() const noexcept { return state_; }
    int slot() const noexcept { return arm_.slot; }
    int bars() const noexcept { return arm_.bars; }
    bool overdub() const noexcept { return arm_.overdub; }
    int64_t startSample() const noexcept { return start_; }
    int64_t lengthSamples() const noexcept { return length_; }
    int64_t recIndex() const noexcept { return state_ == LoopState::Recording ? recIndex_ : -1; }
    int comp() const noexcept { return comp_; }

private:
    double sampleRate_ = 48000.0;
    LoopState state_ = LoopState::Idle;
    LoopArm arm_ {};
    int64_t start_ = 0, length_ = 0, recIndex_ = 0;
    int comp_ = 0;
};

} // namespace jackal
