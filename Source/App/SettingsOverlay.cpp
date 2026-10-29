#include "SettingsOverlay.h"

#include "JackalLookAndFeel.h"

namespace jackal
{

SettingsOverlay::SettingsOverlay (AudioEngine& engine, AppSettings& settings)
    : engine_ (engine), settings_ (settings), devices (engine.deviceManager()), monitor (engine, settings)
{
    addAndMakeVisible (devices);
    addAndMakeVisible (monitor);
    midiLabel.setFont (monoFont (12.f));
    addAndMakeVisible (midiLabel);
    addAndMakeVisible (midi);

    midi.onChange = [this] {
        const int i = midi.getSelectedItemIndex() - 1;
        const auto id = i >= 0 && i < midiDevices_.size() ? midiDevices_[i].identifier : juce::String();
        engine_.openMidiInput (id);
        settings_.setMidiInputId (id);
    };
    refreshMidiDevices();
}

void SettingsOverlay::refreshMidiDevices()
{
    midiDevices_ = juce::MidiInput::getAvailableDevices();
    midi.clear (juce::dontSendNotification);
    midi.addItem ("none", 1);
    int selected = 0;
    for (int i = 0; i < midiDevices_.size(); ++i)
    {
        midi.addItem (midiDevices_[i].name, i + 2);
        if (midiDevices_[i].identifier == engine_.midiInputIdentifier())
            selected = i + 1;
    }
    midi.setSelectedItemIndex (selected, juce::dontSendNotification);
}

void SettingsOverlay::paint (juce::Graphics& g)
{
    g.fillAll (palette::panel);
    g.setColour (palette::neonCyan.withAlpha (0.6f));
    g.drawRect (getLocalBounds(), 1);
}

void SettingsOverlay::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto midiRow = r.removeFromBottom (28);
    midiLabel.setBounds (midiRow.removeFromLeft (90));
    midi.setBounds (midiRow);
    r.removeFromBottom (8);
    monitor.setBounds (r.removeFromBottom (170));
    devices.setBounds (r);
}

} // namespace jackal
