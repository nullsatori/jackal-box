#pragma once

// Stage 4 loop helpers: input sources fed into OfflineRig. Inputs are numbers, nothing is played.

#include "FakeInsert.h"
#include "FxTestHelpers.h"
#include "RigHelpers.h"

#include <cstdint>
#include <memory>
#include <vector>

namespace jackal::test
{

constexpr int64_t kLoopBar = 96000;    // 120 BPM, 48 kHz, 4/4
constexpr int64_t kLoopBeat = 24000;

inline Sample* makeLoopTarget (int64_t capacity)
{
    auto* s = new Sample();
    s->numChannels = 2;
    s->data[0].assign (static_cast<std::size_t> (capacity), 0.f);
    s->data[1].assign (static_cast<std::size_t> (capacity), 0.f);
    return s;
}

inline void armLoop (OfflineRig& rig, int slot, int bars, bool overdub, Sample* target)
{
    rig.cmd (CommandType::ArmLoop, slot | (bars << 8) | ((overdub ? 1 : 0) << 16), 0.f, target);
}

inline void setInsertCmd (OfflineRig& rig, int chain, int slot, InsertProcessor* p)
{
    rig.cmd (CommandType::SetInsert, chain | (slot << 8), 0.f, p);
}

// "Guitarist": impulses 1.0 at sampleAtBeat(k + frac) + L_mp + roundTrip for every beat k (L_mp from the rig's snapshot).
inline void heardBeats (OfflineRig& rig, int roundTrip, std::vector<int64_t> offsets = { 0 }, int64_t until = INT64_MAX)
{
    rig.input = [&rig, roundTrip, offsets, until] (int64_t pos, int n, float* in) {
        const auto snap = rig.core.loopSnapshot();
        const int64_t lag = snap.pdcTracks + snap.pdcMaster + roundTrip;
        for (int i = 0; i < n; ++i)
        {
            const int64_t p = pos + i;
            if (p >= until)
                continue;
            for (int64_t off : offsets)
            {
                const int64_t m = p - lag - off;
                if (m >= 0 && m % kLoopBeat == 0)
                    in[i] = 1.f;
            }
        }
    };
}

// Cable from the output back into the input: in[p] = gainBack * out[p - delay] (+ optional noise).
inline void loopback (OfflineRig& rig, int delay, float gainBack, float noiseAmp = 0.f)
{
    rig.input = [&rig, delay, gainBack, noiseAmp] (int64_t pos, int n, float* in) {
        for (int i = 0; i < n; ++i)
        {
            const int64_t src = pos + i - delay;
            float v = (src >= 0 && src < rig.position()) ? gainBack * rig.left[static_cast<std::size_t> (src)] : 0.f;
            if (noiseAmp > 0.f)
            {
                uint32_t x = static_cast<uint32_t> (pos + i) * 2654435761u + 12345u;
                x ^= x << 13; x ^= x >> 17; x ^= x << 5;
                v += noiseAmp * (static_cast<float> (x) / 4294967296.f * 2.f - 1.f);
            }
            in[i] = v;
        }
    };
}

// Empty one-bar pattern (or `bars` bars) with the given loop slot, published as pattern `index`.
inline void loopPattern (OfflineRig& rig, int index, int loopSlot, int bars = 1)
{
    Pattern p;
    p.bars = static_cast<uint8_t> (bars);
    p.loopSlot = static_cast<int8_t> (loopSlot);
    rig.setPattern (index, p);
}

inline int findEvent (const OfflineRig& rig, EngineEventType type, int slot = -1)
{
    for (std::size_t i = 0; i < rig.events.size(); ++i)
        if (rig.events[i].type == type && (slot < 0 || rig.events[i].track == slot))
            return static_cast<int> (i);
    return -1;
}

inline int countEvents (const OfflineRig& rig, EngineEventType type)
{
    int c = 0;
    for (const auto& e : rig.events)
        c += e.type == type ? 1 : 0;
    return c;
}

} // namespace jackal::test
