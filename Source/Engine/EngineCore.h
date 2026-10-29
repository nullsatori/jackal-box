#pragma once

#include "CommandQueue.h"
#include "EngineTypes.h"
#include "Fx/ShakalChain.h"
#include "GlitchProcessor.h"
#include "Guitar/GuitarStrip.h"
#include "Inserts/InsertChain.h"
#include "Inserts/Pdc.h"
#include "LevelMeter.h"
#include "Metronome.h"
#include "MidiNoteMap.h"
#include "SampleBank.h"
#include "Sequencer.h"
#include "TrackPlayer.h"
#include "Transport.h"

#include <array>
#include <atomic>
#include <cstdint>
#include <vector>

namespace jackal
{

constexpr int kNumStems = 9;   // 8 tracks + guitar

struct StemTaps
{
    std::array<const float*, 2 * kNumTracks> track {};
    std::array<const float*, 2> guitar {};
    int numSamples = 0;
};

// The only thing AudioEngine talks to. prepare() may allocate; process() never does.
class EngineCore
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numInputs, int numOutputs);
    void process (const float* const* in, int numIn, float* const* out, int numOut, int numSamples) noexcept;

    // Not the audio thread: call only after the audio callback is removed.
    // Moves every pattern and sample pointer the engine holds into the retire queue.
    void releaseAll() noexcept;

    CommandQueue& commands() noexcept { return commands_; }
    EngineParams& params() noexcept { return params_; }
    MidiQueue& midi() noexcept { return midi_; }
    EventQueue& events() noexcept { return events_; }
    RetireQueue& retired() noexcept { return retired_; }

    TransportState transportSnapshot() const noexcept;
    SequencerSnapshot sequencerSnapshot() const noexcept;
    ShakalSnapshot shakalSnapshot() const noexcept;
    LoopSnapshot loopSnapshot() const noexcept;
    InsertChain& insertChain (int chain) noexcept { return inserts_[static_cast<std::size_t> (chain)]; }
    const SampleBank& bank() const noexcept { return bank_; }   // tests read slots; the app never does
    int maxBlockSize() const noexcept { return maxBlock_; }
    double sampleRate() const noexcept { return transport_.sampleRate(); }

    // stage 5: buses of the last processBlock (after 7c) and the guitar strip output; valid until the next process().
    // Read only by the thread that calls process().
    StemTaps stemTaps() const noexcept;
    // Not the audio thread. Also applied to inserts installed later by SetInsert (see DEVIATIONS.md, stage 5).
    void setNonRealtime (bool) noexcept;

    // Test builds only: skip steps 7a and 9a (regression E2). Never used by the app.
    void setFxDisabledForTests (bool off) noexcept { fxDisabled_ = off; }

    const LevelMeter& inputMeter() const noexcept { return inputMeter_; }
    const LevelMeter& outputMeter() const noexcept { return outputMeter_; }
    std::atomic<float>& cpuLoad() noexcept { return cpuLoad_; }

private:
    void processBlock (const float* const* in, int numIn, float* const* out, int numOut, int numSamples) noexcept;
    void applyCommand (const Command& command, SequencerContext& ctx) noexcept;
    void publishSnapshot() noexcept;
    void handleLoopResult (const LoopResult&) noexcept;
    void retireSample (const Sample*) noexcept;
    void playResets() noexcept;
    void handleMidi (const MidiEvent&, SequencerContext&) noexcept;

    CommandQueue commands_;
    MidiQueue midi_;
    EventQueue events_;
    RetireQueue retired_;
    EngineParams params_;

    Transport transport_;
    Metronome metronome_;
    GuitarStrip strip_;
    LoopRecorder recorder_;
    LoopPlayer loopPlayer_;
    Calibrator calibrator_;
    std::array<InsertChain, kNumChains> inserts_ {};
    Pdc pdc_;
    LoopPatternInfo lastLoopInfo_ {};
    const Sample* pendingRetire_ = nullptr;
    int lTracks_ = 0, lMaster_ = 0, lGuitar_ = 0;
    bool pdcOverflow_ = false;
    LevelMeter inputMeter_;
    LevelMeter outputMeter_;

    Sequencer sequencer_;
    SampleBank bank_;
    GlitchProcessor glitch_;
    std::array<TrackPlayer, kNumTracks> players_ {};
    std::array<ShakalChain, kNumChains> chains_ {};
    bool fxDisabled_ = false;
    std::array<TrackState, kNumTracks> tracks_ {};
    std::array<std::vector<float>, kNumTracks * 2> buses_;
    std::array<std::vector<float>, 2> master_;
    int maxBlock_ = 0;
    bool recording_ = false;
    uint32_t seed_ = 0x5A4C;
    uint32_t dropped_ = 0;
    int lastPadState_ = -1;
    std::atomic<const MidiNoteMap*> noteMap_ { nullptr };
    bool midiFill_ = false;
    bool nonRealtime_ = false;
    bool songEndedReported_ = false;
    std::array<const float*, 2 * kNumTracks> stemPtrs_ {};
    int stemSamples_ = 0;

    std::atomic<bool> snapPlaying_ { false };
    std::atomic<double> snapBpm_ { kDefaultBpm };
    std::atomic<int64_t> snapPositionSamples_ { 0 };
    std::atomic<double> snapPositionBeats_ { 0.0 };

    std::atomic<int> snapCurrentPattern_ { 0 }, snapPendingPattern_ { -1 };
    std::array<std::atomic<int>, kNumTracks> snapLaneSlot_ {};
    std::atomic<int> snapGlitchSlot_ { -1 }, snapActivePad_ { -1 }, snapPendingPad_ { -1 };
    std::atomic<bool> snapRecording_ { false };
    std::atomic<uint32_t> snapDropped_ { 0 };
    std::atomic<bool> snapSongMode_ { false }, snapSongEnded_ { false };
    std::atomic<int> snapSongIndex_ { -1 }, snapSongRepeat_ { 0 }, snapSongCursor_ { 0 };
    std::atomic<int64_t> snapSongEndSample_ { -1 };
    std::atomic<float> cpuLoad_ { 0.f };
    std::array<std::atomic<float>, kNumChains> snapMacro_ {};
    std::array<std::atomic<uint8_t>, kNumChains> snapEngaged_ {};
    std::atomic<uint16_t> snapOverride_ { 0 };
    // LoopSnapshot fields, one atomic each
    std::atomic<int> snapLoopState_ { 0 }, snapLoopSlot_ { -1 }, snapLoopBars_ { 0 }, snapCal_ { 0 };
    std::atomic<bool> snapLoopOverdub_ { false }, snapPdcOverflow_ { false };
    std::atomic<int64_t> snapLoopStart_ { 0 }, snapLoopLength_ { 0 }, snapRecIndex_ { -1 }, snapPlayIndex_ { -1 };
    std::atomic<int32_t> snapComp_ { 0 }, snapPdcTracks_ { 0 }, snapPdcMaster_ { 0 }, snapPdcGuitar_ { 0 }, snapCalSamples_ { -1 };
};

} // namespace jackal
