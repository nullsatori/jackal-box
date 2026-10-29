#include "Transport.h"

#include <algorithm>
#include <cmath>

namespace jackal
{

void Transport::prepare (double sampleRate) noexcept
{
    sampleRate_ = sampleRate > 0.0 ? sampleRate : 48000.0;
    playing_ = false;
    positionSamples_ = 0;
    anchor_ = { 0, 0.0, anchor_.bpm };
}

void Transport::play() noexcept
{
    playing_ = true;
    positionSamples_ = 0;
    anchor_ = { 0, 0.0, anchor_.bpm };
}

void Transport::stop() noexcept
{
    playing_ = false;
    positionSamples_ = 0;
    anchor_ = { 0, 0.0, anchor_.bpm };
}

void Transport::setBpm (double bpm) noexcept
{
    const double clamped = std::clamp (bpm, kMinBpm, kMaxBpm);
    anchor_ = { positionSamples_, positionBeats(), clamped };
}

void Transport::advance (int numSamples) noexcept
{
    if (playing_ && numSamples > 0)
        positionSamples_ += numSamples;
}

double Transport::samplesPerBeat() const noexcept
{
    return sampleRate_ * 60.0 / anchor_.bpm;
}

int64_t Transport::sampleOfBeat (int64_t beatIndex) const noexcept
{
    const double offset = (static_cast<double> (beatIndex) - anchor_.beat) * samplesPerBeat();
    return anchor_.sample + static_cast<int64_t> (std::llround (offset));
}

double Transport::positionBeats() const noexcept
{
    return anchor_.beat + static_cast<double> (positionSamples_ - anchor_.sample) / samplesPerBeat();
}

TransportState Transport::state() const noexcept
{
    return { playing_, anchor_.bpm, positionSamples_, positionBeats() };
}

} // namespace jackal
