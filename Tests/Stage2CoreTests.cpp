#include "RigHelpers.h"

#include "Engine/SpscQueue.h"

#include <catch2/catch_test_macros.hpp>

#include <thread>

using namespace jackal;
using namespace jackal::test;

TEST_CASE ("C1: SpscQueue for retire and MIDI", "[queue]")
{
    SpscQueue<MidiEvent, 8> small;
    for (int i = 0; i < 7; ++i)
        REQUIRE (small.push ({ static_cast<uint8_t> (i), 0, 0 }));
    REQUIRE (! small.push ({}));
    MidiEvent m;
    for (int i = 0; i < 7; ++i)
    {
        REQUIRE (small.pop (m));
        REQUIRE (m.status == i);
    }
    REQUIRE (! small.pop (m));

    constexpr int kCount = 1000000;
    {
        RetireQueue q;
        std::thread producer ([&] {
            for (int i = 0; i < kCount;)
                if (q.push ({ Retired::Kind::Sample, reinterpret_cast<const void*> (static_cast<uintptr_t> (i + 1)) }))
                    ++i;
        });
        int expected = 1;
        Retired r;
        while (expected <= kCount)
            if (q.pop (r))
            {
                REQUIRE (reinterpret_cast<uintptr_t> (r.ptr) == static_cast<uintptr_t> (expected));
                ++expected;
            }
        producer.join();
    }
    {
        MidiQueue q;
        std::thread producer ([&] {
            for (int i = 0; i < kCount;)
                if (q.push ({ static_cast<uint8_t> (i & 0xFF), static_cast<uint8_t> ((i >> 8) & 0x7F), 0 }))
                    ++i;
        });
        int expected = 0;
        MidiEvent e;
        while (expected < kCount)
            if (q.pop (e))
            {
                REQUIRE (e.status == (expected & 0xFF));
                REQUIRE (e.data1 == ((expected >> 8) & 0x7F));
                ++expected;
            }
        producer.join();
    }
}

TEST_CASE ("E1: master to outputs", "[core]")
{
    auto pat = laneAll (0);
    for (int numOut : { 1, 6 })
    {
        EngineCore core;
        core.prepare (48000.0, 256, 0, numOut);
        core.params().metronomeEnabled = false;
        core.params().monitorEnabled = false;
        core.commands().push ({ CommandType::SetSlotSample, 0.f, 0, makeDc (4000, 0.5f) });
        core.commands().push ({ CommandType::SetTrackSlot, 0.f, 0 });
        core.commands().push ({ CommandType::SetPattern, 0.f, 0, new Pattern (*pat) });
        core.commands().push ({ CommandType::Play, 0.f, 0 });
        core.params().params.set (trackParam (0, TrackParam::Pan), -1.f);

        std::vector<std::vector<float>> bufs (static_cast<std::size_t> (numOut), std::vector<float> (256, 7.f));
        std::vector<float*> out;
        for (auto& b : bufs)
            out.push_back (b.data());
        core.process (nullptr, 0, out.data(), numOut, 256);

        if (numOut == 1)
        {
            // pan hard left: mono output is half the left channel
            REQUIRE (bufs[0][100] != 0.f);
        }
        else
        {
            REQUIRE (bufs[0][100] != 0.f);
            REQUIRE (bufs[1][100] == 0.f);
            for (int c = 2; c < numOut; ++c)
                for (float v : bufs[static_cast<std::size_t> (c)])
                    REQUIRE (v == 0.f);
        }

        core.releaseAll();
        Retired r;
        while (core.retired().pop (r))
        {
            if (r.kind == Retired::Kind::Pattern) delete static_cast<const Pattern*> (r.ptr);
            else delete static_cast<const Sample*> (r.ptr);
        }
    }

    // mono fold is exactly 0.5 (L + R)
    EngineCore a, b;
    for (auto* c : { &a, &b })
    {
        c->prepare (48000.0, 256, 0, 2);
        c->params().metronomeEnabled = false;
        c->params().monitorEnabled = false;
        c->commands().push ({ CommandType::SetSlotSample, 0.f, 0, makeRamp (4000) });
        c->commands().push ({ CommandType::SetTrackSlot, 0.f, 0 });
        c->commands().push ({ CommandType::SetPattern, 0.f, 0, new Pattern (*pat) });
        c->commands().push ({ CommandType::Play, 0.f, 0 });
        c->params().params.set (trackParam (0, TrackParam::Pan), -0.3f);
    }
    std::vector<float> l (256), r (256), m (256);
    float* stereo[2] = { l.data(), r.data() };
    float* mono[1] = { m.data() };
    a.process (nullptr, 0, stereo, 2, 256);
    b.process (nullptr, 0, mono, 1, 256);
    for (int i = 0; i < 256; ++i)
        REQUIRE (m[static_cast<std::size_t> (i)] == 0.5f * (l[static_cast<std::size_t> (i)] + r[static_cast<std::size_t> (i)]));
    for (auto* c : { &a, &b })
    {
        c->releaseAll();
        Retired rr;
        while (c->retired().pop (rr))
        {
            if (rr.kind == Retired::Kind::Pattern) delete static_cast<const Pattern*> (rr.ptr);
            else delete static_cast<const Sample*> (rr.ptr);
        }
    }
}
