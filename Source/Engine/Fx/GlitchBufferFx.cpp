#include "GlitchBufferFx.h"

#include "FxHash.h"

#include <algorithm>
#include <cmath>

namespace jackal
{

void GlitchBufferFx::prepare (double sampleRate, int maxBlockSize)
{
    sampleRate_ = sampleRate;
    ringLen_ = static_cast<int> (std::ceil (sampleRate)) + std::max (1, maxBlockSize);
    for (auto& r : ring_)
        r.assign (static_cast<std::size_t> (ringLen_), 0.f);
    reset();
}

void GlitchBufferFx::reset() noexcept
{
    for (auto& r : ring_)
        std::fill (r.begin(), r.end(), 0.f);
    w_ = 0;
    curCell_ = INT64_MIN;
    curCellStart_ = INT64_MIN;
    event_ = false;
}

float GlitchBufferFx::ringAt (int c, int64_t index) const noexcept
{
    const int64_t m = ((index % ringLen_) + ringLen_) % ringLen_;
    return ring_[c][static_cast<std::size_t> (m)];
}

void GlitchBufferFx::writeRing (const float* left, const float* right, int n) noexcept
{
    for (int i = 0; i < n; ++i)
    {
        const auto m = static_cast<std::size_t> (w_ % ringLen_);
        ring_[0][m] = left != nullptr ? left[i] : 0.f;
        ring_[1][m] = right != nullptr ? right[i] : 0.f;
        ++w_;
    }
}

void GlitchBufferFx::process (float* left, float* right, int n, const FxState& s, const FxContext& ctx) noexcept
{
    // cell bookkeeping happens whether or not the effect is on: switching on mid-cell waits for the next cell
    if (ctx.cellIndex != curCell_ || ctx.cellStartSample != curCellStart_)
    {
        curCell_ = ctx.cellIndex;
        curCellStart_ = ctx.cellStartSample;
        event_ = false;

        if (s.glitchOn && ctx.pos == ctx.cellStartSample)
        {
            const double u1 = FxHash::unit (ctx.seed, FxHash::kSaltGlitchGate, ctx.chainIndex, ctx.cellIndex);
            if (u1 < static_cast<double> (s.glitchProb))
            {
                event_ = true;
                type_ = typeFor (FxHash::unit (ctx.seed, FxHash::kSaltGlitchType, ctx.chainIndex, ctx.cellIndex));
                w0_ = w_;
                // never read further back than the ring holds (only matters below 60 BPM, which the transport forbids)
                cellLen_ = std::clamp<int64_t> (ctx.cellLength, 1, (ringLen_ - 1) / 2);
                loopLen_ = std::clamp<int64_t> (std::llround (static_cast<double> (s.glitchLenMs) * sampleRate_ / 1000.0), 1, cellLen_);
                fade_ = cellLen_ < 128 ? static_cast<int> (cellLen_ / 2) : 64;
            }
        }
    }

    if (! s.glitchOn)
        event_ = false;

    if (! event_)
    {
        writeRing (left, right, n);
        return;
    }

    float* ch[2] = { left, right };
    for (int i = 0; i < n; ++i)
    {
        const int64_t k = ctx.pos + i - curCellStart_;

        // write first, then read
        const auto m = static_cast<std::size_t> (w_ % ringLen_);
        ring_[0][m] = left != nullptr ? left[i] : 0.f;
        ring_[1][m] = right != nullptr ? right[i] : 0.f;
        ++w_;

        if (k < 0 || k >= cellLen_)
            continue;   // output = input

        int64_t idx = 0;
        switch (type_)
        {
            case Type::Stutter: idx = w0_ + (k % loopLen_); break;
            case Type::Reverse: idx = w0_ - 1 - k; break;
            case Type::Repeat:  idx = w0_ - cellLen_ + k; break;
        }

        float g = 1.f;
        if (fade_ > 0)
        {
            if (k < fade_)
                g = static_cast<float> (k) / static_cast<float> (fade_);
            else if (k > cellLen_ - fade_)
                g = static_cast<float> (cellLen_ - k) / static_cast<float> (fade_);
        }

        for (int c = 0; c < 2; ++c)
        {
            float* p = ch[c];
            if (p == nullptr)
                continue;
            const float wet = ringAt (c, idx);
            p[i] = g >= 1.f ? wet : p[i] * (1.f - g) + wet * g;
        }
    }
}

} // namespace jackal
