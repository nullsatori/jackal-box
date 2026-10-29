#pragma once

#include <cstdint>

namespace jackal
{

struct InsertContext
{
    double sampleRate = 48000.0;
    double bpm = 120.0;
    int64_t positionSamples = 0;
    bool playing = false;
};

// An effect in an insert slot. The engine knows only this interface; the app implements it over VST3.
class InsertProcessor
{
public:
    virtual ~InsertProcessor() = default;
    virtual void prepare (double sampleRate, int maxBlockSize) = 0;   // never on the audio thread
    virtual void reset() noexcept = 0;                                 // Play / Stop
    virtual void process (float* left, float* right, int n, const InsertContext&) noexcept = 0;   // in place, stereo
    virtual int latencySamples() const noexcept = 0;                   // read by the audio thread every block
    virtual void setNonRealtime (bool) noexcept {}                     // stage 5 seam (offline export)
};

} // namespace jackal
