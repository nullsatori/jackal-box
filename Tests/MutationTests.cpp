#include "Model/Mutation.h"
#include "Model/PatternOps.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>

using namespace jackal;

namespace
{
    std::unique_ptr<Pattern> groove()
    {
        auto p = std::make_unique<Pattern>();
        for (int t = 0; t < kNumTracks; ++t)
            for (int s = t % 3; s < 16; s += 2)
                p->lanes[static_cast<std::size_t> (t)].steps[static_cast<std::size_t> (s)].active = true;
        return p;
    }
} // namespace

TEST_CASE ("M1: mutation is deterministic and keeps the groove", "[mutation]")
{
    MutationInput in;
    in.sliced[7] = true;
    in.numSlices[7] = 32;

    auto base = groove();
    auto a = std::make_unique<Pattern> (*base), b = std::make_unique<Pattern> (*base);
    mutate (*a, in, 1.f, 1);
    mutate (*b, in, 1.f, 1);
    REQUIRE (*a == *b);
    REQUIRE (! (*a == *base));

    auto zero = std::make_unique<Pattern> (*base);
    mutate (*zero, in, 0.f, 99);
    REQUIRE (*zero == *base);

    for (int t = 0; t < kNumTracks; ++t)
        for (int s = 0; s < kMaxSteps; ++s)
        {
            const auto& m = a->lanes[static_cast<std::size_t> (t)].steps[static_cast<std::size_t> (s)];
            const auto& o = base->lanes[static_cast<std::size_t> (t)].steps[static_cast<std::size_t> (s)];
            REQUIRE (m.active == o.active);
            if (! o.active)
                REQUIRE (m == o);
            REQUIRE (m.velocity >= 1);
            REQUIRE (m.velocity <= 127);
            REQUIRE (m.ratchet >= 1);
            REQUIRE (m.ratchet <= 64);
            REQUIRE (m.nudge >= -25);
            REQUIRE (m.nudge <= 25);
            REQUIRE (m.probability <= 100);
            REQUIRE (m.slice < 32);
            if (t != 7)
            {
                REQUIRE (m.slice == o.slice);
                REQUIRE (m.reverse == o.reverse);
            }
        }
}

TEST_CASE ("O1: pattern ops", "[ops]")
{
    auto p = std::make_unique<Pattern>();
    p->lanes[0].steps[3].active = true;
    p->lanes[0].steps[3].velocity = 77;
    p->glitch.pad[2] = 4;

    auto d = std::make_unique<Pattern> (*p);
    ops::doubleLength (*d);
    REQUIRE (d->bars == 2);
    REQUIRE (d->lanes[0].length == 32);
    REQUIRE (d->lanes[0].steps[19] == p->lanes[0].steps[3]);
    REQUIRE (d->glitch.pad[18] == 4);

    auto full = std::make_unique<Pattern> (*p);
    full->bars = 4;
    full->lanes[0].length = 64;
    ops::doubleLength (*full);
    REQUIRE (full->bars == 4);
    REQUIRE (full->lanes[0].length == 64);

    auto two = std::make_unique<Pattern>();
    two->bars = 2;
    two->lanes[1].length = 64;
    ops::doubleLength (*two);
    REQUIRE (two->bars == 4);
    REQUIRE (two->lanes[1].length == 64);

    auto s = std::make_unique<Pattern>();
    s->lanes[0].length = 8;
    s->lanes[0].steps[7].active = true;
    s->lanes[0].steps[20].active = true;   // outside the lane: untouched
    ops::shiftLane (*s, 0, 1);
    REQUIRE (s->lanes[0].steps[0].active);
    REQUIRE (! s->lanes[0].steps[7].active);
    REQUIRE (s->lanes[0].steps[20].active);
    ops::shiftLane (*s, 0, -1);
    REQUIRE (s->lanes[0].steps[7].active);

    auto r = std::make_unique<Pattern>();
    for (int i : { 0, 2, 4 })
        r->lanes[0].steps[static_cast<std::size_t> (i)].active = true;
    ops::rampVelocity (*r, 0, 0, 4, 20, 100);
    REQUIRE (r->lanes[0].steps[0].velocity == 20);
    REQUIRE (r->lanes[0].steps[2].velocity == 60);
    REQUIRE (r->lanes[0].steps[4].velocity == 100);
    REQUIRE (! r->lanes[0].steps[1].active);
    REQUIRE (r->lanes[0].steps[1].velocity == 100);

    auto c = std::make_unique<Pattern>();
    ops::copyPattern (*p, *c);
    REQUIRE (*c == *p);

    REQUIRE (ops::nextRatchet (1, 1) == 2);
    REQUIRE (ops::nextRatchet (4, 1) == 6);
    REQUIRE (ops::nextRatchet (64, 1) == 64);
    REQUIRE (ops::nextRatchet (2, -1) == 1);
}
