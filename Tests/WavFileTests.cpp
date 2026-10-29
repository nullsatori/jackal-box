// Stage 5: WAV writer and reader (W1-W2). Files live only in a temp folder.

#include "RigHelpers.h"

#include "Model/WavFile.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>
#include <cstring>
#include <fstream>

using namespace jackal;
using namespace jackal::test;

namespace
{
    std::vector<uint8_t> bytesOf (const std::filesystem::path& p)
    {
        std::ifstream f (p, std::ios::binary);
        return { std::istreambuf_iterator<char> (f), std::istreambuf_iterator<char>() };
    }

    uint32_t u32 (const std::vector<uint8_t>& b, std::size_t at)
    {
        return static_cast<uint32_t> (b[at]) | (static_cast<uint32_t> (b[at + 1]) << 8) | (static_cast<uint32_t> (b[at + 2]) << 16)
             | (static_cast<uint32_t> (b[at + 3]) << 24);
    }

    uint32_t u16 (const std::vector<uint8_t>& b, std::size_t at)
    {
        return static_cast<uint32_t> (b[at]) | (static_cast<uint32_t> (b[at + 1]) << 8);
    }

    bool tagAt (const std::vector<uint8_t>& b, std::size_t at, const char* t) { return std::memcmp (b.data() + at, t, 4) == 0; }
} // namespace

TEST_CASE ("W1: WAV header bytes, reproducible files, read back", "[wav]")
{
    TempDir dir;
    constexpr int kN = 1000;
    std::vector<float> l (kN), r (kN);
    for (int i = 0; i < kN; ++i)
    {
        l[static_cast<std::size_t> (i)] = std::sin (static_cast<float> (i) * 0.05f) * 0.9f;
        r[static_cast<std::size_t> (i)] = static_cast<float> (i % 50) / 50.f - 0.5f;
    }
    const float* ch[2] = { l.data(), r.data() };

    for (auto depth : { WavDepth::Pcm16, WavDepth::Pcm24, WavDepth::Float32 })
    {
        const int bytes = depth == WavDepth::Pcm16 ? 2 : depth == WavDepth::Pcm24 ? 3 : 4;
        const bool isFloat = depth == WavDepth::Float32;
        INFO ("bytes " << bytes);
        const auto a = dir.path / ("a" + std::to_string (bytes) + ".wav");
        const auto b = dir.path / ("b" + std::to_string (bytes) + ".wav");
        REQUIRE (writeWav (a, ch, 2, kN, 48000.0, depth).ok);
        REQUIRE (writeWav (b, ch, 2, kN, 48000.0, depth).ok);
        REQUIRE (std::filesystem::exists (a));

        const auto fa = bytesOf (a);
        REQUIRE (fa == bytesOf (b));
        const uint32_t dataBytes = static_cast<uint32_t> (kN * 2 * bytes);
        REQUIRE (tagAt (fa, 0, "RIFF"));
        REQUIRE (u32 (fa, 4) == (isFloat ? 36u + 2u + 12u : 36u) + dataBytes);
        REQUIRE (tagAt (fa, 8, "WAVE"));
        REQUIRE (tagAt (fa, 12, "fmt "));
        REQUIRE (u32 (fa, 16) == (isFloat ? 18u : 16u));
        REQUIRE (u16 (fa, 20) == (isFloat ? 3u : 1u));
        REQUIRE (u16 (fa, 22) == 2u);
        REQUIRE (u32 (fa, 24) == 48000u);
        REQUIRE (u32 (fa, 28) == 48000u * 2u * static_cast<uint32_t> (bytes));
        REQUIRE (u16 (fa, 32) == 2u * static_cast<uint32_t> (bytes));
        REQUIRE (u16 (fa, 34) == 8u * static_cast<uint32_t> (bytes));
        std::size_t dataAt = 36;
        if (isFloat)
        {
            REQUIRE (u16 (fa, 36) == 0u);
            REQUIRE (tagAt (fa, 38, "fact"));
            REQUIRE (u32 (fa, 42) == 4u);
            REQUIRE (u32 (fa, 46) == static_cast<uint32_t> (kN));
            dataAt = 50;
        }
        REQUIRE (tagAt (fa, dataAt, "data"));
        REQUIRE (u32 (fa, dataAt + 4) == dataBytes);
        REQUIRE (fa.size() == dataAt + 8 + dataBytes);

        WavData w;
        std::string err;
        REQUIRE (readWav (a, w, err));
        REQUIRE (w.numChannels == 2);
        REQUIRE (w.sampleRate == 48000.0);
        REQUIRE (w.length == kN);
        const double tol = depth == WavDepth::Pcm16 ? std::ldexp (1.0, -15) : std::ldexp (1.0, -23);
        for (int i = 0; i < kN; ++i)
            for (int c = 0; c < 2; ++c)
            {
                const float want = ch[c][i];
                const float got = w.data[static_cast<std::size_t> (c)][static_cast<std::size_t> (i)];
                if (isFloat)
                    REQUIRE (std::memcmp (&want, &got, 4) == 0);
                else
                    REQUIRE (std::fabs (static_cast<double> (want) - got) <= tol);
            }
    }

    // mono
    const float* mono[1] = { l.data() };
    const auto m = dir.path / "mono.wav";
    REQUIRE (writeWav (m, mono, 1, kN, 44100.0, WavDepth::Pcm24).ok);
    WavData w;
    std::string err;
    REQUIRE (readWav (m, w, err));
    REQUIRE (w.numChannels == 1);
    REQUIRE (w.sampleRate == 44100.0);
    REQUIRE (w.length == kN);
}

TEST_CASE ("W2: clipping, bad folders, foreign files", "[wav]")
{
    TempDir dir;
    std::vector<float> l (100, 0.25f), r (100, -0.25f);
    l[3] = 1.5f;
    l[10] = -2.f;
    r[10] = 3.f;   // same frame as l[10]: counts once
    r[20] = 1.0001f;
    l[30] = -1.1f;
    r[40] = 7.f;
    l[50] = 1.01f;
    r[60] = -1.01f;
    const float* ch[2] = { l.data(), r.data() };

    for (auto depth : { WavDepth::Pcm16, WavDepth::Pcm24 })
    {
        const auto p = dir.path / "clip.wav";
        const auto res = writeWav (p, ch, 2, 100, 48000.0, depth);
        REQUIRE (res.ok);
        REQUIRE (res.clippedSamples == 7);
        WavData w;
        std::string err;
        REQUIRE (readWav (p, w, err));
        REQUIRE (w.data[0][3] == 1.f);
        REQUIRE (w.data[0][10] == -1.f);
        REQUIRE (w.data[1][40] == 1.f);
    }

    const auto f = dir.path / "float.wav";
    const auto res = writeWav (f, ch, 2, 100, 48000.0, WavDepth::Float32);
    REQUIRE (res.ok);
    REQUIRE (res.clippedSamples == 0);
    WavData w;
    std::string err;
    REQUIRE (readWav (f, w, err));
    REQUIRE (w.data[1][40] == 7.f);

    const auto bad = writeWav (dir.path / "no" / "such" / "x.wav", ch, 2, 100, 48000.0, WavDepth::Pcm16);
    REQUIRE (! bad.ok);
    REQUIRE (! bad.error.empty());

    const auto junk = dir.path / "junk.wav";
    {
        std::ofstream o (junk, std::ios::binary);
        o << "this is not a wav file at all";
    }
    REQUIRE (! readWav (junk, w, err));
    REQUIRE (! readWav (dir.path / "missing.wav", w, err));
}
