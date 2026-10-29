#pragma once

#include <array>
#include <cstdint>

namespace jackal
{

constexpr int kNumTracks = 8, kMaxSteps = 64, kNumPatterns = 8, kMaxBars = 4;
constexpr int kMaxRatchet = 64, kMaxPlocksPerStep = 4, kNumPads = 8, kNumSampleSlots = 16;
constexpr int kMinSwing = 50, kMaxSwing = 75;      // 50 = straight
constexpr int kMaxNudgePercent = 25;               // +-25 % of a step
constexpr uint8_t kNoPLock = 0xFF;

enum class LaneSpeed : uint8_t { Half, Normal, Double, Triplet };

// Step length in beats: Half 0.5, Normal 0.25, Double 0.125, Triplet 1/6.
constexpr double stepBeats (LaneSpeed s) noexcept
{
    switch (s)
    {
        case LaneSpeed::Half:    return 0.5;
        case LaneSpeed::Normal:  return 0.25;
        case LaneSpeed::Double:  return 0.125;
        case LaneSpeed::Triplet: return 1.0 / 6.0;
    }
    return 0.25;
}

enum class StepCondition : uint8_t { Always, Fill, NotFill, FirstCycle, Cycle1of2, Cycle2of2 };

struct PLock
{
    uint8_t param = kNoPLock;   // TrackParam, kNoPLock = empty
    float value = 0.f;
    bool operator== (const PLock&) const = default;
};

struct Step
{
    bool    active = false;
    uint8_t velocity = 100;        // 1..127, gain = velocity / 127
    uint8_t ratchet = 1;           // 1 = none, 2..64 hits inside the step
    int8_t  ratchetVelRamp = 0;    // -100..100 %
    int8_t  ratchetPitchRamp = 0;  // -24..24 semitones over the roll
    int8_t  pitch = 0;             // -24..24 semitones on top of TrackParam::Pitch
    int8_t  slice = -1;            // -1 = auto (slot % numSlices), else 0..63
    bool    reverse = false;
    uint8_t probability = 100;     // 0..100
    StepCondition condition = StepCondition::Always;
    int8_t  nudge = 0;             // -25..25 % of a step
    std::array<PLock, kMaxPlocksPerStep> plocks {};
    bool operator== (const Step&) const = default;
};

struct Lane
{
    uint8_t length = 16;           // 1..64
    LaneSpeed speed = LaneSpeed::Normal;
    std::array<Step, kMaxSteps> steps {};
    bool operator== (const Lane&) const = default;
};

struct GlitchLane                  // 1/16 grid, length = bars * 16
{
    std::array<uint8_t, kMaxSteps> pad {};   // 0 = none, 1..8 = pad held on this step
    bool operator== (const GlitchLane&) const = default;
};

struct Pattern
{
    uint8_t bars = 1;              // 1..4
    uint8_t swing = 50;            // 50..75
    std::array<Lane, kNumTracks> lanes {};
    GlitchLane glitch {};
    bool operator== (const Pattern&) const = default;
};

} // namespace jackal
