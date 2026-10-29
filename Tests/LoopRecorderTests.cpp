// Stage 4: loop recording and playback inside the guitar strip (offline renders, inputs are numbers).

#include "LoopTestHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace jackal;
using namespace jackal::test;

namespace
{
    struct L1Setup
    {
        int buffer = 128;
        int trackDelay = 0, masterDelay = 0, guitarDelay = 0;
        bool tapAmp = true;
    };

    struct L1Result
    {
        Signal d0, d1;
        int32_t compTotal = 0;
    };

    // Contract L1: arm at sample 1000, the guitarist plays exactly on the heard beats, round trip 777.
    L1Result runL1 (const L1Setup& s, OfflineRig* keep = nullptr, Sample** keepSample = nullptr)
    {
        auto rigPtr = std::make_unique<OfflineRig> (s.buffer);
        OfflineRig& rig = *rigPtr;
        FakeInsert td (1.f, 0.f, s.trackDelay), md (1.f, 0.f, s.masterDelay), gd (1.f, 0.f, s.guitarDelay);
        rig.core.params().monitorEnabled = true;
        rig.core.params().loopCompSamples = 777;
        rig.core.params().params.set (guitarParam (GuitarParam::LoopTap), s.tapAmp ? 1.f : 0.f);
        loopPattern (rig, 0, 8);
        rig.bpm (120.0);
        if (s.trackDelay > 0) setInsertCmd (rig, 0, 0, &td);
        if (s.masterDelay > 0) setInsertCmd (rig, kMasterChain, 0, &md);
        if (s.guitarDelay > 0) setInsertCmd (rig, kGuitarChain, 0, &gd);
        heardBeats (rig, 777);
        rig.play();
        rig.renderUntil (1000);

        Sample* sample = makeLoopTarget (2 * kLoopBar);
        armLoop (rig, 8, 1, false, sample);
        rig.renderUntil (1000 + 1);
        auto snap = rig.core.loopSnapshot();
        REQUIRE (snap.state == LoopState::Armed);
        REQUIRE (snap.startSample == kLoopBar);
        REQUIRE (snap.lengthSamples == kLoopBar);

        rig.renderSeconds (4.2);   // the window ends at 96000 + 96000 + comp
        const int ev = findEvent (rig, EngineEventType::LoopRecorded, 8);
        REQUIRE (ev >= 0);
        const int64_t evBlock = rig.eventBlocks[static_cast<std::size_t> (ev)];
        const int64_t windowEnd = 2 * kLoopBar + rig.core.loopSnapshot().compTotal;   // 96000 + 96000 + comp
        REQUIRE (evBlock <= windowEnd);
        REQUIRE (windowEnd < evBlock + s.buffer);
        REQUIRE (rig.core.bank().get (8) == sample);
        REQUIRE (sample->length == kLoopBar);
        REQUIRE (sample->sourceBars == 1.0);

        L1Result r;
        r.d0.assign (sample->data[0].begin(), sample->data[0].begin() + kLoopBar);
        r.d1.assign (sample->data[1].begin(), sample->data[1].begin() + kLoopBar);
        r.compTotal = rig.core.loopSnapshot().compTotal;
        rig.core.releaseAll();   // the fake inserts die with this scope
        rig.drain();
        (void) keep; (void) keepSample;
        return r;
    }

    void expectBeatImpulses (const Signal& d, std::vector<int64_t> at)
    {
        for (std::size_t i = 0; i < d.size(); ++i)
        {
            const bool want = std::find (at.begin(), at.end(), static_cast<int64_t> (i)) != at.end();
            INFO ("index " << i);
            REQUIRE (d[i] == (want ? 1.f : 0.f));
        }
    }

    const std::vector<int64_t> kBeats { 0, 24000, 48000, 72000 };
}

TEST_CASE ("L1: the recorded loop lands on the beat", "[loop]")
{
    L1Result first;
    for (int buffer : { 64, 256, 7 })
    {
        L1Setup s;
        s.buffer = buffer;
        const auto r = runL1 (s);
        expectBeatImpulses (r.d0, kBeats);
        expectBitEqual (r.d0, r.d1);
        REQUIRE (r.compTotal == 777);
        if (first.d0.empty())
            first = r;
        else
            expectBitEqual (first.d0, r.d0);
    }

    SECTION ("guitar slot delay, AMP tap")
    {
        L1Setup s; s.guitarDelay = 300; s.tapAmp = true;
        const auto r = runL1 (s);
        expectBeatImpulses (r.d0, kBeats);
        REQUIRE (r.compTotal == 777 + 300);
    }
    SECTION ("guitar slot delay, DI tap")
    {
        L1Setup s; s.guitarDelay = 300; s.tapAmp = false;
        const auto r = runL1 (s);
        expectBeatImpulses (r.d0, kBeats);
        REQUIRE (r.compTotal == 777);
    }
    SECTION ("track and master delays")
    {
        L1Setup s; s.trackDelay = 500; s.masterDelay = 200;
        const auto r = runL1 (s);
        expectBeatImpulses (r.d0, kBeats);
        REQUIRE (r.compTotal == 777 + 700);
    }
}

TEST_CASE ("L2: start quantize and count-in", "[loop]")
{
    auto armAt = [] (int64_t at, int patternBars, int bars) {
        OfflineRig rig (128);
        loopPattern (rig, 0, 8, patternBars);
        rig.bpm (120.0);
        rig.play();
        rig.renderUntil (at);
        Sample* s = makeLoopTarget (4 * kLoopBar);
        armLoop (rig, 8, bars, false, s);
        rig.renderUntil (at + 1);
        const auto snap = rig.core.loopSnapshot();
        REQUIRE (snap.state == LoopState::Armed);
        return std::make_pair (snap.startSample, snap.lengthSamples);
    };
    REQUIRE (armAt (1000, 1, 1).first == kLoopBar);
    REQUIRE (armAt (95999, 1, 1).first == kLoopBar);
    REQUIRE (armAt (96000, 1, 1).first == 2 * kLoopBar);
    REQUIRE (armAt (1000, 2, 1).first % (2 * kLoopBar) == 0);
    REQUIRE (armAt (1000, 1, 2).second == 2 * kLoopBar);

    // armed while stopped: the transport starts, the first cycle is the count-in
    OfflineRig rig (128);
    loopPattern (rig, 0, 8);
    rig.bpm (120.0);
    rig.core.params().metronomeEnabled = false;
    Sample* s = makeLoopTarget (kLoopBar);
    armLoop (rig, 8, 1, false, s);
    rig.renderUntil (1);
    REQUIRE (rig.core.transportSnapshot().playing);
    REQUIRE (rig.core.loopSnapshot().startSample == kLoopBar);
    rig.renderUntil (2 * kLoopBar - 1000);
    bool clicks = false;
    for (int64_t i = 0; i < kLoopBar; ++i)
        clicks = clicks || rig.left[static_cast<std::size_t> (i)] != 0.f;
    REQUIRE (clicks);
    for (int64_t i = kLoopBar + 4000; i < 2 * kLoopBar - 1000; ++i)
        REQUIRE (rig.left[static_cast<std::size_t> (i)] == 0.f);
}

TEST_CASE ("L3: overdub and the engine-level undo", "[loop]")
{
    OfflineRig rig (128);
    rig.core.params().monitorEnabled = true;
    rig.core.params().loopCompSamples = 777;
    loopPattern (rig, 0, 8);
    rig.bpm (120.0);
    heardBeats (rig, 777);
    rig.play();
    rig.renderUntil (1000);
    Sample* a = makeLoopTarget (kLoopBar);
    armLoop (rig, 8, 1, false, a);
    rig.renderSeconds (4.2);
    REQUIRE (rig.core.bank().get (8) == a);

    heardBeats (rig, 777, { kLoopBeat / 2 });
    Sample* b = makeLoopTarget (kLoopBar);
    armLoop (rig, 8, 1, true, b);
    rig.renderUntil (rig.position() + 1);
    REQUIRE (rig.core.loopSnapshot().overdub);
    rig.renderSeconds (4.2);
    REQUIRE (rig.core.bank().get (8) == b);
    REQUIRE (rig.countRetired (a) == 1);
    for (int64_t i = 0; i < kLoopBar; ++i)
    {
        const bool beat = i % kLoopBeat == 0 || i % kLoopBeat == kLoopBeat / 2;
        INFO ("index " << i);
        REQUIRE (b->data[0][static_cast<std::size_t> (i)] == (beat ? 1.f : 0.f));
        REQUIRE (b->data[1][static_cast<std::size_t> (i)] == (beat ? 1.f : 0.f));
    }

    // undo at engine level: republish the first layer
    rig.input = nullptr;
    rig.setSample (8, a);
    rig.renderUntil (rig.position() + 128);
    REQUIRE (rig.countRetired (b) == 1);
    REQUIRE (rig.core.bank().get (8) == a);
    rig.renderUntil (rig.position() + kLoopBar);
    REQUIRE (rig.core.loopSnapshot().playIndex >= 0);
}

TEST_CASE ("L4: loop playback follows the pattern", "[loop]")
{
    auto ramp = [] { auto* s = makeRamp (kLoopBar); s->sourceBars = 1.0; return s; };

    auto run = [&] (int trackDelay, int masterDelay, int guitarDelay, bool tapAmp, float gainDb, int64_t length) {
        auto rig = std::make_unique<OfflineRig> (128);
        auto fakes = std::make_shared<std::array<FakeInsert, 3>>();
        (*fakes)[0].delay = trackDelay; (*fakes)[1].delay = masterDelay; (*fakes)[2].delay = guitarDelay;
        loopPattern (*rig, 0, 8);
        rig->bpm (120.0);
        rig->setSample (8, ramp());
        rig->core.params().params.set (guitarParam (GuitarParam::LoopGainDb), gainDb);
        rig->core.params().params.set (guitarParam (GuitarParam::LoopTap), tapAmp ? 1.f : 0.f);
        if (trackDelay) setInsertCmd (*rig, 0, 0, &(*fakes)[0]);
        if (masterDelay) setInsertCmd (*rig, kMasterChain, 0, &(*fakes)[1]);
        if (guitarDelay) setInsertCmd (*rig, kGuitarChain, 0, &(*fakes)[2]);
        rig->play();
        rig->renderUntil (length);
        Signal out = rig->left;
        rig->core.releaseAll();
        rig->drain();
        return out;
    };

    std::unique_ptr<Sample> ref (makeRamp (kLoopBar));
    const float* r = ref->data[0].data();

    {
        const Signal out = run (0, 0, 0, true, 0.f, 4 * kLoopBar);
        for (int64_t m = 0; m < 4; ++m)
            for (int64_t j = 0; j < kLoopBar; ++j)
                REQUIRE (out[static_cast<std::size_t> (m * kLoopBar + j)] == r[j]);
    }
    {
        const Signal out = run (500, 200, 0, true, 0.f, 3 * kLoopBar);
        for (int64_t j = 0; j < 700; ++j)
            REQUIRE (out[static_cast<std::size_t> (j)] == 0.f);
        for (int64_t m = 0; m < 2; ++m)
            for (int64_t j = 0; j < kLoopBar; ++j)
                REQUIRE (out[static_cast<std::size_t> (m * kLoopBar + 700 + j)] == r[j]);
    }
    {
        // DI tap: the loop enters 400 later and gets the guitar slot's 300 itself, so it is heard at +700 too
        const Signal out = run (500, 200, 300, false, 0.f, 3 * kLoopBar);
        for (int64_t m = 0; m < 2; ++m)
            for (int64_t j = 0; j < kLoopBar; ++j)
                REQUIRE (out[static_cast<std::size_t> (m * kLoopBar + 700 + j)] == r[j]);
    }
    {
        const Signal out = run (0, 0, 0, true, -60.f, kLoopBar);
        for (float v : out)
            REQUIRE (v == 0.f);
    }

    // pattern switch, stop and play
    {
        OfflineRig rig (128);
        loopPattern (rig, 0, 8);
        loopPattern (rig, 1, -1);
        rig.bpm (120.0);
        rig.setSample (8, ramp());
        rig.play();
        rig.renderUntil (kLoopBar + 1000);
        rig.cmd (CommandType::QueuePattern, 1);
        rig.renderUntil (2 * kLoopBar + 1000);
        for (int64_t j = 0; j < 1000; ++j)
            REQUIRE (rig.left[static_cast<std::size_t> (2 * kLoopBar + j)] == 0.f);
        rig.cmd (CommandType::QueuePattern, 0);
        rig.renderUntil (4 * kLoopBar);
        for (int64_t j = 0; j < kLoopBar; ++j)
            REQUIRE (rig.left[static_cast<std::size_t> (3 * kLoopBar + j)] == r[j]);

        rig.cmd (CommandType::Stop);
        rig.renderUntil (4 * kLoopBar + 10000);
        for (int64_t j = 4 * kLoopBar; j < 4 * kLoopBar + 10000; ++j)
            REQUIRE (rig.left[static_cast<std::size_t> (j)] == 0.f);
        rig.play();
        const int64_t p0 = rig.position();
        rig.renderUntil (p0 + 10000);
        for (int64_t j = 0; j < 10000; ++j)
            REQUIRE (rig.left[static_cast<std::size_t> (p0 + j)] == r[j]);
    }

    // BPM 60: the same loop plays at half rate, restarts on the (now longer) bar
    {
        OfflineRig rig (128);
        loopPattern (rig, 0, 8);
        rig.bpm (60.0);
        rig.setSample (8, ramp());
        rig.play();
        rig.renderUntil (2 * kLoopBar + 1000);
        for (int64_t i = 0; i < kLoopBar - 1; ++i)
            REQUIRE (rig.left[static_cast<std::size_t> (2 * i)] == r[i]);
        REQUIRE (rig.left[static_cast<std::size_t> (2 * kLoopBar)] == r[0]);
    }
}

TEST_CASE ("L5: seamless first cycle, capacity, frozen BPM, double arm", "[loop]")
{
    OfflineRig rig (64);
    rig.core.params().monitorEnabled = true;
    rig.core.params().loopCompSamples = 777;
    loopPattern (rig, 0, 8);
    rig.bpm (120.0);
    heardBeats (rig, 777, { 0 }, 2 * kLoopBar + 777);   // the guitarist stops when the window closes
    rig.play();
    rig.renderUntil (1000);
    Sample* s = makeLoopTarget (kLoopBar);
    armLoop (rig, 8, 1, false, s);

    // BPM is frozen while armed, a second arm is refused and handed back at once
    rig.renderUntil (2000);
    rig.bpm (90.0);
    Sample* extra = makeLoopTarget (16);
    armLoop (rig, 9, 1, false, extra);
    rig.renderUntil (2128);
    REQUIRE (rig.core.transportSnapshot().bpm == 120.0);
    REQUIRE (rig.countRetired (extra) == 1);
    REQUIRE (rig.core.loopSnapshot().state == LoopState::Armed);
    REQUIRE (rig.core.loopSnapshot().slot == 8);

    rig.renderUntil (3 * kLoopBar);
    REQUIRE (rig.core.bank().get (8) == s);
    for (int64_t q = 2 * kLoopBar + 777; q < 3 * kLoopBar; ++q)
        REQUIRE (rig.left[static_cast<std::size_t> (q)] == s->data[0][static_cast<std::size_t> (q - 2 * kLoopBar)]);

    rig.bpm (100.0);
    rig.renderUntil (rig.position() + 64);
    REQUIRE (rig.core.transportSnapshot().bpm == 100.0);

    // capacity smaller than the loop: the window is cut, never overrun
    OfflineRig small (128);
    small.core.params().monitorEnabled = true;
    loopPattern (small, 0, 8);
    small.bpm (120.0);
    heardBeats (small, 0);
    small.play();
    Sample* c = makeLoopTarget (50000);
    armLoop (small, 8, 1, false, c);
    small.renderSeconds (3.5);
    REQUIRE (small.core.bank().get (8) == c);
    REQUIRE (c->length == 50000);
}

TEST_CASE ("L6: cancel, stop and a full retire queue", "[loop]")
{
    auto start = [] (OfflineRig& rig, Sample* first) {
        rig.core.params().monitorEnabled = true;
        loopPattern (rig, 0, 8);
        rig.bpm (120.0);
        rig.setSample (8, first);
        rig.play();
        rig.renderUntil (1000);
    };

    {
        OfflineRig rig;
        Sample* old = makeLoopTarget (16);
        start (rig, old);
        Sample* t = makeLoopTarget (kLoopBar);
        armLoop (rig, 8, 1, false, t);
        rig.renderUntil (2000);
        rig.cmd (CommandType::CancelLoop);
        rig.renderUntil (3000);
        REQUIRE (rig.core.loopSnapshot().state == LoopState::Idle);
        REQUIRE (rig.countRetired (t) == 1);
        REQUIRE (rig.core.bank().get (8) == old);
    }
    for (bool viaStop : { false, true })
    {
        OfflineRig rig;
        Sample* old = makeLoopTarget (16);
        start (rig, old);
        Sample* t = makeLoopTarget (kLoopBar);
        armLoop (rig, 8, 1, false, t);
        rig.renderUntil (kLoopBar + 5000);
        REQUIRE (rig.core.loopSnapshot().state == LoopState::Recording);
        rig.cmd (viaStop ? CommandType::Stop : CommandType::CancelLoop);
        rig.renderUntil (kLoopBar + 6000);
        REQUIRE (findEvent (rig, EngineEventType::LoopAborted, 8) >= 0);
        REQUIRE (rig.countRetired (t) == 1);
        REQUIRE (rig.core.bank().get (8) == old);
    }
    {
        OfflineRig rig (128);
        Sample* old = makeLoopTarget (16);
        start (rig, old);
        Sample* t = makeLoopTarget (kLoopBar);
        armLoop (rig, 8, 1, false, t);
        rig.renderUntil (2 * kLoopBar - 128);   // the window closes in the next block
        static int dummy = 0;
        while (rig.core.retired().push ({ Retired::Kind::Insert, &dummy })) {}
        rig.renderUntil (2 * kLoopBar);
        REQUIRE (rig.core.bank().get (8) == t);
        REQUIRE (rig.countRetired (old) == 0);
        rig.renderUntil (2 * kLoopBar + 128);
        REQUIRE (rig.countRetired (old) == 1);
    }
}

TEST_CASE ("L7: recording does not depend on the buffer size", "[loop]")
{
    Signal first;
    for (int buffer : { 32, 64, 128, 256, 512, 1024, 7, 333 })
    {
        OfflineRig rig (buffer);
        rig.core.params().monitorEnabled = true;
        rig.core.params().loopCompSamples = 777;
        loopPattern (rig, 0, 8);
        rig.bpm (120.0);
        rig.input = [] (int64_t pos, int n, float* in) {
            for (int i = 0; i < n; ++i)
            {
                uint32_t x = static_cast<uint32_t> (pos + i) * 2654435761u + 7u;
                x ^= x << 13; x ^= x >> 17; x ^= x << 5;
                in[i] = 0.5f * (static_cast<float> (x) / 4294967296.f * 2.f - 1.f);
            }
        };
        rig.play();
        rig.renderUntil (1000);
        Sample* s = makeLoopTarget (kLoopBar);
        armLoop (rig, 8, 1, false, s);
        rig.renderSeconds (4.2);
        REQUIRE (rig.core.bank().get (8) == s);
        Signal d (s->data[0].begin(), s->data[0].end());
        expectNoNanInf (d);
        if (first.empty())
            first = d;
        else
            expectBitEqual (first, d);
    }
}
