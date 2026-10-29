#include "Calibrator.h"

#include <algorithm>
#include <cmath>

namespace jackal
{

void Calibrator::prepare (double sampleRate, int)
{
    sampleRate_ = sampleRate;
    state_ = CalState::Idle;
}

void Calibrator::start() noexcept
{
    state_ = CalState::Running;
    t_ = 0;
    emitAt_ = std::llround (0.1 * sampleRate_);
    pulseAt_ = -1;
    attempt_ = 0;
    result_ = -1;
}

void Calibrator::cancel() noexcept
{
    state_ = CalState::Idle;
}

bool Calibrator::process (const float* rawIn, float* const* out, int numOut, int n) noexcept
{
    if (state_ != CalState::Running)
        return false;

    const int64_t window = std::llround (2.0 * sampleRate_);
    for (int k = 0; k < n; ++k, ++t_)
    {
        if (pulseAt_ < 0 && t_ == emitAt_)
        {
            for (int c = 0; c < numOut; ++c)
                if (out != nullptr && out[c] != nullptr)
                    out[c][k] += 0.9f;
            pulseAt_ = t_;
            continue;
        }
        if (pulseAt_ < 0)
            continue;

        if (rawIn != nullptr && std::fabs (rawIn[k]) > 0.1f)
        {
            found_[static_cast<std::size_t> (attempt_++)] = t_ - pulseAt_;
            pulseAt_ = -1;
            if (attempt_ == 3)
            {
                auto f = found_;
                std::sort (f.begin(), f.end());
                result_ = static_cast<int32_t> (f[1]);
                state_ = CalState::Done;
                return true;
            }
            emitAt_ = t_ + std::llround (0.5 * sampleRate_);
        }
        else if (t_ - pulseAt_ > window)
        {
            state_ = CalState::Failed;
            return true;
        }
    }
    return false;
}

} // namespace jackal
