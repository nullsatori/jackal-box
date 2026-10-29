#pragma once

#include "Calibrator.h"
#include "LoopPlayer.h"
#include "LoopRecorder.h"
#include "NoiseGate.h"

#include "../EngineTypes.h"
#include "../Fx/Biquad.h"
#include "../Fx/ShakalChain.h"
#include "../InputMonitor.h"
#include "../Inserts/InsertChain.h"

#include <vector>

namespace jackal
{

struct GuitarContext
{
    const float* const* in = nullptr;
    int numIn = 0;
    int64_t blockStart = 0;
    bool playing = false;
    int lMasterPath = 0;          // L_mp
    int32_t loopCompDevice = 0;   // EngineParams::loopCompSamples
    uint32_t seed = 0;
    InsertContext insertCtx {};
};

// Input -> gate -> low cut -> DI point -> inserts -> AMP point -> SHAKAL (chain 9) -> master.
class GuitarStrip
{
public:
    void prepare (double sampleRate, int maxBlockSize, int numInputs);
    void reset() noexcept;   // gate, filter, monitor gain state

    void process (float* masterL, float* masterR, int n, const ParamTable&, const EngineParams&, const Transport&,
                  const SampleBank&, const GuitarContext&, ShakalChain& chain9, InsertChain& inserts9,
                  LoopRecorder&, LoopPlayer&, LoopResult&) noexcept;

    int inputChannel() const noexcept { return monitor_.inputChannel(); }
    void setInputChannel (int c) noexcept { monitor_.setInputChannel (c); }
    int lastComp() const noexcept { return lastComp_; }
    const float* output (int ch) const noexcept { return ch == 0 ? l_.data() : r_.data(); }   // what (7) added to the master
    static int startOffsetFor (int lMasterPath, int lGuitar, bool tapAmp) noexcept { return std::max (0, lMasterPath - (tapAmp ? 0 : lGuitar)); }

private:
    InputMonitor monitor_;
    NoiseGate gate_;
    Biquad hpf_;
    float hpfHz_ = -1.f;
    double sampleRate_ = 48000.0;
    std::vector<float> mono_, l_, r_;
    int lastComp_ = 0;
};

} // namespace jackal
