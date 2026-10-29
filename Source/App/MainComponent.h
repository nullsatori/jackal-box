#pragma once

#include "AppSettings.h"
#include "AudioEngine.h"
#include "DeviceSettingsPanel.h"
#include "LatencyDisplay.h"
#include "MonitorPanel.h"
#include "TransportBar.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace jackal
{

class MainComponent : public juce::Component,
                      public juce::ApplicationCommandTarget,
                      private juce::ChangeListener
{
public:
    enum CommandIDs { togglePlay = 0x2001 };

    MainComponent (AudioEngine& engine, AppSettings& settings);
    ~MainComponent() override;

    juce::ApplicationCommandManager& commandManager() noexcept { return commands; }

    void paint (juce::Graphics& g) override;
    void resized() override;

    juce::ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
    void getAllCommands (juce::Array<juce::CommandID>& ids) override;
    void getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& info) override;
    bool perform (const InvocationInfo& info) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;

    AudioEngine& engine;
    AppSettings& settings;
    juce::ApplicationCommandManager commands;

    TransportBar transportBar;
    LatencyDisplay latencyDisplay;
    DeviceSettingsPanel deviceSettings;
    MonitorPanel monitorPanel;
};

} // namespace jackal
