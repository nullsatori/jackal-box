#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

namespace jackal
{

// Floating editor of one insert slot. Close button and Esc hide it; it dies with the plugin.
class PluginWindow : public juce::DocumentWindow
{
public:
    PluginWindow (juce::AudioProcessor& processor, const juce::String& title);
    ~PluginWindow() override;

    void closeButtonPressed() override { setVisible (false); }
    bool keyPressed (const juce::KeyPress& key) override;

private:
    juce::AudioProcessor& processor_;
};

} // namespace jackal
