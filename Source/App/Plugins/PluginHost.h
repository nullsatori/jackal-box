#pragma once

#include "../AppSettings.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <functional>
#include <memory>

namespace jackal
{

// VST3 only: format manager, known plugin list and its cache (%APPDATA%\JackalBox\plugins.xml).
// No scan at startup; the browser starts one on request.
class PluginHost
{
public:
    explicit PluginHost (AppSettings& settings);

    juce::KnownPluginList& list() noexcept { return list_; }
    const juce::KnownPluginList& list() const noexcept { return list_; }
    juce::AudioPluginFormat* vst3() const;

    std::unique_ptr<juce::AudioPluginInstance> create (const juce::PluginDescription&, double sampleRate, int blockSize, juce::String& error);
    std::unique_ptr<juce::PluginDescription> find (const juce::String& identifier) const;

    juce::StringArray searchPaths() const;   // default VST3 folders + AppSettings::vst3Paths
    void saveCache();
    static juce::File cacheFile();

private:
    AppSettings& settings_;
    juce::AudioPluginFormatManager formats_;
    juce::KnownPluginList list_;
};

} // namespace jackal
