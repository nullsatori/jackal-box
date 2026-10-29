#pragma once

#include "Transport.h"

#include <array>
#include <cstdint>
#include <vector>

namespace jackal
{

enum class GlitchPad : int { Stutter16, Stutter32, Stutter64, Reverse, TapeStop, Crush, Gate, PitchDive };

// Eight performance pads on the master bus. The input is always written into a ring;
// at most one pad is active. A pad starts on the next 1/16 while playing, at once when stopped.
class GlitchProcessor
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset() noexcept;

    void padDown (int offset, int pad, const Transport&, int64_t blockStart) noexcept;
    void padUp (int offset, int pad) noexcept;   // only acts on the active / pending pad with this index
    void process (float* left, float* right, int n, const Transport&, int64_t blockStart) noexcept;

    int activePad() const noexcept { return active_; }
    int pendingPad() const noexcept { return pending_; }

private:
    struct Event { int64_t time = 0; int pad = -1; bool down = false; };

    float readRing (const std::vector<float>& ring, double pos) const noexcept;
    float readRingInt (const std::vector<float>& ring, int64_t pos) const noexcept;

    double sampleRate_ = 48000.0;
    std::vector<float> ringL_, ringR_;
    int64_t ringSize_ = 1;
    int64_t clock_ = 0;

    std::array<Event, 64> events_ {};
    int numEvents_ = 0;

    int active_ = -1, pending_ = -1;
    int64_t pendingStart_ = 0;
    int64_t startA_ = 0;          // clock at activation; w0 == startA_
    double readPos_ = 0.0;
    bool releasing_ = false;
    float mix_ = 0.f;
    int holdCount_ = 0;
    float heldL_ = 0.f, heldR_ = 0.f;
};

} // namespace jackal
