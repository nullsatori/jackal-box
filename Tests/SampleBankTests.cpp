#include "RigHelpers.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using namespace jackal;
using namespace jackal::test;

TEST_CASE ("B1: swapping a sample while it plays", "[bank]")
{
    auto pat = std::make_unique<Pattern>();
    pat->lanes[0].length = 64;
    pat->lanes[0].steps[0].active = true;

    OfflineRig rig (128);
    auto* first = makeDc (96000, 0.5f);
    rig.setSample (0, first);
    rig.setTrack (0, 0);
    rig.setPattern (0, *pat);
    rig.play();
    rig.renderUntil (1280);
    REQUIRE (rig.left.back() != 0.f);

    rig.setSample (0, makeDc (96000, 0.25f));
    rig.renderUntil (1280 + 128);
    // the old voice stopped in that very block, nothing else plays until the next hit
    REQUIRE (std::all_of (rig.left.begin() + 1280, rig.left.end(), [] (float v) { return v == 0.f; }));
    REQUIRE (std::count (rig.retiredPtrs.begin(), rig.retiredPtrs.end(), static_cast<const void*> (first)) == 1);

    rig.renderSeconds (2.0);   // ASan: nothing reads the deleted sample

    rig.setSample (0, nullptr);
    rig.renderSeconds (1.0);

    // full retire queue: the command is dropped and the old sample keeps playing
    OfflineRig full (128);
    auto* keep = makeDc (96000, 0.5f);
    full.setSample (0, keep);
    full.setTrack (0, 0);
    full.setPattern (0, *pat);
    full.play();
    full.renderUntil (128);
    static int dummy = 0;
    while (full.core.retired().push ({ Retired::Kind::Sample, &dummy })) {}
    auto* rejected = makeDc (100, 0.1f);
    full.setSample (0, rejected);
    {
        std::vector<float> l (128), r (128);
        float* out[2] = { l.data(), r.data() };
        full.core.process (nullptr, 0, out, 2, 128);
        REQUIRE (l[100] != 0.f);
    }
    REQUIRE (full.core.sequencerSnapshot().droppedCommands == 1);
    Retired r;
    while (full.core.retired().pop (r)) {}
    delete rejected;   // never reached the engine
}
