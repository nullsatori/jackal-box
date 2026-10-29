#include "SettingsOverlay.h"

#include "JackalLookAndFeel.h"

namespace jackal
{

SettingsOverlay::SettingsOverlay (AudioEngine& engine, AppSettings& settings, MidiMapper& mapper)
    : engine_ (engine), settings_ (settings), devices (engine.deviceManager()), monitor (engine, settings), mapper_ (mapper)
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

    // ---- stage 5: pages and the MIDI page
    for (auto* b : { &audioTab, &midiTab, &learn, &clearAll })
    {
        b->setMouseClickGrabsKeyboardFocus (false);
        b->setWantsKeyboardFocus (false);
        addAndMakeVisible (*b);
    }
    audioTab.setColour (juce::TextButton::buttonOnColourId, palette::neonCyan);
    midiTab.setColour (juce::TextButton::buttonOnColourId, palette::neonCyan);
    audioTab.onClick = [this] { showPage (0); };
    midiTab.onClick = [this] { showPage (1); };

    midiTitle.setFont (monoFont (12.f, true));
    midiTitle.setColour (juce::Label::textColourId, palette::neonCyan);
    bindingsTitle.setFont (monoFont (12.f, true));
    bindingsTitle.setColour (juce::Label::textColourId, palette::neonCyan);
    for (auto* l : { &midiTitle, &channelLabel, &bindingsTitle, &hitLabel, &midiHint })
    {
        if (l != &midiTitle && l != &bindingsTitle)
            l->setFont (monoFont (12.f));
        addAndMakeVisible (*l);
    }
    midiHint.setColour (juce::Label::textColourId, palette::dim);
    midiHint.setFont (monoFont (11.f));
    midiHint.setText ("right click any knob or button: MIDI learn / clear. Ctrl+L: learn", juce::dontSendNotification);

    channel.addItem ("OMNI", 1);
    for (int ch = 1; ch <= 16; ++ch)
        channel.addItem (juce::String (ch), ch + 1);
    channel.onChange = [this] { mapper_.setChannel (channel.getSelectedItemIndex()); };
    addAndMakeVisible (channel);

    defaultNotes.setButtonText (juce::String (juce::CharPointer_UTF8 ("NOTES C..G â TRACKS")));
    defaultNotes.onClick = [this] { mapper_.setDefaultNotes (defaultNotes.getToggleState()); };
    addAndMakeVisible (defaultNotes);

    learn.setColour (juce::TextButton::textColourOffId, palette::magenta);
    learn.onClick = [this] { mapper_.beginLearn(); };
    clearAll.onClick = [this] {
        const auto now = juce::Time::getMillisecondCounter();
        if (clearArmedMs_ != 0 && now - clearArmedMs_ < 3000)
        {
            clearArmedMs_ = 0;
            mapper_.clearAll();
            clearAll.setButtonText ("CLEAR ALL");
        }
        else
        {
            clearArmedMs_ = now;
            clearAll.setButtonText ("SURE?");
        }
    };
    for (int i = 0; i < 8; ++i)
    {
        auto& b = hitButtons[static_cast<std::size_t> (i)];
        b.setButtonText (juce::String (i + 1));
        b.setMouseClickGrabsKeyboardFocus (false);
        b.setWantsKeyboardFocus (false);
        b.setTooltip ("learn a pad of your controller for TRACK HIT " + juce::String (i + 1));
        b.onClick = [this, i, &b] { mapper_.learnTarget (MidiTarget::ofAction (ActionId::TrackHit, i), &b); };
        addAndMakeVisible (b);
    }
    bindings.setRowHeight (20);
    bindings.setColour (juce::ListBox::backgroundColourId, palette::background);
    bindings.setOutlineThickness (1);
    bindings.setColour (juce::ListBox::outlineColourId, palette::gridLine);
    addAndMakeVisible (bindings);

    refreshMidiMap();
    showPage (0);

    refreshTiming();
    startTimerHz (10);
}

void SettingsOverlay::showPage (int page)
{
    page_ = page == 1 ? 1 : 0;
    audioTab.setToggleState (page_ == 0, juce::dontSendNotification);
    midiTab.setToggleState (page_ == 1, juce::dontSendNotification);
    const bool a = page_ == 0;
    for (juce::Component* c : std::initializer_list<juce::Component*> { &devices, &monitor, &timingTitle, &driverLine, &manualLabel, &measuredLine,
                                                                          &manual, &cal, &useMeasured, &foldersTitle, &folders, &addFolder,
                                                                          &removeFolder })
        c->setVisible (a);
    std::vector<juce::Component*> midiPage { &midiLabel, &midi, &midiTitle, &channelLabel, &bindingsTitle, &hitLabel, &midiHint, &channel,
                                             &defaultNotes, &learn, &clearAll, &bindings };
    for (auto& b : hitButtons)
        midiPage.push_back (&b);
    for (auto* c : midiPage)
        c->setVisible (! a);
    if (! a)
        refreshMidiMap();
}

void SettingsOverlay::refreshMidiMap()
{
    shownMapVersion_ = mapper_.version();
    channel.setSelectedItemIndex (mapper_.map().channel, juce::dontSendNotification);
    defaultNotes.setToggleState (mapper_.map().defaultNotes, juce::dontSendNotification);
    bindings.updateContent();
    bindings.repaint();
}

int SettingsOverlay::getNumRows()
{
    return static_cast<int> (mapper_.map().bindings.size());
}

void SettingsOverlay::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool)
{
    const auto& list = mapper_.map().bindings;
    if (row < 0 || row >= static_cast<int> (list.size()))
        return;
    g.setFont (monoFont (12.f));
    g.setColour (palette::text);
    g.drawText (MidiMapper::describe (list[static_cast<std::size_t> (row)]), 6, 0, width - 34, height, juce::Justification::centredLeft, true);
    g.setColour (palette::alarm.withAlpha (0.8f));
    g.drawText ("x", width - 24, 0, 18, height, juce::Justification::centred, false);
}

void SettingsOverlay::listBoxItemClicked (int row, const juce::MouseEvent& e)
{
    if (e.x >= bindings.getVisibleRowWidth() - 28)
        mapper_.removeBinding (row);
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
    if (mapper_.version() != shownMapVersion_)
        refreshMidiMap();
    if (clearArmedMs_ != 0 && juce::Time::getMillisecondCounter() - clearArmedMs_ >= 3000)
    {
        clearArmedMs_ = 0;
        clearAll.setButtonText ("CLEAR ALL");
    }

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
    auto tabs = r.removeFromTop (24);
    audioTab.setBounds (tabs.removeFromLeft (72));
    tabs.removeFromLeft (4);
    midiTab.setBounds (tabs.removeFromLeft (72));
    r.removeFromTop (8);

    // ---- MIDI page
    {
        auto m = r;
        midiTitle.setBounds (m.removeFromTop (20));
        auto dev = m.removeFromTop (26);
        midiLabel.setBounds (dev.removeFromLeft (90));
        midi.setBounds (dev);
        m.removeFromTop (6);
        auto ch = m.removeFromTop (26);
        channelLabel.setBounds (ch.removeFromLeft (90));
        channel.setBounds (ch.removeFromLeft (90));
        ch.removeFromLeft (12);
        defaultNotes.setBounds (ch.removeFromLeft (200));
        m.removeFromTop (6);
        auto lr = m.removeFromTop (26);
        learn.setBounds (lr.removeFromLeft (80));
        lr.removeFromLeft (12);
        midiHint.setBounds (lr);
        m.removeFromTop (6);
        auto hit = m.removeFromTop (24);
        hitLabel.setBounds (hit.removeFromLeft (100));
        for (auto& b : hitButtons)
        {
            b.setBounds (hit.removeFromLeft (28));
            hit.removeFromLeft (4);
        }
        m.removeFromTop (10);
        auto bt = m.removeFromTop (22);
        clearAll.setBounds (bt.removeFromRight (90));
        bindingsTitle.setBounds (bt);
        m.removeFromTop (4);
        bindings.setBounds (m);
    }

    // ---- AUDIO page
    r.removeFromBottom (0);
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
