#pragma once

// Offline rig for stage 2 tests: drives EngineCore exactly like ProjectController does,
// without JUCE and without any audio device. Nothing here ever makes a sound.

#include "Engine/EngineCore.h"
#include "Model/DemoKit.h"
#include "Model/OfflineRender.h"
#include "Model/ProjectPublisher.h"
#include "Model/Project.h"
#include "Model/Slicer.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#ifdef _WIN32
#include <process.h>
#else
#include <unistd.h>
#endif
#include <initializer_list>
#include <memory>
#include <set>
#include <vector>

namespace jackal::test
{

inline Sample* makeImpulse (double sampleRate = 48000.0, int64_t length = 1, float value = 1.f)
{
    auto* s = new Sample();
    s->sampleRate = sampleRate;
    s->length = length;
    s->data[0].assign (static_cast<std::size_t> (length), 0.f);
    s->data[0][0] = value;
    return s;
}

inline Sample* makeRamp (int64_t n, double sampleRate = 48000.0)
{
    auto* s = new Sample();
    s->sampleRate = sampleRate;
    s->length = n;
    s->data[0].resize (static_cast<std::size_t> (n));
    for (int64_t i = 0; i < n; ++i)
        s->data[0][static_cast<std::size_t> (i)] = static_cast<float> (i + 1) / static_cast<float> (n);
    return s;
}

inline Sample* makeDc (int64_t n, float value = 1.f, double sampleRate = 48000.0)
{
    auto* s = new Sample();
    s->sampleRate = sampleRate;
    s->length = n;
    s->data[0].assign (static_cast<std::size_t> (n), value);
    return s;
}

inline std::vector<int64_t> onsets (const std::vector<float>& x, int silenceRun = 16)
{
    std::vector<int64_t> out;
    int zeros = silenceRun;
    for (std::size_t i = 0; i < x.size(); ++i)
    {
        if (x[i] != 0.f)
        {
            if (zeros >= silenceRun)
                out.push_back (static_cast<int64_t> (i));
            zeros = 0;
        }
        else
            ++zeros;
    }
    return out;
}

struct OfflineRig
{
    EngineCore core;
    double sampleRate;
    int bufferSize;
    std::vector<float> left, right;
    std::vector<EngineEvent> events;
    int retiredCount = 0;
    std::vector<const void*> retiredPtrs;
    std::vector<int64_t> eventBlocks;                 // block start of each entry in `events`
    std::set<const Sample*> retiredSamples;           // deleted in the destructor (a test may republish one)
    // Optional live input, one channel: fills `in` for the block [pos, pos + n). Never a device, never a sound.
    std::function<void (int64_t pos, int n, float* in)> input;

    explicit OfflineRig (int buffer = 128, double sr = 48000.0, int maxBlock = -1)
        : sampleRate (sr), bufferSize (buffer)
    {
        core.prepare (sr, maxBlock > 0 ? maxBlock : buffer, 0, 2);
        core.params().monitorEnabled = false;
        core.params().metronomeEnabled = false;
    }

    ~OfflineRig()
    {
        core.releaseAll();
        drain();
        for (const Sample* smp : retiredSamples)
            delete smp;
    }

    void cmd (CommandType t, int ivalue = 0, float value = 0.f, const void* ptr = nullptr)
    {
        core.commands().push ({ t, value, ivalue, ptr });
    }

    void setSample (int slot, const Sample* s) { cmd (CommandType::SetSlotSample, slot, 0.f, s); }
    void setTrack (int track, int slot, int choke = 0, bool fit = false)
    {
        cmd (CommandType::SetTrackSlot, track | ((slot < 0 ? 0xFF : slot) << 8));
        cmd (CommandType::SetTrackChoke, track | (choke << 8));
        cmd (CommandType::SetTrackFit, track | ((fit ? 1 : 0) << 8));
    }
    void setPattern (int index, const Pattern& p) { cmd (CommandType::SetPattern, index, 0.f, new Pattern (p)); }
    void bpm (double b) { cmd (CommandType::SetBpm, 0, static_cast<float> (b)); }
    void play() { cmd (CommandType::Play); }
    void param (int track, TrackParam p, float v) { core.params().params.set (trackParam (track, p), v); }
    void masterGainDb (float db) { core.params().params.set (masterParam (MasterParam::Gain), db); }
    void stop() { cmd (CommandType::Stop); }

    // stage 5
    const SongData* setSong (const SongData& s) { auto* p = new SongData (s); cmd (CommandType::SetSong, 0, 0.f, p); return p; }
    void songMode (bool on) { cmd (CommandType::SetSongMode, on ? 1 : 0); }
    void songPosition (int i) { cmd (CommandType::SetSongPosition, i); }
    const MidiNoteMap* noteMap (const MidiNoteMap* m) { cmd (CommandType::SetNoteMap, 0, 0.f, m); return m; }
    const MidiNoteMap* noteMap (const MidiNoteMap& m) { return noteMap (new MidiNoteMap (m)); }
    void capture (int bars) { cmd (CommandType::CaptureHits, bars); }
    void midi (uint8_t status, uint8_t d1, uint8_t d2) { core.midi().push ({ status, d1, d2 }); }
    SequencerSnapshot snapshot() const { return core.sequencerSnapshot(); }

    void drain (int64_t blockStart = -1)
    {
        Retired r;
        while (core.retired().pop (r))
        {
            ++retiredCount;
            retiredPtrs.push_back (r.ptr);
            if (r.kind == Retired::Kind::Pattern)
                delete static_cast<const Pattern*> (r.ptr);
            else if (r.kind == Retired::Kind::Sample)
                retiredSamples.insert (static_cast<const Sample*> (r.ptr));
            else if (r.kind == Retired::Kind::Song)
                delete static_cast<const SongData*> (r.ptr);
            else if (r.kind == Retired::Kind::NoteMap)
                delete static_cast<const MidiNoteMap*> (r.ptr);
            // Insert: the test owns its FakeInsert objects
        }
        EngineEvent e;
        while (core.events().pop (e))
        {
            events.push_back (e);
            eventBlocks.push_back (blockStart);
        }
    }

    int countRetired (const void* p) const
    {
        return static_cast<int> (std::count (retiredPtrs.begin(), retiredPtrs.end(), p));
    }

    int64_t position() const { return static_cast<int64_t> (left.size()); }

    // Renders until the total output reaches `endSample`, in blocks of bufferSize (the last one clipped).
    void renderUntil (int64_t endSample, const std::function<void (int64_t)>& beforeBlock = {})
    {
        std::vector<float> l (static_cast<std::size_t> (bufferSize)), r (l.size()), inBuf (l.size());
        float* out[2] = { l.data(), r.data() };
        const float* in[1] = { inBuf.data() };
        while (position() < endSample)
        {
            if (beforeBlock)
                beforeBlock (position());
            const int64_t start = position();
            const int n = static_cast<int> (std::min<int64_t> (bufferSize, endSample - start));
            if (input)
            {
                std::fill (inBuf.begin(), inBuf.end(), 0.f);
                input (start, n, inBuf.data());
                core.process (in, 1, out, 2, n);
            }
            else
            {
                core.process (nullptr, 0, out, 2, n);
            }
            left.insert (left.end(), l.begin(), l.begin() + n);
            right.insert (right.end(), r.begin(), r.begin() + n);
            drain (start);
        }
    }

    void renderSeconds (double s) { renderUntil (position() + static_cast<int64_t> (std::llround (s * sampleRate))); }

    // Same path as ProjectController::loadProject, minus files (stage 5: through publishProject).
    void loadProject (Project& p, const PublishedInserts& inserts = {})
    {
        publishProject (core, p, builtinSamples (p, sampleRate), inserts);
    }

    static std::array<Sample*, kNumSampleSlots> builtinSamples (const Project& p, double sr)
    {
        std::array<Sample*, kNumSampleSlots> out {};
        for (int s = 0; s < kNumSampleSlots; ++s)
        {
            const auto& spec = p.slots[static_cast<std::size_t> (s)];
            if (! spec.used || spec.source.rfind ("builtin:", 0) != 0)
                continue;
            auto sample = makeBuiltin (spec.source.substr (8), sr);
            if (sample == nullptr)
                continue;
            auto specCopy = spec;
            applySlicing (*sample, specCopy);
            out[static_cast<std::size_t> (s)] = sample.release();
        }
        return out;
    }
};

inline SongData songOf (std::initializer_list<std::pair<int, int>> entries, bool loop = true)
{
    SongData s;
    s.loop = loop;
    for (const auto& [p, n] : entries)
        s.entries[s.length++] = { static_cast<int8_t> (p), static_cast<uint8_t> (n) };
    return s;
}

// EngineCore + publishProject + renderOffline. Samples and inserts pass to the engine; inserts come back in result.
struct RenderRun
{
    RenderBuffers buffers;
    jackal::RenderResult result;
};

inline RenderRun renderToBuffers (const Project& p, RenderOptions options, int blockSize, std::array<Sample*, kNumSampleSlots> samples,
                                  const PublishedInserts& inserts = {}, RenderProgress* progress = nullptr, double sr = 48000.0)
{
    RenderRun run;
    EngineCore core;
    core.prepare (sr, blockSize, 0, 2);
    publishProject (core, p, samples, inserts);
    options.blockSize = blockSize;
    run.result = renderOffline (core, p, options, run.buffers, progress);
    return run;
}

// Unique temp folder, removed with everything in it when the helper goes away. Tests write WAVs only here.
struct TempDir
{
    std::filesystem::path path;
    TempDir()
    {
        static int counter = 0;
#ifdef _WIN32
        const long pid = static_cast<long> (_getpid());
#else
        const long pid = static_cast<long> (getpid());
#endif
        path = std::filesystem::temp_directory_path() / ("jackal-tests-" + std::to_string (pid) + "-" + std::to_string (++counter));
        std::filesystem::create_directories (path);
    }
    ~TempDir()
    {
        std::error_code ec;
        std::filesystem::remove_all (path, ec);
    }
};

// A pattern on the heap with one lane filled.
inline std::unique_ptr<Pattern> laneAll (int lane, int length = 16, LaneSpeed speed = LaneSpeed::Normal)
{
    auto p = std::make_unique<Pattern>();
    auto& l = p->lanes[static_cast<std::size_t> (lane)];
    l.length = static_cast<uint8_t> (length);
    l.speed = speed;
    for (int i = 0; i < length; ++i)
        l.steps[static_cast<std::size_t> (i)].active = true;
    return p;
}

} // namespace jackal::test
