#include "DeviceSettingsPanel.h"

#include "JackalLookAndFeel.h"

namespace jackal
{

DeviceSettingsPanel::DeviceSettingsPanel (juce::AudioDeviceManager& deviceManager)
    : selector (deviceManager,
                0, 2,    // audio inputs min/max
                2, 2,    // audio outputs min/max
                false,   // midi in
                false,   // midi out
                false,   // stereo pairs
                false)   // advanced options hidden behind a button
{
    addAndMakeVisible (selector);
}

void DeviceSettingsPanel::paint (juce::Graphics& g)
{
    g.fillAll (palette::panel);
}

void DeviceSettingsPanel::resized()
{
    selector.setBounds (getLocalBounds().reduced (8));
}

} // namespace jackal
