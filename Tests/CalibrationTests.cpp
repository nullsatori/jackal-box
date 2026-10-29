// Stage 4: round-trip calibration against a modelled cable (numbers only, nothing reaches a device).

#include "LoopTestHelpers.h"

#include <catch2/catch_test_macros.hpp>

using namespace jackal;
using namespace jackal::test;

TEST_CASE ("C1: calibration measures the loopback", "[calibration]")
{
    for (float noise : { 0.f, std::pow (10.f, -50.f / 20.f) })
    {
        OfflineRig rig (128);
        loopback (rig, 1234, 0.5f, noise);
        rig.cmd (CommandType::StartCalibration);
        rig.renderSeconds (3.0);
        const auto snap = rig.core.loopSnapshot();
        REQUIRE (snap.cal == CalState::Done);
        REQUIRE (snap.calSamples == 1234);
        REQUIRE (countEvents (rig, EngineEventType::CalibrationDone) == 1);
        REQUIRE (rig.events[static_cast<std::size_t> (findEvent (rig, EngineEventType::CalibrationDone))].value == 1);
        bool pulse = false;
        for (float v : rig.left)
            pulse = pulse || std::fabs (v - 0.9f) < noise + 1e-6f;
        REQUIRE (pulse);
    }

    {
        OfflineRig rig (128);
        loopback (rig, 1234, 0.f);
        rig.cmd (CommandType::StartCalibration);
        rig.renderSeconds (2.5);
        REQUIRE (rig.core.loopSnapshot().cal == CalState::Failed);
        REQUIRE (rig.events[static_cast<std::size_t> (findEvent (rig, EngineEventType::CalibrationDone))].value == 0);
    }

    {
        OfflineRig rig (128);
        loopback (rig, 1234, 0.5f);
        rig.cmd (CommandType::StartCalibration);
        rig.renderSeconds (0.05);
        REQUIRE (rig.core.loopSnapshot().cal == CalState::Running);
        rig.cmd (CommandType::CancelCalibration);
        rig.renderSeconds (0.01);
        REQUIRE (rig.core.loopSnapshot().cal == CalState::Idle);
    }

    {
        // pulses go to the outputs after the master copy: a loop recorded meanwhile does not contain them
        OfflineRig rig (128);
        rig.core.params().monitorEnabled = false;
        loopPattern (rig, 0, 8);
        rig.bpm (120.0);
        loopback (rig, 1234, 0.5f);
        rig.play();
        Sample* s = makeLoopTarget (kLoopBar);
        armLoop (rig, 8, 1, false, s);
        rig.renderUntil (kLoopBar + 1000);
        rig.cmd (CommandType::StartCalibration);
        rig.renderUntil (3 * kLoopBar);
        REQUIRE (rig.core.bank().get (8) == s);
        for (float v : s->data[0])
            REQUIRE (v == 0.f);
    }
}
