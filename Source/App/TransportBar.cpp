#include "TransportBar.h"

#include <cmath>

namespace jackal
{

TransportBar::TransportBar (ProjectController& controller, EngineCore& c, AppSettings& s)
    : c_ (controller), core (c), settings (s), fillButton (controller), swing (controller, "Swing"), master (controller, "Master"), masterShakal (controller, "Master SHAKAL")
{
    for (auto* b : std::initializer_list<juce::Button*> { &playButton, &recButton, &fillButton, &clickToggle })
    {
        styleButton (*b);
        addAndMakeVisible (*b);
    }
    recButton.setColour (juce::TextButton::buttonOnColourId, palette::rec);
    fillButton.setColour (juce::TextButton::buttonOnColourId, palette::nixie);

    playButton.onClick = [this] { c_.togglePlay(); };
    recButton.onClick = [this] { c_.setRecording (! c_.recording()); };

    for (juce::Label* l : { &position, &patternLetter, &cpu, &swingLabel, static_cast<juce::Label*> (&masterLabel), static_cast<juce::Label*> (&shakalLabel) })
        addAndMakeVisible (*l);
    position.setJustificationType (juce::Justification::centred);
    position.setColour (juce::Label::textColourId, palette::nixie);
    position.setFont (monoFont (24.f, true));
    patternLetter.setJustificationType (juce::Justification::centred);
    patternLetter.setColour (juce::Label::textColourId, palette::nixie);
    patternLetter.setFont (monoFont (24.f, true));
    cpu.setFont (monoFont (12.f));
    cpu.setJustificationType (juce::Justification::centredRight);
    swingLabel.setText ("SWING", juce::dontSendNotification);
    masterLabel.setText ("MASTER", juce::dontSendNotification);
    shakalLabel.setText ("SHAKAL", juce::dontSendNotification);
    masterLabel.onClick = [this] { c_.setTrackPanelTab (3); };
    shakalLabel.onClick = [this] { c_.setTrackPanelTab (3); };
    masterLabel.setTooltip ("click: master tab");
    shakalLabel.setTooltip ("click: master tab");
    for (juce::Label* l : { &swingLabel, static_cast<juce::Label*> (&masterLabel), static_cast<juce::Label*> (&shakalLabel) })
    {
        l->setFont (monoFont (11.f));
        l->setColour (juce::Label::textColourId, palette::dim);
        l->setJustificationType (juce::Justification::centredRight);
    }

    bpm.setRange (kMinBpm, kMaxBpm, 1.0);
    bpm.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 24);
    bpm.setColour (juce::Slider::textBoxTextColourId, palette::nixie);
    bpm.setColour (juce::Slider::trackColourId, palette::nixie.withAlpha (0.5f));
    bpm.setMouseClickGrabsKeyboardFocus (false);
    bpm.setWantsKeyboardFocus (false);
    bpm.onValueChange = [this] { c_.setBpm (bpm.getValue()); };
    addAndMakeVisible (bpm);

    clickToggle.setToggleState (settings.metronomeEnabled(), juce::dontSendNotification);
    clickToggle.onClick = [this] {
        const bool on = clickToggle.getToggleState();
        core.params().metronomeEnabled.store (on);
        settings.setMetronomeEnabled (on);
    };
    clickGain.setRange (0.0, 1.0, 0.01);
    clickGain.setValue (settings.metronomeGain(), juce::dontSendNotification);
    clickGain.setMouseClickGrabsKeyboardFocus (false);
    clickGain.onValueChange = [this] {
        const auto g = static_cast<float> (clickGain.getValue());
        core.params().metronomeGain.store (g);
        settings.setMetronomeGain (g);
    };
    addAndMakeVisible (clickGain);
    core.params().metronomeEnabled.store (clickToggle.getToggleState());
    core.params().metronomeGain.store (static_cast<float> (clickGain.getValue()));

    swing.setRange (kMinSwing, kMaxSwing, 1.0);
    swing.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    swing.setDoubleClickReturnValue (true, 50.0);
    swing.onValueChange = [this] { c_.setSwing (juce::roundToInt (swing.getValue())); };
    addAndMakeVisible (swing);

    master.setRange (-24.0, 6.0, 0.1);
    master.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    master.setDoubleClickReturnValue (true, -6.0);
    master.onValueChange = [this] { c_.setMasterGainDb (static_cast<float> (master.getValue())); };
    addAndMakeVisible (master);

    masterShakal.setRange (0.0, 1.0, 0.0);
    masterShakal.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    masterShakal.setDoubleClickReturnValue (true, 0.0);
    masterShakal.onValueChange = [this] { c_.setShakal (kMasterChain, static_cast<float> (masterShakal.getValue())); };
    addAndMakeVisible (masterShakal);

    startTimerHz (30);
}

void TransportBar::timerCallback()
{
    const auto s = c_.transport();

    if (s.playing != shownPlaying)
    {
        shownPlaying = s.playing;
        playButton.setButtonText (s.playing ? "STOP" : "PLAY");
        playButton.setToggleState (s.playing, juce::dontSendNotification);
    }
    recButton.setToggleState (c_.recording(), juce::dontSendNotification);
    fillButton.setToggleState (c_.fillHeld(), juce::dontSendNotification);

    int bar = 1, beat = 1;
    if (s.playing)
    {
        const auto beats = static_cast<long> (std::floor (s.positionBeats));
        bar = static_cast<int> (beats / kBeatsPerBar) + 1;
        beat = static_cast<int> (beats % kBeatsPerBar) + 1;
    }
    position.setText (juce::String (bar) + "." + juce::String (beat), juce::dontSendNotification);

    const int pat = s.playing ? c_.snapshot().currentPattern : c_.editPatternIndex();
    patternLetter.setText (juce::String::charToString (static_cast<juce::juce_wchar> ('A' + juce::jlimit (0, 7, pat))), juce::dontSendNotification);

    const float load = c_.cpuLoad() * 100.f;
    cpu.setText ("CPU " + juce::String (juce::roundToInt (load)) + "%", juce::dontSendNotification);
    cpu.setColour (juce::Label::textColourId, load > 90.f ? palette::alarm : (load > 70.f ? palette::nixie : palette::dim));

    if (c_.version() != shownVersion_)
    {
        shownVersion_ = c_.version();
        if (! bpm.isMouseButtonDown())
            bpm.setValue (c_.project().bpm, juce::dontSendNotification);
        if (! swing.isMouseButtonDown())
            swing.setValue (c_.editPattern().swing, juce::dontSendNotification);
        if (! master.isMouseButtonDown())
            master.setValue (c_.project().masterGainDb, juce::dontSendNotification);
        if (! masterShakal.isMouseButtonDown())
            masterShakal.setValue (c_.project().masterShakal, juce::dontSendNotification);
        swing.setTooltip ("Swing " + juce::String (c_.editPattern().swing) + " (pattern)");
        master.setTooltip ("Master " + juce::String (c_.project().masterGainDb, 1) + " dB");
    }
}

void TransportBar::paint (juce::Graphics& g)
{
    g.fillAll (palette::panel);
    g.setColour (palette::neonCyan.withAlpha (0.4f));
    g.drawHorizontalLine (getHeight() - 1, 0.f, static_cast<float> (getWidth()));
    g.setColour (palette::neonCyan);
    g.setFont (monoFont (15.f, true));
    g.drawText ("JACKAL BOX", getLocalBounds().removeFromRight (120).reduced (8, 0), juce::Justification::centredRight, false);
}

void TransportBar::resized()
{
    auto r = getLocalBounds().reduced (10, 12);
    r.removeFromRight (120);
    playButton.setBounds (r.removeFromLeft (72));
    r.removeFromLeft (6);
    recButton.setBounds (r.removeFromLeft (52));
    r.removeFromLeft (6);
    fillButton.setBounds (r.removeFromLeft (52));
    r.removeFromLeft (12);
    position.setBounds (r.removeFromLeft (70));
    patternLetter.setBounds (r.removeFromLeft (30));
    r.removeFromLeft (12);
    bpm.setBounds (r.removeFromLeft (220));
    r.removeFromLeft (12);
    clickToggle.setBounds (r.removeFromLeft (62));
    r.removeFromLeft (4);
    clickGain.setBounds (r.removeFromLeft (80));
    r.removeFromLeft (8);
    swingLabel.setBounds (r.removeFromLeft (48));
    swing.setBounds (r.removeFromLeft (40));
    masterLabel.setBounds (r.removeFromLeft (56));
    master.setBounds (r.removeFromLeft (40));
    shakalLabel.setBounds (r.removeFromLeft (56));
    masterShakal.setBounds (r.removeFromLeft (40));
    cpu.setBounds (r);
}

} // namespace jackal
