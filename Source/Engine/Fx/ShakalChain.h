#pragma once

#include "BitcrushFx.h"
#include "BitflipFx.h"
#include "DecimatorFx.h"
#include "DriveFx.h"
#include "FxParams.h"
#include "GlitchBufferFx.h"
#include "Sp12Fx.h"

#include "../ParamIds.h"
#include "../Transport.h"

#include <array>
#include <cstdint>

namespace jackal
{

// Drive -> SP-12 -> Decimator -> Bitcrush -> Bitflip -> GlitchBuffer, one per track bus and one on the master.
// Control runs on 32-sample blocks aligned to the absolute stream position, so the result never depends on buffer size.
class ShakalChain
{
public:
    static constexpr int kControlBlock = 32;
    static constexpr int kMaxPendingTriggers = 128;

    void prepare (double sampleRate, int maxBlockSize, int chainIndex);
    void reset() noexcept;   // Play / Stop

    // Macro override from a step: offset inside the coming block; macro >= 0 sets the override, macro < 0 clears it.
    void onTrigger (int offset, float macro) noexcept;
    void process (float* left, float* right, int n, const ParamTable&, const Transport&, int64_t blockStart,
                  bool playing, uint32_t seed) noexcept;

    float macroActual() const noexcept { return macro_; }
    uint8_t engagedMask() const noexcept { return engaged_; }
    bool overrideActive() const noexcept { return overrideOn_; }
    int latencySamples() const noexcept { return 0; }   // stage 4 seam (PDC)

private:
    void controlUpdate (int64_t pos, float knob, ShakalPreset preset, const FxState& manual, bool bypass) noexcept;
    void cellAt (int64_t pos, bool playing, const Transport&, int64_t& index, int64_t& start, int64_t& end) const noexcept;

    double sampleRate_ = 48000.0;
    int chain_ = 0;
    float smoothA_ = 0.28f;

    DriveFx drive_;
    Sp12Fx sp12_;
    DecimatorFx decim_;
    BitcrushFx crush_;
    BitflipFx flip_;
    GlitchBufferFx glitch_;

    struct PendingTrigger { int64_t pos = 0; int offset = 0; float macro = -1.f; bool resolved = false; };
    std::array<PendingTrigger, kMaxPendingTriggers> pending_ {};
    int numPending_ = 0;

    FxState state_ {};
    bool initialised_ = false, snap_ = true;
    float macro_ = 0.f;
    bool overrideOn_ = false;
    float override_ = 0.f;
    float gainPrev_ = 1.f, gainCur_ = 1.f;
    uint8_t engaged_ = 0;
    int64_t stoppedCounter_ = 0;
};

} // namespace jackal
