#pragma once

#include "AppSettings.h"
#include "AudioEngine.h"
#include "GlitchPadBar.h"
#include "PatternStrip.h"
#include "ProjectController.h"
#include "SettingsOverlay.h"
#include "StatusBar.h"
#include "StepGrid.h"
#include "TrackHeaders.h"
#include "TrackPanel.h"
#include "TransportBar.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace jackal
{

class MainComponent : public juce::Component,
                      public juce::ApplicationCommandTarget,
                      public juce::FileDragAndDropTarget,
                      private juce::ChangeListener
{
public:
    MainComponent (AudioEngine& engine, AppSettings& settings, ProjectController& controller);
    ~MainComponent() override;

    juce::ApplicationCommandManager& commandManager() noexcept { return commands; }

    void paint (juce::Graphics& g) override;
    void resized() override;
    bool keyStateChanged (bool isKeyDown) override;
    bool keyPressed (const juce::KeyPress& key) override;

    juce::ApplicationCommandTarget* getNextCommandTarget() override { return nullptr; }
    void getAllCommands (juce::Array<juce::CommandID>& ids) override;
    void getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& info) override;
    bool perform (const InvocationInfo& info) override;

    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

private:
    void changeListenerCallback (juce::ChangeBroadcaster* source) override;
    void toggleSettings();
    void openDialog();
    void saveAsDialog();

    AudioEngine& engine;
    AppSettings& settings;
    ProjectController& controller;
    juce::ApplicationCommandManager commands;

    TransportBar transportBar;
    PatternStrip patternStrip;
    TrackHeaders trackHeaders;
    StepGrid stepGrid;
    TrackPanel trackPanel;
    GlitchPadBar padBar;
    StatusBar statusBar;
    SettingsOverlay settingsOverlay;

    std::unique_ptr<juce::FileChooser> chooser;
    bool fillKeyDown = false;
    std::array<bool, kNumPads> padKeyDown {};
};

} // namespace jackal
