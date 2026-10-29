#include "MainComponent.h"

#include "JackalLookAndFeel.h"

namespace jackal
{

MainComponent::MainComponent (AudioEngine& e, AppSettings& s)
    : engine (e),
      settings (s),
      transportBar (e.core(), s),
      latencyDisplay (e),
      deviceSettings (e.deviceManager()),
      monitorPanel (e, s)
{
    addAndMakeVisible (transportBar);
    addAndMakeVisible (latencyDisplay);
    addAndMakeVisible (deviceSettings);
    addAndMakeVisible (monitorPanel);

    commands.registerAllCommandsForTarget (this);
    commands.setFirstCommandTarget (this);

    engine.deviceManager().addChangeListener (this);

    setWantsKeyboardFocus (true);
    setSize (960, 600);
}

MainComponent::~MainComponent()
{
    engine.deviceManager().removeChangeListener (this);
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    const auto state = engine.deviceManager().createStateXml();
    settings.setAudioDeviceState (state.get());
    monitorPanel.refreshChannels();
}

void MainComponent::getAllCommands (juce::Array<juce::CommandID>& ids)
{
    ids.add (togglePlay);
}

void MainComponent::getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& info)
{
    if (id == togglePlay)
    {
        info.setInfo ("Play / Stop", "Start or stop the transport", "Transport", 0);
        info.addDefaultKeypress (juce::KeyPress::spaceKey, juce::ModifierKeys::noModifiers);
    }
}

bool MainComponent::perform (const InvocationInfo& info)
{
    if (info.commandID == togglePlay)
    {
        transportBar.togglePlay();
        return true;
    }
    return false;
}

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (palette::background);
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    transportBar.setBounds (r.removeFromTop (64));
    latencyDisplay.setBounds (r.removeFromTop (28));
    deviceSettings.setBounds (r.removeFromLeft (juce::roundToInt (r.getWidth() * 0.55f)));
    monitorPanel.setBounds (r);
}

} // namespace jackal
