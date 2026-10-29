#pragma once

#include "Project.h"

#include "Engine/EngineCore.h"

#include <array>
#include <atomic>
#include <string>
#include <string_view>
#include <vector>

namespace jackal
{

enum class RenderScope : uint8_t { Pattern, Song };

struct RenderOptions
{
    RenderScope scope = RenderScope::Song;
    int patternIndex = 0;
    int cycles = 1;
    double tailSeconds = 2.0;
    bool stems = true;
    int blockSize = 512;
};

struct RenderProgress
{
    std::atomic<double> fraction { 0.0 };
    std::atomic<bool> cancel { false };
};

struct RenderBuffers
{
    std::array<std::vector<float>, 2> master;
    std::array<std::array<std::vector<float>, 2>, kNumStems> stems;   // empty when options.stems == false
    std::array<bool, kNumStems> stemSilent {};                          // every sample exactly 0
    int64_t songEndSample = 0, totalSamples = 0;
    float peakMaster = 0.f;
};

enum class RenderStatus : uint8_t { Done, Cancelled, Error };

struct RenderResult
{
    RenderStatus status = RenderStatus::Error;
    std::string error;
    std::vector<InsertProcessor*> retiredInserts;   // the caller deletes them (on the thread that owns plugins)
};

// core: prepare(sr, blockSize, 0, 2) done, publishProject called, transport stopped.
// Any render is a song without loop; its length is known up front and checked against the engine's song end.
RenderResult renderOffline (EngineCore& core, const Project&, const RenderOptions&, RenderBuffers& out, RenderProgress* progress);

// Number of samples the render will produce (song or pattern length plus the tail) at this rate.
int64_t renderLength (const Project&, const RenderOptions&, double sampleRate, int64_t* songEnd = nullptr);

// "01_KICK.wav" ... "09_GUITAR.wav": index 0-based, name reduced to [A-Za-z0-9_-], empty -> "T<n>".
std::string stemFileName (int index, std::string_view name);
std::string sanitizeFileName (std::string_view name);

} // namespace jackal
