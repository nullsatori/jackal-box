#pragma once

#include "ParamIds.h"
#include "PatternData.h"

#include <array>
#include <atomic>
#include <cstdint>

namespace jackal
{

constexpr double kMinBpm = 60.0, kMaxBpm = 300.0, kDefaultBpm = 172.0;
constexpr int kBeatsPerBar = 4;            // fixed 4/4
constexpr int kCommandQueueCapacity = 256;

enum class CommandType : uint8_t
{
    Play, Stop, SetBpm, SetInputChannel,   // stage 1
    SetPattern,        // ivalue = index 0..7, ptr = const Pattern* (engine owns it until retired)
    QueuePattern,      // ivalue = index; switches on the next bar, at once when stopped
    SetSlotSample,     // ivalue = slot 0..15, ptr = const Sample* or nullptr
    SetTrackSlot,      // ivalue = track | (slot << 8); slot 0xFF = none
    SetTrackChoke,     // ivalue = track | (group << 8), group 0..4
    SetTrackFit,       // ivalue = track | (fit << 8)   (see DEVIATIONS.md, stage 2)
    TriggerTrack,      // ivalue = track, value = velocity 1..127
    PadDown, PadUp,    // ivalue = pad 0..7
    SetRecording,      // ivalue = 0/1
    SetSeed,           // ivalue = seed (bit pattern of uint32)
    SetInsert,         // ivalue = chain | (slot << 8), ptr = InsertProcessor* or nullptr; old one is retired
    ArmLoop,           // ivalue = slot | (bars << 8) | (overdub << 16), ptr = Sample* (capacity >= loop length)
    CancelLoop,
    StartCalibration, CancelCalibration,
    // stage 5
    SetSong,           // ptr = const SongData* (engine owns it until retired); nullptr = no song
    SetSongMode,       // ivalue 0/1
    SetSongPosition,   // ivalue = entry 0..31; while playing the jump happens on the next cycle boundary
    SetNoteMap,        // ptr = const MidiNoteMap* or nullptr (= kDefaultNoteMap); the old one is retired
    CaptureHits        // ivalue = bars 1..4; results as StepRecorded ... CaptureDone
};

struct Command
{
    CommandType type = CommandType::Stop;
    float value = 0.f;
    int32_t ivalue = 0;
    const void* ptr = nullptr;
};

// Continuous parameters: UI writes, audio reads (relaxed).
struct EngineParams
{
    std::atomic<float> monitorGainDb { 0.f };   // -60..+12, -60 means -inf
    std::atomic<bool>  monitorEnabled { false };   // off until the user turns it on: mic + speakers feed back
    std::atomic<bool>  metronomeEnabled { true };
    std::atomic<float> metronomeGain { 0.5f };  // linear 0..1

    ParamTable params;
    std::array<std::atomic<bool>, kNumTracks> trackMute {}, trackSolo {};
    std::atomic<bool> fillHeld { false };
    std::atomic<int32_t> loopCompSamples { 0 };   // device latency (driver or calibration) + manual offset; written by the app
};

enum class LoopState : uint8_t { Idle, Armed, Recording };
enum class CalState : uint8_t { Idle, Running, Done, Failed };

struct LoopSnapshot
{
    LoopState state = LoopState::Idle;
    int8_t slot = -1, bars = 0;
    bool overdub = false;
    int64_t startSample = 0, lengthSamples = 0;   // record window in transport positions (no compensation)
    int64_t recIndex = -1;                        // loop samples written so far, -1 outside recording
    int64_t playIndex = -1;                       // LoopPlayer position in loop samples, -1 when silent
    int32_t compTotal = 0;
    int32_t pdcTracks = 0, pdcMaster = 0, pdcGuitar = 0;
    bool pdcOverflow = false;
    CalState cal = CalState::Idle;
    int32_t calSamples = -1;
};

struct TransportState
{
    bool playing = false;
    double bpm = kDefaultBpm;
    int64_t positionSamples = 0;
    double positionBeats = 0.0;
};

struct SequencerSnapshot
{
    int8_t currentPattern = 0, pendingPattern = -1;
    std::array<int16_t, kNumTracks> laneSlot {};   // slot under the playhead, -1 when stopped
    int16_t glitchSlot = -1;
    int8_t activePad = -1, pendingPad = -1;
    bool recording = false;
    uint32_t droppedCommands = 0;
    // stage 5: song
    bool songMode = false;
    int8_t songIndex = -1;        // playing entry, -1 outside the song or when it is not playable
    uint8_t songRepeat = 0;       // current repeat 0..repeats-1
    int8_t songCursor = 0;        // entry Play starts from (stopped) / pending jump (playing), -1 = none
    bool songEnded = false;
    int64_t songEndSample = -1;   // transport sample of the song end, -1 until reached
};

struct ShakalSnapshot
{
    std::array<float, kNumChains> macro {};
    std::array<uint8_t, kNumChains> engaged {};
    uint16_t overrideMask = 0;   // bit per chain
};

enum class EngineEventType : uint8_t
{
    TrackHit, StepRecorded, GlitchRecorded, PadState,
    LoopRecorded,      // track = slot
    LoopAborted,       // track = slot
    CalibrationDone,   // value = 1 when done, 0 when failed
    CaptureDone,       // closes the StepRecorded batch of CaptureHits
    SongEnded
};

struct EngineEvent
{
    EngineEventType type = EngineEventType::TrackHit;
    uint8_t track = 0;
    uint8_t step = 0;
    uint8_t value = 0;
};

struct MidiEvent
{
    uint8_t status = 0, data1 = 0, data2 = 0;
};

struct Retired
{
    enum class Kind : uint8_t { Pattern, Sample, Insert, Song, NoteMap };
    Kind kind = Kind::Pattern;
    const void* ptr = nullptr;
};

} // namespace jackal
