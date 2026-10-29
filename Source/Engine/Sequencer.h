#pragma once

#include "CommandQueue.h"
#include "EngineTypes.h"
#include "GlitchProcessor.h"
#include "PatternData.h"
#include "SampleBank.h"
#include "TrackPlayer.h"
#include "Transport.h"

#include <array>
#include <atomic>
#include <cstdint>

namespace jackal
{

struct TrackState
{
    int8_t slot = -1;       // SampleBank slot, -1 = none
    uint8_t choke = 0;      // 0 = none, 1..4
    bool fit = false;       // fit loop to tempo by pitch
};

// Everything the sequencer reads or drives during one block (see DEVIATIONS.md, stage 2).
struct SequencerContext
{
    const Transport& transport;
    int64_t blockStart;
    int numSamples;
    const ParamTable& params;
    const SampleBank& bank;
    const std::array<TrackState, kNumTracks>& tracks;
    const std::array<bool, kNumTracks>& audible;   // mute/solo resolved for this block
    bool fillHeld;
    uint32_t seed;
    bool recording;
    std::array<TrackPlayer, kNumTracks>& players;
    GlitchProcessor& glitch;
    EventQueue& events;
    uint32_t& dropped;
};

class Sequencer
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset() noexcept;   // Play/Stop: k = 0, P = 0, rolls off, pending = -1

    const Pattern* patternAt (int index) const noexcept;
    // Replaces the pointer; the caller has already retired the old one.
    void setPattern (int index, const Pattern* next, const Transport&) noexcept;
    void queuePattern (int index, bool playing) noexcept;
    void killSample (const Sample*) noexcept;   // cancel rolls that point at this sample

    void process (SequencerContext&) noexcept;
    void liveHit (SequencerContext&, int track, uint8_t velocity) noexcept;

    void setManualPad (int pad) noexcept { manualPad_ = pad; }   // -1 = none (for glitch-lane recording)

    int currentPattern() const noexcept { return currentPattern_; }
    int pendingPattern() const noexcept { return pendingPattern_; }
    int laneSlot (int track) const noexcept { return lastSlot_[static_cast<std::size_t> (track)]; }
    int glitchSlot() const noexcept { return glitchSlot_; }

private:
    struct RollState
    {
        bool active = false;
        int r = 1, j = 0;
        int64_t onset = 0;
        double stepSamples = 0.0;
        TriggerParams base {};
        float gain0 = 1.f, velRamp = 0.f, pitch0 = 0.f, pitchRamp = 0.f;
    };

    const Pattern& current() const noexcept;
    int64_t onsetSample (const Transport&, const Pattern&, int track, int64_t k) const noexcept;
    bool buildTrigger (const SequencerContext&, int track, const Step& step, int slot, TriggerParams& out) const noexcept;
    void fire (SequencerContext&, int track, int offset, const TriggerParams&) noexcept;
    void fireRollHit (SequencerContext&, int track, int64_t at) noexcept;
    void processRange (SequencerContext&, int64_t r0, int64_t r1) noexcept;
    void switchPattern (SequencerContext&, double switchBeat, int64_t switchSample) noexcept;
    void resyncLanes (const Transport&, const Pattern& before, const Pattern& after) noexcept;

    std::array<std::atomic<const Pattern*>, kNumPatterns> patterns_ {};
    int currentPattern_ = 0, pendingPattern_ = -1;
    double patternStartBeat_ = 0.0;
    std::array<int64_t, kNumTracks> nextK_ {};
    std::array<int, kNumTracks> lastSlot_ {};
    std::array<RollState, kNumTracks> rolls_ {};
    int64_t nextGlitchStep_ = 0;
    int currentGlitchPad_ = 0;
    int glitchSlot_ = -1;
    int manualPad_ = -1;
    bool playing_ = false;
};

} // namespace jackal
