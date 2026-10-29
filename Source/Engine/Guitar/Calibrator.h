#pragma once

#include "../EngineTypes.h"

#include <array>
#include <cstdint>

namespace jackal
{

// Measures the output -> input round trip with three single-sample pulses (median).
class Calibrator
{
public:
    void prepare (double sampleRate, int maxBlockSize);
    void start() noexcept;
    void cancel() noexcept;
    // Writes pulses into out (after the master copy) and looks for them in the raw input. True in the block it finishes.
    bool process (const float* rawIn, float* const* out, int numOut, int n) noexcept;
    CalState state() const noexcept { return state_; }
    int32_t result() const noexcept { return result_; }

private:
    double sampleRate_ = 48000.0;
    CalState state_ = CalState::Idle;
    int64_t t_ = 0, emitAt_ = 0, pulseAt_ = -1;
    int attempt_ = 0;
    std::array<int64_t, 3> found_ {};
    int32_t result_ = -1;
};

} // namespace jackal
