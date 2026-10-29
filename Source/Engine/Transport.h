#pragma once

#include "EngineTypes.h"

#include <cstdint>

namespace jackal
{

// The tempo anchor keeps timing deterministic and independent of buffer size:
// beat k sits at anchor.sample + llround((k - anchor.beat) * samplesPerBeat).
struct TempoAnchor
{
    int64_t sample = 0;
    double beat = 0.0;
    double bpm = kDefaultBpm;
};

class Transport
{
public:
    void prepare (double sampleRate) noexcept;   // position 0, stopped (BPM is kept, see DEVIATIONS.md)
    void play() noexcept;                        // stage 1 always starts from 0
    void stop() noexcept;
    void setBpm (double bpm) noexcept;           // clamp 60..300, re-anchor at current position
    void advance (int numSamples) noexcept;      // end of block, only while playing

    double samplesPerBeat() const noexcept;
    int64_t sampleOfBeat (int64_t beatIndex) const noexcept;
    TransportState state() const noexcept;

    bool isPlaying() const noexcept { return playing_; }
    int64_t positionSamples() const noexcept { return positionSamples_; }
    const TempoAnchor& anchor() const noexcept { return anchor_; }

private:
    double positionBeats() const noexcept;

    double sampleRate_ = 48000.0;
    bool playing_ = false;
    int64_t positionSamples_ = 0;
    TempoAnchor anchor_ {};
};

} // namespace jackal
