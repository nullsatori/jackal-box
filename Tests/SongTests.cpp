// Stage 5: song mode in the engine (SG1-SG5). Offline, nothing is played.

#include "LoopTestHelpers.h"
#include "RigHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>

using namespace jackal;
using namespace jackal::test;

namespace
{
    constexpr int64_t kBeat = 24000;   // 120 BPM, 48 kHz
    int64_t beatAt (double b) { return static_cast<int64_t> (std::llround (b * static_cast<double> (kBeat))); }

    // A: kick (track 0, left) on slot 0; B: snare (track 1, right) on slot 0. Both one bar.
    void kickSnare (OfflineRig& rig)
    {
        rig.setSample (0, makeImpulse());
        rig.setSample (1, makeImpulse());
        rig.setTrack (0, 0);
        rig.setTrack (1, 1);
        rig.param (0, TrackParam::Pan, -1.f);
        rig.param (1, TrackParam::Pan, 1.f);
        Pattern a, b;
        a.lanes[0].steps[0].active = true;
        b.lanes[1].steps[0].active = true;
        rig.setPattern (0, a);
        rig.setPattern (1, b);
        rig.bpm (120.0);
    }

    std::vector<int64_t> beats (std::initializer_list<double> bs)
    {
        std::vector<int64_t> v;
        for (double b : bs)
            v.push_back (beatAt (b));
        return v;
    }
} // namespace

TEST_CASE ("SG1: song A x2, B plays cycle by cycle, sample exact for any buffer", "[song]")
{
    std::vector<float> refL, refR;
    for (int buffer : { 64, 1024, 7 })
    {
        INFO ("buffer " << buffer);
        OfflineRig rig (buffer);
        kickSnare (rig);
        rig.setSong (songOf ({ { 0, 2 }, { 1, 1 } }, true));
        rig.songMode (true);
        rig.play();

        if (buffer == 64)
        {
            rig.renderUntil (beatAt (2));
            auto s = rig.snapshot();
            REQUIRE (s.songMode);
            REQUIRE (s.songIndex == 0);
            REQUIRE (s.songRepeat == 0);
            rig.renderUntil (beatAt (6));
            s = rig.snapshot();
            REQUIRE (s.songIndex == 0);
            REQUIRE (s.songRepeat == 1);
            rig.renderUntil (beatAt (10));
            s = rig.snapshot();
            REQUIRE (s.songIndex == 1);
            REQUIRE (s.songRepeat == 0);
            REQUIRE (s.currentPattern == 1);
        }
        rig.renderUntil (480000 + 2 * kBeat);   // 11 s = 22 beats

        REQUIRE (onsets (rig.left) == beats ({ 0, 4, 12, 16 }));
        REQUIRE (onsets (rig.right) == beats ({ 8, 20 }));
        if (! refL.empty())
        {
            REQUIRE (rig.left == refL);
            REQUIRE (rig.right == refR);
        }
        refL = rig.left;
        refR = rig.right;
    }
}

TEST_CASE ("SG2: entry boundaries reset lanes, repeats do not", "[song]")
{
    SECTION ("conditions")
    {
        OfflineRig rig (128);
        rig.setSample (0, makeImpulse());
        rig.setTrack (0, 0);
        Pattern a;
        a.lanes[0].steps[0].active = true;
        a.lanes[0].steps[0].condition = StepCondition::FirstCycle;
        a.lanes[0].steps[4].active = true;
        a.lanes[0].steps[4].condition = StepCondition::Cycle2of2;
        rig.setPattern (0, a);
        rig.bpm (120.0);
        rig.setSong (songOf ({ { 0, 2 }, { 0, 1 } }));
        rig.songMode (true);
        rig.play();
        rig.renderUntil (beatAt (11.9));
        REQUIRE (onsets (rig.left) == beats ({ 0, 5, 8 }));
    }

    SECTION ("rolls: through a repeat they finish, through an entry they are cut")
    {
        OfflineRig rig (128);
        rig.setSample (0, makeImpulse());
        rig.setTrack (0, 0);
        Pattern a;
        auto& st = a.lanes[0].steps[15];
        st.active = true;
        st.nudge = 50 / 2;      // +25 % of a step: 3.8125
        st.ratchet = 4;         // hits every 1/16 of a beat: the last two land after the bar line
        rig.setPattern (0, a);
        rig.bpm (120.0);
        rig.setSong (songOf ({ { 0, 2 }, { 0, 1 } }));
        rig.songMode (true);
        rig.play();
        rig.renderUntil (beatAt (11.9));

        const auto on = onsets (rig.left, 1);
        auto count = [&on] (double from, double to) {
            return std::count_if (on.begin(), on.end(), [&] (int64_t x) { return x >= beatAt (from) && x < beatAt (to); });
        };
        REQUIRE (count (3.5, 4.5) == 4);   // repeat boundary at 4: roll finishes
        REQUIRE (count (7.5, 8.5) == 3);   // entry boundary at 8: cut (3.8125 + 0, 1/16, 2/16 < 4)
    }

    SECTION ("guitar loop restarts on an entry and runs on through a repeat")
    {
        OfflineRig rig (128);
        auto* loop = makeRamp (2 * kLoopBar);
        loop->sourceBars = 2.0;
        rig.setSample (8, loop);
        Pattern a;
        a.loopSlot = 8;
        rig.setPattern (0, a);
        rig.bpm (120.0);
        rig.setSong (songOf ({ { 0, 3 }, { 0, 1 } }));
        rig.songMode (true);
        rig.play();
        rig.renderUntil (beatAt (14));

        auto idx = [&rig] (double b) {
            return std::llround (static_cast<double> (rig.left[static_cast<std::size_t> (beatAt (b))]) * 2.0 * kLoopBar) - 1;
        };
        REQUIRE (idx (0) == 0);
        REQUIRE (idx (4) == kLoopBar);    // repeat: continues into the second bar of the loop
        REQUIRE (idx (8) == 0);           // the loop's own cycle wraps
        REQUIRE (idx (12) == 0);          // entry boundary: restart (continuing would be kLoopBar)
        REQUIRE (idx (13) == kBeat);
    }
}

TEST_CASE ("SG3: song end without loop is sample exact, the transport keeps running", "[song]")
{
    OfflineRig rig (100);
    rig.setSample (0, makeImpulse());
    rig.setSample (1, makeDc (48000 * 4, 0.5f));
    rig.setTrack (0, 0);
    rig.setTrack (1, 1);
    rig.param (0, TrackParam::Pan, -1.f);
    rig.param (1, TrackParam::Pan, 1.f);
    rig.param (1, TrackParam::Decay, 500.f);
    auto a = laneAll (0);
    auto b = laneAll (0, 32);
    b->bars = 2;
    b->lanes[1].length = 32;
    b->lanes[1].steps[31].active = true;   // a DC hit on the very last step
    rig.setPattern (0, *a);
    rig.setPattern (1, *b);
    rig.bpm (120.0);
    rig.setSong (songOf ({ { 0, 1 }, { 1, 1 } }, false));
    rig.songMode (true);
    rig.play();
    rig.renderUntil (beatAt (12) + 4 * 48000);

    const auto l = onsets (rig.left);
    REQUIRE (! l.empty());
    REQUIRE (l.back() <= beatAt (12) - 1);
    REQUIRE (l.back() == beatAt (11.75));
    const auto s = rig.snapshot();
    REQUIRE (s.songEnded);
    REQUIRE (s.songEndSample == beatAt (12));
    REQUIRE (countEvents (rig, EngineEventType::SongEnded) == 1);
    REQUIRE (rig.core.transportSnapshot().playing);
    REQUIRE (rig.core.transportSnapshot().positionSamples == beatAt (12) + 4 * 48000);

    // the DC tail keeps sounding past the end and dies away
    REQUIRE (std::fabs (rig.right[static_cast<std::size_t> (beatAt (12) + 100)]) > 0.01f);
    REQUIRE (std::fabs (rig.right.back()) < 1e-4f);

    // leaving the song: the pattern schedules again from the next step
    rig.songMode (false);
    const int64_t from = rig.position();
    rig.renderUntil (from + kBeat);
    std::vector<float> tail (rig.left.begin() + from, rig.left.end());
    REQUIRE (! onsets (tail).empty());
    REQUIRE (! rig.snapshot().songEnded);

    SECTION ("empty song and an invalid entry behave like PTN")
    {
        for (int variant = 0; variant < 2; ++variant)
        {
            OfflineRig r2 (128);
            r2.setSample (0, makeImpulse());
            r2.setTrack (0, 0);
            Pattern p;
            p.lanes[0].steps[0].active = true;
            r2.setPattern (0, p);
            r2.bpm (120.0);
            r2.setSong (variant == 0 ? SongData {} : songOf ({ { 9, 1 } }, false));
            r2.songMode (true);
            r2.play();
            r2.renderUntil (beatAt (2));
            REQUIRE (r2.snapshot().songIndex == -1);
            r2.renderUntil (beatAt (17));
            REQUIRE (onsets (r2.left) == beats ({ 0, 4, 8, 12, 16 }));
            REQUIRE (! r2.snapshot().songEnded);
            REQUIRE (r2.snapshot().songIndex == -1);
            REQUIRE (countEvents (r2, EngineEventType::SongEnded) == 0);
        }
    }
}

TEST_CASE ("SG4: position, modes, publishing", "[song]")
{
    SECTION ("cursor when stopped, jump while playing, Stop resets the cursor")
    {
        OfflineRig rig (128);
        kickSnare (rig);
        rig.setSong (songOf ({ { 0, 1 }, { 1, 1 }, { 0, 1 } }));
        rig.songMode (true);
        rig.songPosition (1);
        rig.renderUntil (128);
        REQUIRE (rig.snapshot().currentPattern == 1);
        REQUIRE (rig.snapshot().songCursor == 1);

        rig.play();
        rig.renderUntil (beatAt (2));
        // rig output index = transport position + 128 (one stopped block before Play)
        REQUIRE (onsets (rig.right) == std::vector<int64_t> { 128 });   // Play starts from B
        REQUIRE (rig.snapshot().songIndex == 1);

        rig.songPosition (0);
        rig.renderUntil (beatAt (2) + 128);
        REQUIRE (rig.snapshot().songCursor == 0);
        rig.renderUntil (beatAt (7) + 128);
        REQUIRE (onsets (rig.left) == std::vector<int64_t> { beatAt (4) + 128 });   // jump on the next cycle boundary
        REQUIRE (rig.snapshot().songIndex == 0);
        REQUIRE (rig.snapshot().songRepeat == 0);
        REQUIRE (rig.snapshot().songCursor == -1);

        rig.stop();
        rig.renderUntil (rig.position() + 128);
        REQUIRE (rig.snapshot().songCursor == 0);
    }

    SECTION ("entering the song while playing waits for the cycle of the current pattern")
    {
        OfflineRig rig (128);
        kickSnare (rig);
        Pattern c;   // two bars, kick on slot 0
        c.bars = 2;
        c.lanes[2].length = 32;
        rig.setPattern (2, c);
        rig.cmd (CommandType::QueuePattern, 2);
        rig.setSong (songOf ({ { 1, 1 } }));
        rig.play();
        rig.renderUntil (beatAt (3));
        rig.songMode (true);
        rig.renderUntil (beatAt (3) + 128);
        REQUIRE (! rig.snapshot().songMode);
        REQUIRE (rig.snapshot().songCursor == 0);
        rig.renderUntil (beatAt (9));
        REQUIRE (onsets (rig.right) == beats ({ 8 }));
        REQUIRE (rig.snapshot().songMode);
    }

    SECTION ("QueuePattern leaves the song")
    {
        OfflineRig rig (128);
        kickSnare (rig);
        Pattern c;
        c.lanes[0].steps[2].active = true;
        rig.setPattern (2, c);
        rig.setSong (songOf ({ { 0, 1 }, { 1, 1 } }));
        rig.songMode (true);
        rig.play();
        rig.renderUntil (beatAt (1.5));
        rig.cmd (CommandType::QueuePattern, 2);
        rig.renderUntil (beatAt (1.5) + 128);
        REQUIRE (! rig.snapshot().songMode);
        rig.renderUntil (beatAt (13));
        REQUIRE (rig.snapshot().currentPattern == 2);
        REQUIRE (onsets (rig.left) == beats ({ 0, 4.5, 8.5, 12.5 }));
        REQUIRE (onsets (rig.right).empty());
    }

    SECTION ("SetSong while playing retires the old song once; a shorter song wraps to entry 0")
    {
        OfflineRig rig (128);
        kickSnare (rig);
        const SongData* first = rig.setSong (songOf ({ { 1, 1 }, { 1, 1 }, { 1, 1 } }));
        rig.songMode (true);
        rig.songPosition (2);
        rig.play();
        rig.renderUntil (beatAt (1));
        REQUIRE (rig.snapshot().songIndex == 2);
        rig.setSong (songOf ({ { 0, 1 } }));
        rig.renderUntil (beatAt (5));
        REQUIRE (rig.countRetired (first) == 1);
        REQUIRE (rig.snapshot().songIndex == 0);
        REQUIRE (onsets (rig.left) == beats ({ 4 }));
    }

    SECTION ("releaseAll retires the song and the note map")
    {
        auto rig = std::make_unique<OfflineRig> (128);
        const SongData* song = rig->setSong (songOf ({ { 0, 1 } }));
        const MidiNoteMap* map = rig->noteMap (MidiNoteMap {});
        rig->renderUntil (128);
        rig->core.releaseAll();
        rig->drain();
        REQUIRE (rig->countRetired (song) == 1);
        REQUIRE (rig->countRetired (map) == 1);
    }

    SECTION ("a full retire queue drops SetSong and keeps the old song")
    {
        OfflineRig rig (128);
        kickSnare (rig);
        rig.songMode (true);
        // SetSong swaps nobody drains: each one retires its predecessor until the queue is full
        std::vector<SongData*> songs;
        for (int i = 0; i < 1100; ++i)
            songs.push_back (new SongData (songOf ({ { i < 1000 ? 1 : 0, 1 } })));
        float l[1] {}, r[1] {};
        float* out[2] = { l, r };
        for (std::size_t i = 0; i < songs.size(); ++i)
        {
            rig.cmd (CommandType::SetSong, 0, 0.f, songs[i]);
            if (i % 200 == 199 || i + 1 == songs.size())
                rig.core.process (nullptr, 0, out, 2, 1);
        }
        REQUIRE (rig.snapshot().droppedCommands > 0);
        rig.drain();
        REQUIRE (rig.retiredCount == 1023);
        // accepted: songs[0..1023]; the installed one is songs[1023] (pattern A), the rest were dropped
        const std::size_t accepted = static_cast<std::size_t> (rig.retiredCount) + 1;
        for (std::size_t i = accepted; i < songs.size(); ++i)
            delete songs[i];
        rig.play();
        rig.renderUntil (beatAt (1));
        REQUIRE (rig.snapshot().currentPattern == 0);
        REQUIRE (rig.countRetired (songs[1023]) == 0);
    }
}

TEST_CASE ("SG5: edits during the song", "[song]")
{
    std::vector<float> ref;
    for (int buffer : { 64, 333 })
    {
        OfflineRig rig (buffer);
        kickSnare (rig);
        rig.setSong (songOf ({ { 0, 2 }, { 1, 1 } }));
        rig.songMode (true);
        rig.play();
        rig.renderUntil (beatAt (1.3));
        Pattern a2;
        a2.lanes[0].steps[8].active = true;   // beat 2 instead of 0
        rig.setPattern (0, a2);
        rig.renderUntil (beatAt (5.5));
        rig.bpm (150.0);
        rig.renderUntil (beatAt (5.5) + 4 * 48000);

        const auto l = onsets (rig.left);
        REQUIRE (l.size() >= 2);
        REQUIRE (l[0] == 0);
        REQUIRE (l[1] == beatAt (2));
        const double spb150 = 48000.0 * 60.0 / 150.0;
        REQUIRE (l[2] == beatAt (5.5) + std::llround (0.5 * spb150));
        // after the tempo change: B on the next cycle boundary (beat 8), A again at beat 14 (2 + 12)
        const auto r = onsets (rig.right);
        REQUIRE (r.size() >= 1);
        REQUIRE (r[0] == beatAt (5.5) + std::llround (2.5 * spb150));
        REQUIRE (l[3] == beatAt (5.5) + std::llround (8.5 * spb150));
        if (! ref.empty())
            REQUIRE (rig.left == ref);
        ref = rig.left;
    }
}
