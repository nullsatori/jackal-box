#pragma once

#include <atomic>
#include <cstdint>

namespace jackal
{

constexpr double kMinBpm = 60.0, kMaxBpm = 300.0, kDefaultBpm = 172.0;
constexpr int kBeatsPerBar = 4;            // stage 1: fixed 4/4
constexpr int kCommandQueueCapacity = 256;

enum class CommandType : uint8_t { Play, Stop, SetBpm, SetInputChannel };

struct Command
{
    CommandType type = CommandType::Stop;
    float value = 0.f;
    int32_t ivalue = 0;
};

// Continuous parameters: UI writes, audio reads once per block (relaxed).
struct EngineParams
{
    std::atomic<float> monitorGainDb { 0.f };   // -60..+12, -60 means -inf
    std::atomic<bool>  monitorEnabled { true };
    std::atomic<bool>  metronomeEnabled { true };
    std::atomic<float> metronomeGain { 0.5f };  // linear 0..1
};

struct TransportState
{
    bool playing = false;
    double bpm = kDefaultBpm;
    int64_t positionSamples = 0;
    double positionBeats = 0.0;
};

} // namespace jackal
