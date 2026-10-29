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

    for (auto* l : { &timingTitle, &foldersTitle })
    {
        l->setFont (monoFont (12.f, true));
        l->setColour (juce::Label::textColourId, palette::neonCyan);
        addAndMakeVisible (*l);
    }
    for (auto* l : { &driverLine, &manualLabel, &measuredLine })
    {
        l->setFont (monoFont (12.f));
        addAndMakeVisible (*l);
    }
    manual.setRange (-50.0, 50.0, 0.1);
    manual.setTextValueSuffix (" ms");
    manual.setDoubleClickReturnValue (true, 0.0);
    manual.setValue (settings_.loopManualMs(), juce::dontSendNotification);
    manual.onValueChange = [this] {
        settings_.setLoopManualMs (manual.getValue());
        engine_.updateLoopComp (settings_);
    };
    addAndMakeVisible (manual);

    cal.onClick = [this] { calibrating() ? cancelCalibration() : startCalibration(); };
    addAndMakeVisible (cal);
    useMeasured.setToggleState (settings_.loopUseMeasured(), juce::dontSendNotification);
    useMeasured.onClick = [this] {
        settings_.setLoopUseMeasured (useMeasured.getToggleState());
        engine_.updateLoopComp (settings_);
        refreshTiming();
    };
    addAndMakeVisible (useMeasured);

    addAndMakeVisible (folders);
    addAndMakeVisible (addFolder);
    addAndMakeVisible (removeFolder);
    addFolder.onClick = [this] {
        chooser_ = std::make_unique<juce::FileChooser> ("Add a VST3 folder", juce::File());
        chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                               [this] (const juce::FileChooser& fc) {
                                   if (! fc.getResult().isDirectory())
                                       return;
                                   auto p = settings_.vst3Paths();
                                   p.addIfNotAlreadyThere (fc.getResult().getFullPathName());
                                   settings_.setVst3Paths (p);
                                   refreshTiming();
                               });
    };
    removeFolder.onClick = [this] {
        auto p = settings_.vst3Paths();
        p.removeString (folders.getText());
        settings_.setVst3Paths (p);
        refreshTiming();
    };

    refreshTiming();
    startTimerHz (10);
}

bool SettingsOverlay::calibrating() const
{
    return const_cast<AudioEngine&> (engine_).core().loopSnapshot().cal == CalState::Running;
}

void SettingsOverlay::startCalibration()
{
    engine_.core().commands().push ({ CommandType::StartCalibration, 0.f, 0 });
    lastCal_ = CalState::Running;
    cal.setButtonText (juce::String (juce::CharPointer_UTF8 ("CAL\xe2\x80\xa6")));
}

void SettingsOverlay::cancelCalibration()
{
    engine_.core().commands().push ({ CommandType::CancelCalibration, 0.f, 0 });
}

void SettingsOverlay::timerCallback()
{
    const auto snap = engine_.core().loopSnapshot();
    if (snap.cal != lastCal_)
    {
        if (lastCal_ == CalState::Running && snap.cal == CalState::Done && snap.calSamples >= 0)
        {
            settings_.setLoopCal (engine_.deviceName(), snap.calSamples);
            engine_.updateLoopComp (settings_);
        }
        lastCal_ = snap.cal;
        cal.setButtonText (snap.cal == CalState::Running ? juce::String (juce::CharPointer_UTF8 ("CAL\xe2\x80\xa6")) : juce::String ("CAL"));
        refreshTiming();
        if (snap.cal == CalState::Failed)
        {
            measuredLine.setColour (juce::Label::textColourId, palette::alarm);
            measuredLine.setText ("no signal: connect output to input", juce::dontSendNotification);
        }
    }
}

void SettingsOverlay::refreshTiming()
{
    const auto info = engine_.latency();
    const double sr = info.sampleRate > 0.0 ? info.sampleRate : 48000.0;
    auto ms = [sr] (int smp) { return juce::String (smp / sr * 1000.0, 1) + " ms"; };
    driverLine.setText ("driver in " + ms (info.inputSamples) + " + out " + ms (info.outputSamples) + " = "
                            + juce::String (info.inputSamples + info.outputSamples) + " smp",
                        juce::dontSendNotification);
    const int measured = settings_.loopCal (engine_.deviceName());
    measuredLine.setColour (juce::Label::textColourId, palette::text);
    measuredLine.setText (measured >= 0 ? "measured " + ms (measured) + " (" + juce::String (measured) + ")" : juce::String ("not measured"),
                          juce::dontSendNotification);
    useMeasured.setEnabled (measured >= 0);

    folders.clear (juce::dontSendNotification);
    const auto paths = settings_.vst3Paths();
    for (int i = 0; i < paths.size(); ++i)
        folders.addItem (paths[i], i + 1);
    folders.setTextWhenNothingSelected (paths.isEmpty() ? "default folders only" : juce::String (paths.size()) + " extra folders");
    removeFolder.setEnabled (! paths.isEmpty());
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

    auto fold = r.removeFromBottom (48);
    foldersTitle.setBounds (fold.removeFromTop (20));
    auto frow = fold.removeFromTop (24);
    removeFolder.setBounds (frow.removeFromRight (70));
    frow.removeFromRight (4);
    addFolder.setBounds (frow.removeFromRight (60));
    frow.removeFromRight (4);
    folders.setBounds (frow);
    r.removeFromBottom (8);

    auto t = r.removeFromBottom (96);
    timingTitle.setBounds (t.removeFromTop (20));
    driverLine.setBounds (t.removeFromTop (20));
    auto mrow = t.removeFromTop (24);
    manualLabel.setBounds (mrow.removeFromLeft (64));
    manual.setBounds (mrow);
    auto crow = t.removeFromTop (26);
    cal.setBounds (crow.removeFromLeft (60).reduced (0, 1));
    crow.removeFromLeft (8);
    useMeasured.setBounds (crow.removeFromRight (130));
    measuredLine.setBounds (crow);
    r.removeFromBottom (8);

    monitor.setBounds (r.removeFromBottom (170));
    devices.setBounds (r);
}

} // namespace jackal
