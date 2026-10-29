#pragma once

#include <juce_audio_utils/juce_audio_utils.h>

namespace jackal
{

class DeviceSettingsPanel : public juce::Component
{
public:
    explicit DeviceSettingsPanel (juce::AudioDeviceManager& deviceManager);
    void paint (juce::Graphics& g) override;
    void resized() override;

private:
    juce::AudioDeviceSelectorComponent selector;
};

} // namespace jackal
