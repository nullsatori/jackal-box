// Stage 5: offline render (RX1-RX6) and the stage 4 regression (E5). Offline, nothing is played; WAVs only in a temp folder.

#include "FakeInsert.h"
#include "RigHelpers.h"

#include "Model/WavFile.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <thread>
#include <cmath>
#include <cstring>

using namespace jackal;
using namespace jackal::test;

namespace
{
    uint64_t fnv (const std::vector<float>& a, const std::vector<float>& b)
    {
        uint64_t h = 1469598103934665603ull;
        for (const auto* v : { &a, &b })
            for (float x : *v)
            {
                uint32_t bits = 0;
                std::memcpy (&bits, &x, 4);
                for (int i = 0; i < 4; ++i)
                {
                    h ^= (bits >> (8 * i)) & 0xFFu;
                    h *= 1099511628211ull;
                }
            }
        return h;
    }

    int64_t at (double bpm, double beats, double sr = 48000.0)
    {
        Transport t;
        t.prepare (sr);
        t.setBpm (static_cast<double> (static_cast<float> (bpm)));
        return t.sampleAtBeat (beats);
    }

    // RX1 project: demo kit with SHAKAL on the tracks, presets round robin, master NET 0.4, probability, glitch lane in B.
    std::unique_ptr<Project> rx1Project()
    {
        auto p = makeDemoProject (48000.0);
        const float macro[kNumTracks] { 0.5f, 0.f, 0.8f, 0.3f, 0.f, 0.6f, 0.2f, 0.9f };
        for (int t = 0; t < kNumTracks; ++t)
        {
            auto& tr = p->tracks[static_cast<std::size_t> (t)];
            tr.params[static_cast<std::size_t> (TrackParam::Shakal)] = macro[t];
            tr.fx.preset = static_cast<int8_t> (t % 3);
        }
        p->masterShakal = 0.4f;
        p->masterFx.preset = 1;   // NET
        p->patterns[0].lanes[3].steps[2].probability = 50;
        p->patterns[1].lanes[0].steps[0].probability = 50;
        for (int c = 4; c < 8; ++c)
            p->patterns[1].glitch.pad[static_cast<std::size_t> (c)] = 3;
        p->song = songOf ({ { 0, 2 }, { 1, 2 } }, false);
        return p;
    }

    RenderOptions songOptions (double tail = 1.0)
    {
        RenderOptions o;
        o.scope = RenderScope::Song;
        o.tailSeconds = tail;
        return o;
    }

    void deleteInserts (const std::vector<InsertProcessor*>& v)
    {
        for (auto* p : v)
            delete p;
    }
} // namespace

TEST_CASE ("E5: without song and note map the demo renders exactly as in stage 4", "[render][regression]")
{
    // Reference hash: the stage 4 build (commit 4dd97de) rendered the same project with the same code;
    // the value is compiler specific, so it is checked only for the build it was taken with (clang, Release).
    auto demo = [] (int buffer) {
        auto p = makeDemoProject (48000.0);
        OfflineRig rig (buffer);
        rig.loadProject (*p);
        for (int t = 0; t < kNumTracks; ++t)
            rig.param (t, TrackParam::Shakal, 0.1f * static_cast<float> (t));
        rig.core.params().params.set (masterParam (MasterParam::Shakal), 0.4f);
        rig.play();
        rig.renderSeconds (12.0);
        return fnv (rig.left, rig.right);
    };
    const uint64_t h256 = demo (256);
    REQUIRE (h256 == demo (77));
#if defined(__clang__) && defined(NDEBUG) && ! defined(_MSC_VER)
    REQUIRE (h256 == 0x1fa15b06487976cbull);
#endif

    // sending the stage 5 defaults explicitly changes nothing
    auto p = makeDemoProject (48000.0);
    OfflineRig rig (256);
    rig.loadProject (*p);
    for (int t = 0; t < kNumTracks; ++t)
        rig.param (t, TrackParam::Shakal, 0.1f * static_cast<float> (t));
    rig.core.params().params.set (masterParam (MasterParam::Shakal), 0.4f);
    rig.noteMap (nullptr);
    rig.songMode (false);
    rig.cmd (CommandType::SetSong, 0, 0.f, nullptr);
    rig.play();
    rig.renderSeconds (12.0);
    REQUIRE (fnv (rig.left, rig.right) == h256);
}

TEST_CASE ("RX1: render is deterministic, sample exact in length, independent of the block size", "[render]")
{
    auto p = rx1Project();
    const int64_t end = at (p->bpm, 16.0);

    RenderProgress progress;

    auto a = renderToBuffers (*p, songOptions(), 512, OfflineRig::builtinSamples (*p, 48000.0), {}, &progress);
    REQUIRE (a.result.status == RenderStatus::Done);
    REQUIRE (std::fabs (progress.fraction.load() - 1.0) < 1e-9);

    REQUIRE (a.buffers.songEndSample == end);
    REQUIRE (static_cast<int64_t> (a.buffers.master[0].size()) == end + 48000);
    REQUIRE (a.buffers.totalSamples == end + 48000);
    float peak = 0.f;
    for (const auto& ch : a.buffers.master)
        for (float x : ch)
        {
            REQUIRE (std::isfinite (x));
            peak = std::max (peak, std::fabs (x));
        }
    REQUIRE (peak == a.buffers.peakMaster);
    REQUIRE (peak > 0.f);

    auto b = renderToBuffers (*p, songOptions(), 512, OfflineRig::builtinSamples (*p, 48000.0));
    REQUIRE (b.buffers.master == a.buffers.master);
    for (int block : { 64, 333 })
    {
        auto c = renderToBuffers (*p, songOptions(), block, OfflineRig::builtinSamples (*p, 48000.0));
        INFO ("block " << block);
        REQUIRE (c.buffers.master == a.buffers.master);
    }
}

TEST_CASE ("RX1b: progress grows monotonically to 1", "[render]")
{
    auto p = rx1Project();
    RenderProgress progress;
    std::vector<double> seen;
    std::atomic<bool> done { false };
    RenderStatus status = RenderStatus::Error;
    std::thread t ([&] {
        auto r = renderToBuffers (*p, songOptions(), 64, OfflineRig::builtinSamples (*p, 48000.0), {}, &progress);
        status = r.result.status;
        done = true;
    });
    while (! done.load())
        seen.push_back (progress.fraction.load());
    t.join();
    REQUIRE (status == RenderStatus::Done);
    seen.push_back (progress.fraction.load());
    REQUIRE (std::is_sorted (seen.begin(), seen.end()));
    REQUIRE (std::fabs (seen.back() - 1.0) < 1e-9);
}

TEST_CASE ("RX2: the export equals playing from Play", "[render]")
{
    auto p = rx1Project();
    const int64_t total = at (p->bpm, 16.0) + 48000;

    for (bool withInsert : { false, true })
    {
        INFO ("insert " << withInsert);
        PublishedInserts ins;
        auto* exportInsert = withInsert ? new FakeInsert (0.5f) : nullptr;
        ins.slots[0] = exportInsert;
        auto r = renderToBuffers (*p, songOptions(), 512, OfflineRig::builtinSamples (*p, 48000.0), ins);
        REQUIRE (r.result.status == RenderStatus::Done);
        if (withInsert)
        {
            REQUIRE (exportInsert->nonRealtime.load());
            REQUIRE (r.result.retiredInserts.size() == 1);
        }

        auto* rigInsert = withInsert ? new FakeInsert (0.5f) : nullptr;
        {
            PublishedInserts rigIns;
            rigIns.slots[0] = rigInsert;
            OfflineRig rig (512);
            rig.loadProject (*p, rigIns);
            rig.setSong (p->song);
            rig.songMode (true);
            rig.songPosition (0);
            rig.play();
            rig.renderUntil (total);
            REQUIRE (rig.left == r.buffers.master[0]);
            REQUIRE (rig.right == r.buffers.master[1]);
            if (withInsert)
                REQUIRE (! rigInsert->nonRealtime.load());
        }
        deleteInserts (r.result.retiredInserts);
        delete rigInsert;
    }
}

TEST_CASE ("RX3: stems are the track buses after PDC and the guitar strip", "[render]")
{
    auto neutral = [] {
        auto p = makeDemoProject (48000.0);
        p->masterShakal = 0.f;
        p->masterGainDb = 0.f;
        for (auto& pat : p->patterns)
            pat.glitch = {};
        p->tracks[5].params[static_cast<std::size_t> (TrackParam::Volume)] = 0.f;
        p->song = songOf ({ { 0, 1 }, { 1, 1 } }, false);
        return p;
    };

    SECTION ("sum of stems equals the master; gain scales the master only")
    {
        auto p = neutral();
        p->patterns[1].loopSlot = 8;
        auto samples = OfflineRig::builtinSamples (*p, 48000.0);
        auto* loop = makeRamp (2 * 96000);   // stand-in guitar loop (rate fit by tempo)
        loop->sourceBars = 1.0;
        samples[8] = loop;
        p->slots[8].used = true;
        p->slots[8].source = "loop";

        auto r = renderToBuffers (*p, songOptions(), 512, samples);
        REQUIRE (r.result.status == RenderStatus::Done);
        const auto n = r.buffers.master[0].size();
        bool equal = true;
        for (int c = 0; c < 2 && equal; ++c)
            for (std::size_t i = 0; i < n; ++i)
            {
                float s = 0.f;
                for (int t = 0; t < kNumTracks; ++t)
                    s += r.buffers.stems[static_cast<std::size_t> (t)][static_cast<std::size_t> (c)][i];
                s += r.buffers.stems[kNumTracks][static_cast<std::size_t> (c)][i];
                if (s != r.buffers.master[static_cast<std::size_t> (c)][i])
                {
                    equal = false;
                    break;
                }
            }
        REQUIRE (equal);
        REQUIRE (! r.buffers.stemSilent[kNumTracks]);   // the loop is in the guitar stem
        REQUIRE (r.buffers.stemSilent[5]);             // RIM has no steps in the demo

        auto p6 = neutral();
        p6->masterGainDb = -6.f;
        p6->patterns[1].loopSlot = -1;
        auto r6 = renderToBuffers (*p6, songOptions(), 512, OfflineRig::builtinSamples (*p6, 48000.0));
        const float g = std::pow (10.f, -6.f / 20.f);
        double maxErr = 0.0;
        for (std::size_t i = 0; i < r6.buffers.master[0].size(); ++i)
        {
            float s = 0.f;
            for (int t = 0; t < kNumTracks; ++t)
                s += r6.buffers.stems[static_cast<std::size_t> (t)][0][i];
            maxErr = std::max (maxErr, static_cast<double> (std::fabs (s * g - r6.buffers.master[0][i])));
        }
        REQUIRE (maxErr <= 1e-6);
    }

    SECTION ("stems are aligned by PDC")
    {
        Project p;
        p.bpm = 120.0;
        p.masterGainDb = 0.f;
        p.tracks[0].sampleSlot = 0;
        p.tracks[1].sampleSlot = 0;
        p.patterns[0].lanes[0].steps[0].active = true;
        p.patterns[0].lanes[1].steps[0].active = true;
        p.song = songOf ({ { 0, 1 } }, false);
        std::array<Sample*, kNumSampleSlots> samples {};
        samples[0] = makeImpulse();
        PublishedInserts ins;
        ins.slots[0] = new FakeInsert (1.f, 0.f, 500);
        auto r = renderToBuffers (p, songOptions (0.5), 512, samples, ins);
        REQUIRE (r.result.status == RenderStatus::Done);
        const auto o0 = onsets (r.buffers.stems[0][0]);
        const auto o1 = onsets (r.buffers.stems[1][0]);
        REQUIRE (! o0.empty());
        REQUIRE (o0 == o1);
        REQUIRE (o0.front() == 500);
        deleteInserts (r.result.retiredInserts);
    }

    SECTION ("no stems requested: empty arrays")
    {
        auto p = neutral();
        auto o = songOptions();
        o.stems = false;
        auto r = renderToBuffers (*p, o, 512, OfflineRig::builtinSamples (*p, 48000.0));
        REQUIRE (r.result.status == RenderStatus::Done);
        for (const auto& st : r.buffers.stems)
            REQUIRE (st[0].empty());
    }
}

TEST_CASE ("RX4: pattern scope, cancel, empty song", "[render]")
{
    auto p = rx1Project();

    RenderOptions po;
    po.scope = RenderScope::Pattern;
    po.patternIndex = 1;
    po.cycles = 3;
    po.tailSeconds = 1.0;
    auto a = renderToBuffers (*p, po, 512, OfflineRig::builtinSamples (*p, 48000.0));
    REQUIRE (a.result.status == RenderStatus::Done);
    REQUIRE (static_cast<int64_t> (a.buffers.master[0].size()) == at (p->bpm, 12.0 * p->patterns[1].bars) + 48000);

    auto q = *p;
    q.song = songOf ({ { 1, 3 } }, true);
    auto b = renderToBuffers (q, songOptions(), 512, OfflineRig::builtinSamples (q, 48000.0));
    REQUIRE (b.buffers.master == a.buffers.master);

    // cancel after 30 %
    {
        RenderProgress progress;
        PublishedInserts ins;
        ins.slots[3] = new FakeInsert (0.7f);
        ins.slots[2 * kMasterChain + 1] = new FakeInsert (0.9f);
        EngineCore core;
        core.prepare (48000.0, 256, 0, 2);
        publishProject (core, *p, OfflineRig::builtinSamples (*p, 48000.0), ins);
        RenderBuffers buf;
        std::atomic<bool> stop { false };
        std::thread canceller ([&] {
            while (progress.fraction.load() < 0.3 && ! stop.load()) {}
            progress.cancel = true;
        });
        auto o = songOptions();
        o.blockSize = 256;
        const auto r = renderOffline (core, *p, o, buf, &progress);
        stop = true;
        canceller.join();
        REQUIRE (r.status == RenderStatus::Cancelled);
        const auto total = static_cast<double> (at (p->bpm, 16.0) + 48000);
        REQUIRE (static_cast<double> (buf.master[0].size()) >= 0.3 * total);
        Retired x;
        REQUIRE (! core.retired().pop (x));
        REQUIRE (r.retiredInserts.size() == 2);
        REQUIRE (r.retiredInserts[0] != r.retiredInserts[1]);
        deleteInserts (r.retiredInserts);
    }

    auto e = *p;
    e.song = {};
    auto r = renderToBuffers (e, songOptions(), 512, OfflineRig::builtinSamples (e, 48000.0));
    REQUIRE (r.result.status == RenderStatus::Error);
    REQUIRE (! r.result.error.empty());
}

TEST_CASE ("RX5: files of a render; stem names", "[render]")
{
    TempDir dir;
    auto p = rx1Project();
    auto r = renderToBuffers (*p, songOptions(), 512, OfflineRig::builtinSamples (*p, 48000.0));
    const auto n = static_cast<int64_t> (r.buffers.master[0].size());
    const float* ch[2] = { r.buffers.master[0].data(), r.buffers.master[1].data() };

    REQUIRE (writeWav (dir.path / "master.wav", ch, 2, n, 48000.0, WavDepth::Float32).ok);
    WavData w;
    std::string err;
    REQUIRE (readWav (dir.path / "master.wav", w, err));
    REQUIRE (w.data[0] == r.buffers.master[0]);
    REQUIRE (w.data[1] == r.buffers.master[1]);

    REQUIRE (writeWav (dir.path / "m24.wav", ch, 2, n, 48000.0, WavDepth::Pcm24).ok);
    REQUIRE (readWav (dir.path / "m24.wav", w, err));
    double maxErr = 0.0;
    for (int64_t i = 0; i < n; ++i)
        if (std::fabs (r.buffers.master[0][static_cast<std::size_t> (i)]) <= 1.f)
            maxErr = std::max (maxErr, std::fabs (static_cast<double> (r.buffers.master[0][static_cast<std::size_t> (i)]) - w.data[0][static_cast<std::size_t> (i)]));
    REQUIRE (maxErr <= std::ldexp (1.0, -23));

    auto loud = *p;
    loud.masterGainDb = 6.f;
    loud.masterFx.params[static_cast<std::size_t> (FxParam::DriveOn)] = 1.f;
    loud.masterFx.params[static_cast<std::size_t> (FxParam::DriveDb)] = 36.f;
    loud.masterFx.params[static_cast<std::size_t> (FxParam::DriveMode)] = 0.f;
    auto rl = renderToBuffers (loud, songOptions(), 512, OfflineRig::builtinSamples (loud, 48000.0));
    const float* lch[2] = { rl.buffers.master[0].data(), rl.buffers.master[1].data() };
    const auto res = writeWav (dir.path / "loud.wav", lch, 2, static_cast<int64_t> (rl.buffers.master[0].size()), 48000.0, WavDepth::Pcm16);
    REQUIRE (res.ok);
    REQUIRE (res.clippedSamples > 0);
    REQUIRE (readWav (dir.path / "loud.wav", w, err));
    for (const auto& c : { w.data[0], w.data[1] })
        for (float x : c)
            REQUIRE (std::fabs (x) <= 1.f);

    REQUIRE (stemFileName (0, "Ki/ck!") == "01_Kick.wav");
    REQUIRE (stemFileName (2, "") == "03_T3.wav");
    REQUIRE (stemFileName (8, "GUITAR") == "09_GUITAR.wav");
}

TEST_CASE ("RX6: render is much faster than real time", "[render][cpu]")
{
#if defined(JACKAL_UNDER_SANITIZER) || ! defined(NDEBUG)
    SKIP ("timing is meaningless under sanitizers or in Debug");
#else
    auto p = makeDemoProject (48000.0);
    p->bpm = 172.0;
    for (int c = 0; c < kNumChains; ++c)
    {
        FxSettings& fx = c < kNumTracks ? p->tracks[static_cast<std::size_t> (c)].fx : (c == kMasterChain ? p->masterFx : p->guitar.fx);
        fx.preset = 2;   // MEM
        if (c < kNumTracks)
            p->tracks[static_cast<std::size_t> (c)].params[static_cast<std::size_t> (TrackParam::Shakal)] = 1.f;
    }
    p->masterShakal = 1.f;
    p->guitar.shakal = 1.f;
    p->song = songOf ({ { 0, 16 }, { 1, 16 } }, false);   // 32 bars
    const auto t0 = std::chrono::steady_clock::now();
    auto r = renderToBuffers (*p, songOptions (0.0), 512, OfflineRig::builtinSamples (*p, 48000.0));
    const double wall = std::chrono::duration<double> (std::chrono::steady_clock::now() - t0).count();
    REQUIRE (r.result.status == RenderStatus::Done);
    const double songSeconds = 32.0 * 4.0 * 60.0 / 172.0;
    INFO ("render " << wall << " s for " << songSeconds << " s of song");
    REQUIRE (wall < songSeconds / 8.0);
#endif
}
