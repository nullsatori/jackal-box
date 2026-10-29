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
    std::string source;                        // "builtin:kick" | "file" | "loop" (stage 4, path loops/<slot>-<n>.wav)
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

// One VST3 insert slot (stage 4). Empty identifier = free slot.
struct InsertSlotSpec
{
    std::string identifier;    // PluginDescription::createIdentifierString()
    std::string name;          // shown, also when the plugin is MISSING
    std::string stateBase64;   // getStateInformation, base64; the model never decodes it
    bool bypass = false;
    bool empty() const noexcept { return identifier.empty(); }
    bool operator== (const InsertSlotSpec&) const = default;
};

using InsertSlots = std::array<InsertSlotSpec, kSlotsPerChain>;

enum class LoopTap : uint8_t { Di, Amp };

struct GuitarSettings
{
    float shakal = 0.f;
    FxSettings fx {};
    float gateDb = -80.f, hpfHz = 20.f, loopGainDb = 0.f;
    LoopTap loopTap = LoopTap::Amp;
    InsertSlots inserts {};
    bool operator== (const GuitarSettings&) const = default;
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
    InsertSlots inserts {};
    bool operator== (const TrackSettings&) const = default;
};

struct UiState
{
    int selectedTrack = 0;
    int currentPattern = 0;
    int gridPage = 0;
    float mutationStrength = 0.5f;
    int trackPanelTab = 0;   // 0 SRC, 1 FX, 2 VST, 3 STEP, 4 MST, 5 GTR (stage 4)
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
    InsertSlots masterInserts {};
    GuitarSettings guitar {};
    std::array<SampleSlotSpec, kNumSampleSlots> slots {};
    std::array<TrackSettings, kNumTracks> tracks {};
    std::array<Pattern, kNumPatterns> patterns {};
    UiState ui {};
    bool operator== (const Project&) const = default;
};

} // namespace jackal
