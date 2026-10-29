#include "Vst3Insert.h"

namespace jackal
{

std::unique_ptr<Vst3Insert> Vst3Insert::make (std::unique_ptr<juce::AudioPluginInstance> inst, juce::String& error)
{
    if (inst == nullptr)
    {
        error = "plugin did not load";
        return nullptr;
    }

    using Set = juce::AudioChannelSet;
    const std::pair<Layout, std::pair<Set, Set>> tries[] {
        { Layout::Stereo,          { Set::stereo(), Set::stereo() } },
        { Layout::MonoInStereoOut, { Set::mono(),   Set::stereo() } },
        { Layout::Mono,            { Set::mono(),   Set::mono() } },
    };
    for (const auto& [layout, sets] : tries)
    {
        juce::AudioProcessor::BusesLayout l;
        l.inputBuses.add (sets.first);
        l.outputBuses.add (sets.second);
        if (inst->setBusesLayout (l))
            return std::unique_ptr<Vst3Insert> (new Vst3Insert (std::move (inst), layout));
    }
    error = inst->getName() + ": no stereo or mono layout";
    return nullptr;
}

Vst3Insert::Vst3Insert (std::unique_ptr<juce::AudioPluginInstance> inst, Layout layout)
    : instance_ (std::move (inst)), layout_ (layout)
{
    midi_.ensureSize (256);
    instance_->addListener (this);
    instance_->setPlayHead (this);
    latency_.store (instance_->getLatencySamples());
}

Vst3Insert::~Vst3Insert()
{
    instance_->removeListener (this);
    instance_->setPlayHead (nullptr);
    instance_->releaseResources();
    instance_.reset();
}

void Vst3Insert::prepare (double sampleRate, int maxBlockSize)
{
    instance_->setPlayHead (this);
    instance_->releaseResources();
    instance_->setRateAndBufferSizeDetails (sampleRate, maxBlockSize);
    instance_->prepareToPlay (sampleRate, maxBlockSize);
    latency_.store (instance_->getLatencySamples());
}

void Vst3Insert::reset() noexcept
{
    instance_->reset();
}

void Vst3Insert::setNonRealtime (bool nonRealtime) noexcept
{
    instance_->setNonRealtime (nonRealtime);
}

void Vst3Insert::process (float* left, float* right, int n, const InsertContext& ctx) noexcept
{
    ctx_ = ctx;
    midi_.clear();
    float* chans[2] = { left, right };
    const int numChannels = layout_ == Layout::Mono ? 1 : 2;
    juce::AudioBuffer<float> buf (chans, numChannels, n);   // over our buffers, no allocation
    instance_->processBlock (buf, midi_);
    if (layout_ == Layout::Mono)
        std::copy (left, left + n, right);
}

juce::Optional<juce::AudioPlayHead::PositionInfo> Vst3Insert::getPosition() const
{
    PositionInfo info;
    info.setBpm (ctx_.bpm);
    info.setTimeInSamples (ctx_.positionSamples);
    info.setTimeInSeconds (ctx_.sampleRate > 0.0 ? static_cast<double> (ctx_.positionSamples) / ctx_.sampleRate : 0.0);
    info.setPpqPosition (ctx_.sampleRate > 0.0 ? static_cast<double> (ctx_.positionSamples) / ctx_.sampleRate * ctx_.bpm / 60.0 : 0.0);
    info.setTimeSignature (TimeSignature { 4, 4 });
    info.setIsPlaying (ctx_.playing);
    return info;
}

juce::MemoryBlock Vst3Insert::state() const
{
    juce::MemoryBlock mb;
    instance_->getStateInformation (mb);
    return mb;
}

void Vst3Insert::setState (const juce::MemoryBlock& mb)
{
    if (mb.getSize() > 0)
        instance_->setStateInformation (mb.getData(), static_cast<int> (mb.getSize()));
    latency_.store (instance_->getLatencySamples());
}

void Vst3Insert::audioProcessorParameterChanged (juce::AudioProcessor*, int, float)
{
    changed();
}

void Vst3Insert::audioProcessorChanged (juce::AudioProcessor*, const ChangeDetails& details)
{
    if (details.latencyChanged)
        latency_.store (instance_->getLatencySamples());
    changed();
}

} // namespace jackal
