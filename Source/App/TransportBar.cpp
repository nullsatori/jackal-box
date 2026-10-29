#include "TransportBar.h"

#include "JackalLookAndFeel.h"

#include <cmath>

namespace jackal
{

TransportBar::TransportBar (EngineCore& c, AppSettings& s)
    : core (c), settings (s)
{
    for (auto* comp : std::initializer_list<juce::Component*> { &playButton, &bpmLabel, &bpmSlider, &clickToggle, &clickGain, &position })
    {
        comp->setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (comp);
    }

    playButton.setWantsKeyboardFocus (false);
    clickToggle.setWantsKeyboardFocus (false);
    playButton.onClick = [this] { togglePlay(); };

    bpmSlider.setRange (kMinBpm, kMaxBpm, 1.0);
    bpmSlider.setValue (settings.bpm(), juce::dontSendNotification);
    bpmSlider.setTextBoxStyle (juce::Slider::TextBoxRight, false, 56, 24);
    bpmSlider.onValueChange = [this] { pushBpm (bpmSlider.getValue()); };

    clickToggle.setToggleState (settings.metronomeEnabled(), juce::dontSendNotification);
    clickToggle.onClick = [this] {
        const bool on = clickToggle.getToggleState();
        core.params().metronomeEnabled.store (on);
        settings.setMetronomeEnabled (on);
    };

    clickGain.setRange (0.0, 1.0, 0.01);
    clickGain.setValue (settings.metronomeGain(), juce::dontSendNotification);
    clickGain.onValueChange = [this] {
        const auto g = static_cast<float> (clickGain.getValue());
        core.params().metronomeGain.store (g);
        settings.setMetronomeGain (g);
    };

    position.setJustificationType (juce::Justification::centred);
    position.setColour (juce::Label::textColourId, palette::nixie);
    position.setFont (juce::FontOptions (28.0f, juce::Font::bold));

    // push restored state into the engine
    core.params().metronomeEnabled.store (clickToggle.getToggleState());
    core.params().metronomeGain.store (static_cast<float> (clickGain.getValue()));
    core.commands().push ({ CommandType::SetBpm, static_cast<float> (bpmSlider.getValue()), 0 });

    startTimerHz (30);
}

void TransportBar::pushBpm (double bpm)
{
    core.commands().push ({ CommandType::SetBpm, static_cast<float> (bpm), 0 });
    settings.setBpm (bpm);
}

void TransportBar::togglePlay()
{
    const bool playing = core.transportSnapshot().playing;
    core.commands().push ({ playing ? CommandType::Stop : CommandType::Play, 0.f, 0 });
}

void TransportBar::timerCallback()
{
    const auto s = core.transportSnapshot();

    if (s.playing != shownPlaying)
    {
        shownPlaying = s.playing;
        playButton.setButtonText (s.playing ? "Stop" : "Play");
        playButton.setColour (juce::TextButton::buttonColourId, s.playing ? palette::nixie.withAlpha (0.8f) : palette::panel);
    }

    int bar = 1, beat = 1;
    if (s.playing)
    {
        const auto beats = static_cast<long> (std::floor (s.positionBeats));
        bar = static_cast<int> (beats / kBeatsPerBar) + 1;
        beat = static_cast<int> (beats % kBeatsPerBar) + 1;
    }
    position.setText (juce::String (bar) + "." + juce::String (beat), juce::dontSendNotification);
}

void TransportBar::paint (juce::Graphics& g)
{
    g.fillAll (palette::panel);
    g.setColour (palette::neonCyan.withAlpha (0.4f));
    g.drawHorizontalLine (getHeight() - 1, 0.f, static_cast<float> (getWidth()));
}

void TransportBar::resized()
{
    auto r = getLocalBounds().reduced (12, 12);
    playButton.setBounds (r.removeFromLeft (96));
    r.removeFromLeft (16);
    position.setBounds (r.removeFromLeft (96));
    r.removeFromLeft (16);
    bpmLabel.setBounds (r.removeFromLeft (40));
    bpmSlider.setBounds (r.removeFromLeft (260));
    r.removeFromLeft (24);
    clickToggle.setBounds (r.removeFromLeft (72));
    clickGain.setBounds (r.removeFromLeft (160));
}

} // namespace jackal
