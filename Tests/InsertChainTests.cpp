// Stage 4: insert slots, bypass and plugin delay compensation (offline renders, FakeInsert only).

#include "FakeInsert.h"
#include "FxTestHelpers.h"
#include "RigHelpers.h"
#include "TestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

using namespace jackal;
using namespace jackal::test;

namespace
{
    constexpr int64_t kBar = 96000;   // 120 BPM, 48 kHz

    // Impulse on step 0 of every listed track (sample slot = track).
    void setupHits (OfflineRig& rig, std::initializer_list<int> tracks, bool everyQuarter = false)
    {
        Pattern p;
        for (int t : tracks)
        {
            rig.setSample (t, makeImpulse());
            rig.setTrack (t, t);
            auto& lane = p.lanes[static_cast<std::size_t> (t)];
            lane.steps[0].active = true;
            if (everyQuarter)
                lane.steps[4].active = lane.steps[8].active = lane.steps[12].active = true;
        }
        rig.setPattern (0, p);
        rig.bpm (120.0);
        rig.masterGainDb (0.f);   // unity, so the expected values below need no gain
    }

    void setInsert (OfflineRig& rig, int chain, int slot, InsertProcessor* p)
    {
        rig.cmd (CommandType::SetInsert, chain | (slot << 8), 0.f, p);
    }

    Signal shifted (const Signal& base, int64_t by)
    {
        Signal s (base.size(), 0.f);
        for (std::size_t i = 0; i + static_cast<std::size_t> (by) < base.size(); ++i)
            s[i + static_cast<std::size_t> (by)] = base[i];
        return s;
    }

    Signal baseline (std::initializer_list<int> tracks, int64_t length, int buffer = 128, bool metronome = false, bool quarters = false)
    {
        OfflineRig rig (buffer);
        rig.core.params().metronomeEnabled = metronome;
        setupHits (rig, tracks, quarters);
        rig.play();
        rig.renderUntil (length);
        return rig.left;
    }
}

TEST_CASE ("I1: SetInsert swaps, retires and drops", "[insert]")
{
    const Signal base = baseline ({ 0 }, kBar);

    FakeInsert a (0.5f), b (1.f);
    OfflineRig rig;
    setupHits (rig, { 0 });
    setInsert (rig, 0, 0, &a);
    rig.play();
    rig.renderUntil (kBar);
    Signal half (base.size());
    for (std::size_t i = 0; i < base.size(); ++i)
        half[i] = base[i] * 0.5f;
    expectBitEqual (rig.left, half);
    REQUIRE (rig.countRetired (&a) == 0);

    setInsert (rig, 0, 0, &b);
    rig.renderUntil (kBar + 128);
    REQUIRE (rig.countRetired (&a) == 1);
    setInsert (rig, 0, 0, nullptr);
    rig.renderUntil (kBar + 256);
    REQUIRE (rig.countRetired (&b) == 1);
    REQUIRE (rig.countRetired (&a) == 1);

    // full retire queue: the command is dropped, the old insert keeps working
    FakeInsert c (0.25f), d (1.f);
    setInsert (rig, 0, 0, &c);
    rig.renderUntil (kBar + 384);
    static int dummy = 0;
    while (rig.core.retired().push ({ Retired::Kind::Insert, &dummy })) {}
    const auto droppedBefore = rig.core.sequencerSnapshot().droppedCommands;
    setInsert (rig, 0, 0, &d);
    std::vector<float> l (128), r (128);
    float* out[2] = { l.data(), r.data() };
    rig.core.process (nullptr, 0, out, 2, 128);
    REQUIRE (rig.core.sequencerSnapshot().droppedCommands == droppedBefore + 1);
    REQUIRE (rig.core.insertChain (0).slot (0).get() == &c);
    rig.drain();
    REQUIRE (rig.countRetired (&c) == 0);

    // releaseAll hands every installed insert back
    FakeInsert m (1.f), g (1.f);
    setInsert (rig, kMasterChain, 1, &m);
    setInsert (rig, kGuitarChain, 0, &g);
    rig.renderSeconds (0.01);
    rig.core.releaseAll();
    rig.drain();
    REQUIRE (rig.countRetired (&c) == 1);
    REQUIRE (rig.countRetired (&m) == 1);
    REQUIRE (rig.countRetired (&g) == 1);
    REQUIRE (rig.countRetired (&d) == 0);   // never reached the engine
}

TEST_CASE ("I2: slot order, master and guitar slots", "[insert]")
{
    const Signal base = baseline ({ 0 }, kBar / 4);

    for (int order = 0; order < 2; ++order)
    {
        FakeInsert off (1.f, 0.5f), twice (2.f, 0.f);
        OfflineRig rig;
        setupHits (rig, { 0 });
        setInsert (rig, 0, 0, order == 0 ? &off : &twice);
        setInsert (rig, 0, 1, order == 0 ? &twice : &off);
        rig.play();
        rig.renderUntil (kBar / 4);
        Signal want (base.size());
        for (std::size_t i = 0; i < base.size(); ++i)
            want[i] = order == 0 ? (base[i] + 0.5f) * 2.f : (base[i] * 2.f) + 0.5f;
        expectBitEqual (rig.left, want);
    }

    {
        FakeInsert one (1.f, 1.f);
        OfflineRig rig;
        setupHits (rig, { 0 });
        rig.masterGainDb (-6.f);
        setInsert (rig, kMasterChain, 0, &one);
        rig.play();
        rig.renderUntil (kBar / 4);
        const float g = std::pow (10.f, -6.f / 20.f);
        Signal want (base.size());
        for (std::size_t i = 0; i < base.size(); ++i)
            want[i] = (base[i] + 1.f) * g;
        expectBitEqual (rig.left, want);
    }

    {
        FakeInsert three (3.f);
        OfflineRig rig;
        rig.core.params().monitorEnabled = true;
        rig.input = [] (int64_t, int n, float* in) { std::fill (in, in + n, 0.1f); };
        setInsert (rig, kGuitarChain, 0, &three);
        rig.renderSeconds (0.05);
        for (float v : rig.left)
            REQUIRE (v == 0.1f * 3.f);
    }
}

TEST_CASE ("I3: prepare and reset reach the inserts", "[insert]")
{
    FakeInsert a;
    a.prepare (48000.0, 256);   // the app prepares before it publishes
    OfflineRig rig (256);
    setInsert (rig, 0, 0, &a);
    rig.renderUntil (256);
    REQUIRE (a.prepareCalls == 1);

    rig.core.prepare (96000.0, 128, 0, 2);
    REQUIRE (a.prepareCalls == 2);
    REQUIRE (a.lastSr == 96000.0);
    REQUIRE (a.lastBlock == 128);

    const int resets = a.resetCalls;
    rig.bufferSize = 128;
    rig.play();
    rig.renderUntil (rig.position() + 128);
    rig.cmd (CommandType::Stop);
    rig.renderUntil (rig.position() + 128);
    REQUIRE (a.resetCalls == resets + 2);
}

TEST_CASE ("I4: bypass keeps the reported delay", "[insert]")
{
    const Signal base = baseline ({ 0 }, kBar / 2);
    Signal results[2];
    const int buffers[2] = { 64, 1024 };
    for (int k = 0; k < 2; ++k)
    {
        FakeInsert z (0.f, 0.f, 300);
        OfflineRig rig (buffers[k]);
        setupHits (rig, { 0 });
        rig.core.params().params.set (insertParam (0, 0, InsertParam::Bypass), 1.f);
        setInsert (rig, 0, 0, &z);
        rig.play();
        rig.renderUntil (kBar / 2);
        expectBitEqual (rig.left, shifted (base, 300));
        results[k] = rig.left;

        rig.core.params().params.set (insertParam (0, 0, InsertParam::Bypass), 0.f);
        rig.renderUntil (kBar + kBar / 2);
        for (std::size_t i = static_cast<std::size_t> (kBar - 1000); i < rig.left.size(); ++i)
            REQUIRE (rig.left[i] == 0.f);
        REQUIRE (rig.core.loopSnapshot().pdcTracks == 300);
    }
    expectBitEqual (results[0], results[1]);
}

TEST_CASE ("PD1: track and master delays line up with the click", "[insert][pdc]")
{
    const Signal base = baseline ({ 0, 1 }, 2 * kBar, 128, true);
    Signal results[3];
    const int buffers[3] = { 64, 128, 7 };
    for (int k = 0; k < 3; ++k)
    {
        FakeInsert d500 (1.f, 0.f, 500), d200 (1.f, 0.f, 200);
        OfflineRig rig (buffers[k]);
        rig.core.params().metronomeEnabled = true;
        setupHits (rig, { 0, 1 });
        setInsert (rig, 0, 0, &d500);
        rig.play();
        rig.renderUntil (kBar);
        {
            const Signal got (rig.left.begin(), rig.left.end());
            expectBitEqual (got, shifted (Signal (base.begin(), base.begin() + kBar), 500));
        }
        REQUIRE (rig.core.loopSnapshot().pdcTracks == 500);

        // master slot +200: clicks and drums now at +700 (from the next bar on, the switch itself is a cut)
        setInsert (rig, kMasterChain, 0, &d200);
        rig.renderUntil (2 * kBar);
        const auto snap = rig.core.loopSnapshot();
        REQUIRE (snap.pdcMaster == 200);
        REQUIRE (snap.pdcTracks + snap.pdcMaster == 700);
        const auto o = onsets (rig.left);
        const auto ob = onsets (base);
        // every onset of the second bar of the baseline appears 700 later
        for (int64_t x : ob)
            if (x >= kBar && x + 700 < 2 * kBar)
                REQUIRE (std::find (o.begin(), o.end(), x + 700) != o.end());
        for (int64_t i = kBar + 700; i < 2 * kBar; ++i)
            REQUIRE (rig.left[static_cast<std::size_t> (i)] == base[static_cast<std::size_t> (i - 700)]);

        // take the track insert out: from then on +200
        setInsert (rig, 0, 0, nullptr);
        rig.renderUntil (3 * kBar);
        const auto o2 = onsets (Signal (rig.left.begin() + 2 * kBar, rig.left.end()));
        REQUIRE_FALSE (o2.empty());
        REQUIRE (o2.front() == 200 + ob.front());
        results[k] = rig.left;
    }
    expectBitEqual (results[0], results[1]);
    expectBitEqual (results[0], results[2]);
}

TEST_CASE ("PD2: the PDC ceiling", "[insert][pdc]")
{
    // track 1 sounds, track 0 has the oversized insert and no hits
    OfflineRig ref;
    ref.setSample (1, makeImpulse());
    ref.setTrack (1, 1);
    Pattern p;
    p.lanes[1].steps[0].active = true;
    ref.setPattern (0, p);
    ref.bpm (120.0);
    ref.play();
    ref.renderUntil (kBar / 2);

    FakeInsert huge (1.f, 0.f, 20000);
    OfflineRig rig;
    rig.setSample (1, makeImpulse());
    rig.setTrack (1, 1);
    rig.setPattern (0, p);
    rig.bpm (120.0);
    setInsert (rig, 0, 0, &huge);
    rig.play();
    rig.renderUntil (kBar / 2);
    const auto snap = rig.core.loopSnapshot();
    REQUIRE (snap.pdcTracks == kMaxPdcSamples);
    REQUIRE (snap.pdcOverflow);
    expectNoNanInf (rig.left);
    expectBitEqual (rig.left, shifted (ref.left, kMaxPdcSamples));
}

TEST_CASE ("PD3: no inserts, no loops: stage 3 output", "[insert][pdc]")
{
    // Stage 1-3 tests run unchanged (except J4). Here: empty slots, bypass flags and guitar defaults
    // change nothing, and the PDC reports zero.
    auto render = [] (bool touch, bool noFx) {
        OfflineRig rig (128);
        rig.core.setFxDisabledForTests (noFx);
        auto demo = makeDemoProject (48000.0);
        rig.loadProject (*demo);
        if (touch)
            for (int c = 0; c < kNumChains; ++c)
                for (int s = 0; s < kSlotsPerChain; ++s)
                {
                    rig.cmd (CommandType::SetInsert, c | (s << 8), 0.f, nullptr);
                    rig.core.params().params.set (insertParam (c, s, InsertParam::Bypass), 1.f);
                }
        rig.play();
        rig.renderSeconds (4.0);
        REQUIRE (rig.core.loopSnapshot().pdcTracks == 0);
        REQUIRE (rig.core.loopSnapshot().pdcMaster == 0);
        return rig.left;
    };
    for (bool noFx : { false, true })
        expectBitEqual (render (false, noFx), render (true, noFx));
}
