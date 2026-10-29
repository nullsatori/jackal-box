#pragma once

#include "Engine/Inserts/InsertProcessor.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <atomic>
#include <memory>

namespace jackal
{

// An InsertProcessor over a hosted VST3 effect. Created, prepared and destroyed on the message thread;
// process() runs on the audio thread and never allocates on our side.
class Vst3Insert final : public InsertProcessor,
                         private juce::AudioProcessorListener,
                         private juce::AudioPlayHead
{
public:
    enum class Layout { Stereo, MonoInStereoOut, Mono };

    // Picks the first bus layout the plugin accepts; nullptr and an error text when none fits.
    static std::unique_ptr<Vst3Insert> make (std::unique_ptr<juce::AudioPluginInstance>, juce::String& error);
    ~Vst3Insert() override;

    void prepare (double sampleRate, int maxBlockSize) override;
    void reset() noexcept override;
    void process (float* left, float* right, int n, const InsertContext&) noexcept override;
    int latencySamples() const noexcept override { return latency_.load (std::memory_order_relaxed); }
    void setNonRealtime (bool) noexcept override;

    juce::AudioPluginInstance& instance() noexcept { return *instance_; }
    juce::MemoryBlock state() const;
    void setState (const juce::MemoryBlock&);
    juce::String name() const { return instance_->getName(); }

    // Set by any change in the plugin (any thread, the audio thread included); the controller polls it.
    bool consumeChanged() noexcept { return changed_.exchange (false, std::memory_order_relaxed); }

private:
    Vst3Insert (std::unique_ptr<juce::AudioPluginInstance>, Layout);

    void audioProcessorParameterChanged (juce::AudioProcessor*, int, float) override;
    void audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails&) override;
    juce::Optional<PositionInfo> getPosition() const override;
    void changed() noexcept { changed_.store (true, std::memory_order_relaxed); }

    std::unique_ptr<juce::AudioPluginInstance> instance_;
    Layout layout_;
    juce::MidiBuffer midi_;
    std::atomic<int> latency_ { 0 };
    InsertContext ctx_ {};
    std::atomic<bool> changed_ { false };
};

} // namespace jackal
