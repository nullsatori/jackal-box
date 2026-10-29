#include "WavFile.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <system_error>

namespace jackal
{

namespace
{
    void put16 (std::vector<uint8_t>& b, uint32_t v)
    {
        b.push_back (static_cast<uint8_t> (v & 0xFF));
        b.push_back (static_cast<uint8_t> ((v >> 8) & 0xFF));
    }

    void put32 (std::vector<uint8_t>& b, uint32_t v)
    {
        put16 (b, v & 0xFFFF);
        put16 (b, v >> 16);
    }

    void tag (std::vector<uint8_t>& b, const char* t) { b.insert (b.end(), t, t + 4); }

    uint32_t get16 (const uint8_t* p) { return static_cast<uint32_t> (p[0]) | (static_cast<uint32_t> (p[1]) << 8); }
    uint32_t get32 (const uint8_t* p) { return get16 (p) | (get16 (p + 2) << 16); }
} // namespace

WavWriteResult writeWav (const std::filesystem::path& path, const float* const* channels, int numChannels, int64_t length,
                         double sampleRate, WavDepth depth)
{
    WavWriteResult r;
    if (numChannels < 1 || numChannels > 2 || length < 0 || channels == nullptr)
    {
        r.error = "bad arguments";
        return r;
    }

    const bool isFloat = depth == WavDepth::Float32;
    const int bytes = depth == WavDepth::Pcm16 ? 2 : (depth == WavDepth::Pcm24 ? 3 : 4);
    const auto blockAlign = static_cast<uint32_t> (bytes * numChannels);
    const auto dataBytes = static_cast<uint64_t> (length) * blockAlign;
    if (dataBytes > 0xFFFFFFFFull - 64)
    {
        r.error = "file too large for WAV";
        return r;
    }
    const auto rate = static_cast<uint32_t> (std::llround (sampleRate));

    std::vector<uint8_t> b;
    b.reserve (static_cast<std::size_t> (dataBytes) + 64);
    tag (b, "RIFF");
    put32 (b, static_cast<uint32_t> ((isFloat ? 36 + 2 + 12 : 36) + dataBytes));
    tag (b, "WAVE");
    tag (b, "fmt ");
    put32 (b, isFloat ? 18 : 16);
    put16 (b, isFloat ? 3 : 1);
    put16 (b, static_cast<uint32_t> (numChannels));
    put32 (b, rate);
    put32 (b, rate * blockAlign);
    put16 (b, blockAlign);
    put16 (b, static_cast<uint32_t> (bytes * 8));
    if (isFloat)
    {
        put16 (b, 0);   // cbSize
        tag (b, "fact");
        put32 (b, 4);
        put32 (b, static_cast<uint32_t> (length));
    }
    tag (b, "data");
    put32 (b, static_cast<uint32_t> (dataBytes));

    for (int64_t i = 0; i < length; ++i)
    {
        bool clipped = false;
        for (int c = 0; c < numChannels; ++c)
        {
            const float x = channels[c] != nullptr ? channels[c][i] : 0.f;
            if (isFloat)
            {
                uint32_t bits = 0;
                std::memcpy (&bits, &x, 4);
                put32 (b, bits);
                continue;
            }
            if (std::fabs (x) > 1.f || std::isnan (x))
                clipped = true;
            const double v = std::isnan (x) ? 0.0 : std::clamp (static_cast<double> (x), -1.0, 1.0);
            if (depth == WavDepth::Pcm16)
            {
                const auto q = static_cast<int32_t> (std::lround (v * 32767.0));
                put16 (b, static_cast<uint32_t> (q) & 0xFFFF);
            }
            else
            {
                const auto q = static_cast<int32_t> (std::lround (v * 8388607.0));
                const auto u = static_cast<uint32_t> (q);
                b.push_back (static_cast<uint8_t> (u & 0xFF));
                b.push_back (static_cast<uint8_t> ((u >> 8) & 0xFF));
                b.push_back (static_cast<uint8_t> ((u >> 16) & 0xFF));
            }
        }
        if (clipped)
            ++r.clippedSamples;
    }

    std::error_code ec;
    const auto dir = path.parent_path();
    if (! dir.empty() && ! std::filesystem::is_directory (dir, ec))
    {
        r.error = "folder does not exist: " + dir.string();
        return r;
    }

    auto tmp = path;
    tmp += ".tmp";
    {
        std::ofstream f (tmp, std::ios::binary | std::ios::trunc);
        if (! f)
        {
            r.error = "cannot write " + tmp.string();
            return r;
        }
        f.write (reinterpret_cast<const char*> (b.data()), static_cast<std::streamsize> (b.size()));
        if (! f)
        {
            r.error = "write failed: " + tmp.string();
            return r;
        }
    }
    std::filesystem::rename (tmp, path, ec);
    if (ec)
    {
        std::filesystem::remove (path, ec);
        std::filesystem::rename (tmp, path, ec);
        if (ec)
        {
            r.error = "cannot rename to " + path.string() + ": " + ec.message();
            std::filesystem::remove (tmp, ec);
            return r;
        }
    }
    r.ok = true;
    return r;
}

bool readWav (const std::filesystem::path& path, WavData& out, std::string& error)
{
    out = {};
    std::ifstream f (path, std::ios::binary);
    if (! f)
    {
        error = "cannot open " + path.string();
        return false;
    }
    std::vector<uint8_t> b ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());
    if (b.size() < 12 || std::memcmp (b.data(), "RIFF", 4) != 0 || std::memcmp (b.data() + 8, "WAVE", 4) != 0)
    {
        error = "not a RIFF/WAVE file";
        return false;
    }

    int format = 0, channels = 0, bits = 0;
    uint32_t rate = 0;
    const uint8_t* data = nullptr;
    uint32_t dataSize = 0;
    std::size_t pos = 12;
    while (pos + 8 <= b.size())
    {
        const uint8_t* ck = b.data() + pos;
        const uint32_t size = get32 (ck + 4);
        const std::size_t body = pos + 8;
        if (body + size > b.size())
        {
            if (std::memcmp (ck, "data", 4) == 0)
            {
                data = b.data() + body;
                dataSize = static_cast<uint32_t> (b.size() - body);
            }
            break;
        }
        if (std::memcmp (ck, "fmt ", 4) == 0 && size >= 16)
        {
            format = static_cast<int> (get16 (ck + 8));
            channels = static_cast<int> (get16 (ck + 10));
            rate = get32 (ck + 12);
            bits = static_cast<int> (get16 (ck + 22));
            if (format == 0xFFFE && size >= 40)   // WAVE_FORMAT_EXTENSIBLE: sub format GUID starts with the tag
                format = static_cast<int> (get16 (ck + 32));
        }
        else if (std::memcmp (ck, "data", 4) == 0)
        {
            data = b.data() + body;
            dataSize = size;
        }
        pos = body + size + (size & 1);
    }

    if (data == nullptr || channels < 1 || channels > 2 || rate == 0)
    {
        error = "unsupported WAV layout";
        return false;
    }
    if (format == 1 && bits == 16)       out.depth = WavDepth::Pcm16;
    else if (format == 1 && bits == 24)  out.depth = WavDepth::Pcm24;
    else if (format == 3 && bits == 32)  out.depth = WavDepth::Float32;
    else
    {
        error = "unsupported WAV sample format";
        return false;
    }

    const int bytes = bits / 8;
    const int64_t frames = dataSize / static_cast<uint32_t> (bytes * channels);
    out.numChannels = channels;
    out.sampleRate = rate;
    out.length = frames;
    for (int c = 0; c < channels; ++c)
        out.data[static_cast<std::size_t> (c)].resize (static_cast<std::size_t> (frames));

    const uint8_t* p = data;
    for (int64_t i = 0; i < frames; ++i)
        for (int c = 0; c < channels; ++c, p += bytes)
        {
            float v = 0.f;
            if (out.depth == WavDepth::Pcm16)
                v = static_cast<float> (static_cast<int16_t> (get16 (p))) / 32767.f;
            else if (out.depth == WavDepth::Pcm24)
            {
                int32_t q = static_cast<int32_t> (p[0] | (p[1] << 8) | (p[2] << 16));
                if (q & 0x800000)
                    q -= 0x1000000;
                v = static_cast<float> (static_cast<double> (q) / 8388607.0);
            }
            else
            {
                const uint32_t bitsv = get32 (p);
                std::memcpy (&v, &bitsv, 4);
            }
            out.data[static_cast<std::size_t> (c)][static_cast<std::size_t> (i)] = v;
        }
    return true;
}

} // namespace jackal
