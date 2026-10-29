#pragma once

#include "Engine/Sample.h"
#include "Model/Project.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_events/juce_events.h>

#include <deque>
#include <functional>
#include <memory>
#include <vector>

namespace jackal
{

// Waveform summary for the UI: min/max per bucket of the mono mix.
struct WavePeaks
{
    std::vector<float> mins, maxs;
    int64_t length = 0;
};

WavePeaks computePeaks (const Sample&, int buckets = 1024);

struct LoadedSample
{
    int slot = -1;
    int generation = 0;
    std::unique_ptr<Sample> sample;
    SampleSlotSpec spec;          // may carry new transient boundaries / guessed bars
    WavePeaks peaks;
    juce::String error;
};

// Decodes WAV/AIFF on its own thread. Results come back on the message thread.
class SampleLoader : private juce::Thread
{
public:
    using Callback = std::function<void (LoadedSample&&)>;

    explicit SampleLoader (Callback onLoaded);
    ~SampleLoader() override;

    // guessBars: set spec.sourceBars from the length when it is 0
    void load (int slot, int generation, const juce::File& file, const SampleSlotSpec& spec, bool guessBars);

private:
    struct Job { int slot; int generation; juce::File file; SampleSlotSpec spec; bool guessBars; };

    void run() override;
    LoadedSample decode (const Job&);

    Callback onLoaded_;
    juce::AudioFormatManager formats_;
    juce::CriticalSection lock_;
    std::deque<Job> jobs_;
    juce::WaitableEvent wake_;
    std::shared_ptr<bool> alive_ = std::make_shared<bool> (true);
};

} // namespace jackal
