#include "StatusBar.h"

#include "JackalLookAndFeel.h"
#include "Widgets.h"

namespace jackal
{

StatusBar::StatusBar (ProjectController& controller, AudioEngine& engine)
    : c_ (controller), engine_ (engine), latency (engine)
{
    addAndMakeVisible (latency);
    styleButton (settingsButton);
    settingsButton.onClick = [this] { if (onSettings) onSettings(); };
    addAndMakeVisible (settingsButton);
    startTimerHz (30);
}

void StatusBar::timerCallback()
{
    auto decay = [] (float shown, float now) { return now > shown ? now : shown * 0.85f; };
    inLevel_ = decay (inLevel_, engine_.core().inputMeter().peak());
    outLevel_ = decay (outLevel_, engine_.core().outputMeter().peak());
    repaint (meterArea_);
    repaint (infoArea_);
}

void StatusBar::paint (juce::Graphics& g)
{
    g.fillAll (palette::panel);
    g.setColour (palette::gridLine);
    g.drawHorizontalLine (0, 0.f, static_cast<float> (getWidth()));

    // two tiny meters: input and output
    auto m = meterArea_.toFloat().reduced (0.f, 7.f);
    for (int i = 0; i < 2; ++i)
    {
        auto bar = m.removeFromTop (m.getHeight() / (2 - i)).reduced (0.f, 1.f);
        const float level = i == 0 ? inLevel_ : outLevel_;
        const float db = level > 0.f ? 20.f * std::log10 (level) : -60.f;
        const float f = juce::jlimit (0.f, 1.f, (db + 60.f) / 60.f);
        g.setColour (palette::ledOff);
        g.fillRect (bar);
        g.setColour (level >= 1.f ? palette::alarm : palette::neonCyan);
        g.fillRect (bar.withWidth (bar.getWidth() * f));
    }

    auto info = infoArea_;
    g.setFont (monoFont (12.f));
    const auto midi = engine_.midiInputName();
    g.setColour (palette::dim);
    g.drawText ("MIDI " + (midi.isEmpty() ? juce::String ("-") : midi), info.removeFromLeft (180), juce::Justification::centredLeft, true);

    g.setColour (palette::text);
    const auto name = c_.projectName() + (c_.isDirty() ? "  *" : "");
    g.drawText (name, info.removeFromLeft (260), juce::Justification::centredLeft, true);

    const auto err = c_.lastError();
    if (err.isNotEmpty())
    {
        g.setColour (juce::Colour (0xFFFFD23F));
        g.drawText (err, info, juce::Justification::centredLeft, true);
    }
}

void StatusBar::resized()
{
    auto r = getLocalBounds().reduced (6, 0);
    latency.setBounds (r.removeFromLeft (360));
    settingsButton.setBounds (r.removeFromRight (96).reduced (0, 3));
    r.removeFromRight (8);
    meterArea_ = r.removeFromRight (80);
    r.removeFromRight (8);
    infoArea_ = r;
}

} // namespace jackal
