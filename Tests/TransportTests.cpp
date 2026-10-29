#include "Engine/Transport.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace jackal;

TEST_CASE ("T5: advance moves position in samples and beats", "[transport]")
{
    Transport t;
    t.prepare (48000.0);
    t.setBpm (120.0);
    t.play();

    t.advance (24000); // half a second = 1 beat at 120 BPM
    auto s = t.state();
    REQUIRE (s.positionSamples == 24000);
    REQUIRE (s.positionBeats == Catch::Approx (1.0));

    t.advance (12000);
    REQUIRE (t.state().positionBeats == Catch::Approx (1.5));
}

TEST_CASE ("T5: advance does nothing while stopped", "[transport]")
{
    Transport t;
    t.prepare (48000.0);
    t.advance (1000);
    REQUIRE (t.state().positionSamples == 0);
}

TEST_CASE ("T5: setBpm does not jump positionBeats and anchors correctly", "[transport]")
{
    Transport t;
    t.prepare (48000.0);
    t.setBpm (172.0);
    t.play();
    t.advance (12345);

    const double before = t.state().positionBeats;
    t.setBpm (90.0);
    REQUIRE (t.state().positionBeats == Catch::Approx (before).margin (1e-12));

    const auto& a = t.anchor();
    REQUIRE (a.sample == 12345);
    REQUIRE (t.sampleOfBeat (static_cast<int64_t> (a.beat)) <= a.sample);

    // a beat index equal to the anchor beat maps to the anchor sample
    Transport u;
    u.prepare (48000.0);
    u.play();
    u.setBpm (100.0);
    REQUIRE (u.sampleOfBeat (0) == u.anchor().sample);
}

TEST_CASE ("T5: BPM is clamped to 60..300", "[transport]")
{
    Transport t;
    t.prepare (48000.0);
    t.setBpm (59.0);
    REQUIRE (t.state().bpm == 60.0);
    t.setBpm (301.0);
    REQUIRE (t.state().bpm == 300.0);
}
