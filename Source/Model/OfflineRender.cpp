#include "OfflineRender.h"

#include "SongOps.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace jackal
{

namespace
{
    template <typename T>
    constexpr std::size_t ix (T i) noexcept { return static_cast<std::size_t> (i); }

    SongData jobSong (const Project& p, const RenderOptions& o)
    {
        SongData job;
        if (o.scope == RenderScope::Pattern)
        {
            job.length = 1;
            job.entries[0] = { static_cast<int8_t> (std::clamp (o.patternIndex, 0, kNumPatterns - 1)),
                               static_cast<uint8_t> (std::clamp (o.cycles, 1, kMaxRepeats)) };
        }
        else
        {
            job = p.song;
        }
        job.loop = false;
        return job;
    }

    // Same arithmetic as the engine: SetBpm carries a float, the transport anchors at 0.
    int64_t sampleAtBeat (const Project& p, double sampleRate, double beats)
    {
        Transport t;
        t.prepare (sampleRate);
        t.setBpm (static_cast<double> (static_cast<float> (p.bpm)));
        return t.sampleAtBeat (beats);
    }

    void drain (EngineCore& core, RenderResult& r)
    {
        Retired x;
        while (core.retired().pop (x))
        {
            switch (x.kind)
            {
                case Retired::Kind::Pattern: delete static_cast<const Pattern*> (x.ptr); break;
                case Retired::Kind::Sample:  delete static_cast<const Sample*> (x.ptr); break;
                case Retired::Kind::Song:    delete static_cast<const SongData*> (x.ptr); break;
                case Retired::Kind::NoteMap: delete static_cast<const MidiNoteMap*> (x.ptr); break;
                case Retired::Kind::Insert:
                    r.retiredInserts.push_back (static_cast<InsertProcessor*> (const_cast<void*> (x.ptr)));
                    break;
            }
        }
        EngineEvent e;
        while (core.events().pop (e)) {}
    }

    void finish (EngineCore& core, RenderResult& r)
    {
        core.commands().push ({ CommandType::Stop, 0.f, 0 });
        core.releaseAll();
        drain (core, r);
    }
} // namespace

int64_t renderLength (const Project& p, const RenderOptions& o, double sampleRate, int64_t* songEnd)
{
    const SongData job = jobSong (p, o);
    const int64_t end = sampleAtBeat (p, sampleRate, songops::totalBeats (job, p.patterns));
    if (songEnd != nullptr)
        *songEnd = end;
    return end + static_cast<int64_t> (std::llround (std::clamp (o.tailSeconds, 0.0, 60.0) * sampleRate));
}

RenderResult renderOffline (EngineCore& core, const Project& project, const RenderOptions& options, RenderBuffers& out,
                            RenderProgress* progress)
{
    RenderResult r;
    out = {};

    const SongData job = jobSong (project, options);
    if (! songops::playable (job))
    {
        r.status = RenderStatus::Error;
        r.error = "song is empty";
        // apply what publishProject queued, so releaseAll hands every sample back
        float l = 0.f, rr = 0.f;
        float* outs[2] = { &l, &rr };
        core.process (nullptr, 0, outs, 2, 1);
        finish (core, r);
        return r;
    }

    const double sampleRate = core.sampleRate();
    int64_t expectedEnd = 0;
    const int64_t total = renderLength (project, options, sampleRate, &expectedEnd);

    core.params().monitorEnabled = false;
    core.params().metronomeEnabled = false;
    core.setNonRealtime (true);
    auto& q = core.commands();
    q.push ({ CommandType::SetSong, 0.f, 0, new SongData (job) });
    q.push ({ CommandType::SetSongMode, 0.f, 1 });
    q.push ({ CommandType::SetSongPosition, 0.f, 0 });
    q.push ({ CommandType::Play, 0.f, 0 });

    const int block = std::max (1, std::min (options.blockSize, core.maxBlockSize()));
    std::vector<float> l (ix (block)), rr (ix (block));
    float* outPtrs[2] = { l.data(), rr.data() };

    for (auto& ch : out.master)
        ch.reserve (ix (total));
    if (options.stems)
        for (auto& st : out.stems)
            for (auto& ch : st)
                ch.reserve (ix (total));

    int64_t pos = 0;
    while (pos < total)
    {
        if (progress != nullptr && progress->cancel.load (std::memory_order_relaxed))
        {
            finish (core, r);
            r.status = RenderStatus::Cancelled;
            return r;
        }

        const int n = static_cast<int> (std::min<int64_t> (block, total - pos));
        core.process (nullptr, 0, outPtrs, 2, n);
        out.master[0].insert (out.master[0].end(), l.begin(), l.begin() + n);
        out.master[1].insert (out.master[1].end(), rr.begin(), rr.begin() + n);

        if (options.stems)
        {
            const StemTaps taps = core.stemTaps();
            for (int t = 0; t < kNumTracks; ++t)
                for (int c = 0; c < 2; ++c)
                {
                    const float* src = taps.track[ix (2 * t + c)];
                    auto& dst = out.stems[ix (t)][ix (c)];
                    dst.insert (dst.end(), src, src + n);
                }
            for (int c = 0; c < 2; ++c)
            {
                const float* src = taps.guitar[ix (c)];
                auto& dst = out.stems[ix (kNumTracks)][ix (c)];
                dst.insert (dst.end(), src, src + n);
            }
        }

        drain (core, r);
        pos += n;
        if (progress != nullptr)
            progress->fraction.store (static_cast<double> (pos) / static_cast<double> (total), std::memory_order_relaxed);
    }

    // with no tail the song end sits exactly on the last sample boundary: one discarded sample lets the engine see it
    if (! core.sequencerSnapshot().songEnded)
    {
        core.process (nullptr, 0, outPtrs, 2, 1);
        drain (core, r);
    }

    const auto snap = core.sequencerSnapshot();
    const bool endOk = snap.songEnded && snap.songEndSample == expectedEnd;
    finish (core, r);
    if (! endOk)
    {
        r.status = RenderStatus::Error;
        r.error = "song end mismatch";
        return r;
    }

    out.songEndSample = expectedEnd;
    out.totalSamples = total;
    float peak = 0.f;
    for (const auto& ch : out.master)
        for (float x : ch)
            peak = std::max (peak, std::fabs (x));
    out.peakMaster = peak;
    for (int s = 0; s < kNumStems; ++s)
    {
        bool silent = true;
        if (options.stems)
            for (const auto& ch : out.stems[ix (s)])
                silent = silent && std::all_of (ch.begin(), ch.end(), [] (float x) { return x == 0.f; });
        out.stemSilent[ix (s)] = silent;
    }
    if (progress != nullptr)
        progress->fraction.store (1.0, std::memory_order_relaxed);
    r.status = RenderStatus::Done;
    return r;
}

std::string sanitizeFileName (std::string_view name)
{
    std::string s;
    for (char c : name)
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_' || c == '-')
            s.push_back (c);
    return s;
}

std::string stemFileName (int index, std::string_view name)
{
    std::string s = sanitizeFileName (name);
    if (s.empty())
        s = "T" + std::to_string (index + 1);
    char num[16];
    std::snprintf (num, sizeof num, "%02d_", index + 1);
    return std::string (num) + s + ".wav";
}

} // namespace jackal
