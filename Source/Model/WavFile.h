#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace jackal
{

enum class WavDepth : uint8_t { Pcm16, Pcm24, Float32 };

struct WavWriteResult
{
    bool ok = false;
    std::string error;
    int64_t clippedSamples = 0;   // PCM only: sample frames with |x| > 1 in any channel
};

// RIFF/WAVE, no LIST chunk, byte-for-byte reproducible. Writes to a temp file next to `path`, then renames.
WavWriteResult writeWav (const std::filesystem::path&, const float* const* channels, int numChannels /*1..2*/, int64_t length,
                         double sampleRate, WavDepth);

struct WavData
{
    int numChannels = 0;
    double sampleRate = 0;
    WavDepth depth = WavDepth::Pcm16;
    int64_t length = 0;
    std::array<std::vector<float>, 2> data;
};

bool readWav (const std::filesystem::path&, WavData&, std::string& error);   // PCM 16/24, float 32, 1-2 channels

} // namespace jackal
