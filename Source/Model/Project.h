#pragma once

#include "Engine/ParamIds.h"
#include "Engine/PatternData.h"

#include <array>
#include <cstdint>
#include <string>
#include <vector>

namespace jackal
{

enum class SliceMode : uint8_t { Off, Grid16, Grid32, Transient };

struct SampleSlotSpec
{
    std::string source;                        // "builtin:kick" | "file"
    std::string path;                          // file: relative path inside the project, "samples/x.wav"
    SliceMode sliceMode = SliceMode::Off;
    double sourceBars = 0.0;                   // 0 = not a loop
    std::vector<int64_t> transientBoundaries;  // only for Transient
    bool used = false;
    bool operator== (const SampleSlotSpec&) const = default;
};

struct FxSettings
{
    int8_t preset = 0;     // ShakalPreset
    bool bypass = false;
    // full FxParam array; Bypass and Preset entries are ignored (the fields above are the source of truth)
    std::array<float, kNumFxParams> params = kFxParamDefaults;
    bool operator== (const FxSettings&) const = default;
};

struct TrackSettings
{
    std::string name;                          // <= 16 chars
    int8_t sampleSlot = -1;
    uint8_t chokeGroup = 0;
    bool fitToTempo = false;
    bool mute = false, solo = false;
    std::array<float, kNumTrackParams> params = kTrackParamDefaults;   // params[Shakal] is the track macro
    FxSettings fx {};
    bool operator== (const TrackSettings&) const = default;
};

struct UiState
{
    int selectedTrack = 0;
    int currentPattern = 0;
    int gridPage = 0;
    float mutationStrength = 0.5f;
    int trackPanelTab = 0;   // 0 SRC, 1 FX, 2 STEP, 3 MST
    bool operator== (const UiState&) const = default;
};

struct Project
{
    std::string name = "Untitled";
    double bpm = 172.0;
    uint32_t seed = 0x5A4C;
    float masterGainDb = kMasterGainDefaultDb;
    float masterShakal = 0.f;
    FxSettings masterFx {};
    std::array<SampleSlotSpec, kNumSampleSlots> slots {};
    std::array<TrackSettings, kNumTracks> tracks {};
    std::array<Pattern, kNumPatterns> patterns {};
    UiState ui {};
    bool operator== (const Project&) const = default;
};

} // namespace jackal
