#pragma once

#include "CommandQueue.h"
#include "EngineTypes.h"
#include "GlitchProcessor.h"
#include "PatternData.h"
#include "SampleBank.h"
#include "SongData.h"
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

struct HitRecord
{
    int64_t sample = 0;
    uint8_t track = 0, velocity = 0;
};
constexpr int kHitHistory = 4096;

class Sequencer
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset() noexcept;   // Play/Stop: k = 0, P = 0, rolls off, pending = -1
    void onPlay() noexcept;  // after reset() on Play: song state from the cursor
    void onStop() noexcept;  // after reset() on Stop: cursor 0, hit history cleared

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

    // stage 4: loop recorder and player
    double patternStartBeat() const noexcept { return patternStartBeat_; }
    int currentBars() const noexcept;
    int currentLoopSlot() const noexcept;
    // First pattern-cycle start strictly after `beat` (a boundary exactly at `beat` counts as passed).
    double nextCycleStartBeat (double beat) const noexcept;

    // stage 5: song
    const SongData* song() const noexcept { return song_.load (std::memory_order_acquire); }
    void setSong (const SongData* next) noexcept;        // the caller has already retired the old one
    void setSongMode (bool on, bool playing) noexcept;
    void setSongPosition (int index, bool playing) noexcept;
    bool songMode() const noexcept { return songMode_; }
    int songIndex() const noexcept;
    int songRepeat() const noexcept { return songRepeat_; }
    int songCursor() const noexcept { return playing_ ? (songMode_ ? pendingSongIndex_ : songCursor_) : songCursor_; }
    bool songEnded() const noexcept { return songEnded_; }
    int64_t songEndSample() const noexcept { return songEndSample_; }

    // stage 5: retro capture of live hits
    void capture (SequencerContext&, int bars) noexcept;

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
    void switchPattern (SequencerContext&, int index, double switchBeat, int64_t switchSample) noexcept;
    bool songPlayable() const noexcept;
    void handleSongBoundary (SequencerContext&, double beat, int64_t sample) noexcept;
    void enterSong (SequencerContext&, double beat, int64_t sample) noexcept;
    int64_t firstBoundaryAtOrAfter (const Transport&, int64_t pos, double& beatOut) const noexcept;
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

    std::atomic<const SongData*> song_ { nullptr };
    bool songMode_ = false, pendingSongMode_ = false, songEnded_ = false, scheduling_ = true, songBroken_ = false;
    int songIndex_ = 0, songRepeat_ = 0, songCursor_ = 0, pendingSongIndex_ = -1;
    int64_t songEndSample_ = -1;

    std::array<HitRecord, kHitHistory> hits_ {};
    int64_t hitCount_ = 0;
};

} // namespace jackal
