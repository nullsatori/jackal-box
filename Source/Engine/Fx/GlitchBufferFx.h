#pragma once

#include "FxParams.h"

#include <cstdint>
#include <vector>

namespace jackal
{

// Ring buffer with per-cell events (Stutter / Reverse / Repeat) on the 1/16 grid.
// The ring is written on every sample, even when the effect or the whole chain is off.
class GlitchBufferFx
{
public:
    enum class Type : uint8_t { Stutter, Reverse, Repeat };

    void prepare (double sampleRate, int maxBlockSize);
    void reset() noexcept;
    // All n samples belong to the cell described by ctx (ShakalChain splits at cell boundaries).
    void process (float* left, float* right, int n, const FxState&, const FxContext&) noexcept;
    void writeRing (const float* left, const float* right, int n) noexcept;

    static Type typeFor (double u2) noexcept { return u2 < 0.5 ? Type::Stutter : (u2 < 0.8 ? Type::Reverse : Type::Repeat); }
    int ringLength() const noexcept { return ringLen_; }

private:
    float ringAt (int c, int64_t index) const noexcept;

    double sampleRate_ = 48000.0;
    std::vector<float> ring_[2];
    int ringLen_ = 1;
    int64_t w_ = 0;                     // absolute write counter; index = w_ mod ringLen_

    int64_t curCell_ = INT64_MIN, curCellStart_ = INT64_MIN;
    bool event_ = false;
    Type type_ = Type::Stutter;
    int64_t w0_ = 0;
    int64_t cellLen_ = 0, loopLen_ = 1;
    int fade_ = 64;
};

} // namespace jackal
