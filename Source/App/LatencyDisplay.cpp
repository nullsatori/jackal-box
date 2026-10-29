#include "LatencyDisplay.h"

#include "JackalLookAndFeel.h"

namespace jackal
{

LatencyDisplay::LatencyDisplay (AudioEngine& e) : engine (e)
{
    startTimerHz (4);
    timerCallback();
}

void LatencyDisplay::timerCallback()
{
    juce::String newText;
    const auto info = engine.latency();
    isError = engine.lastError().isNotEmpty();

    if (isError)
    {
        newText = engine.lastError();
    }
    else if (! info.deviceOpen || info.sampleRate <= 0.0)
    {
        newText = "No audio device";
    }
    else
    {
        auto ms = [&info] (int samples) { return juce::String (samples / info.sampleRate * 1000.0, 1) + " ms"; };
        newText << "In " << ms (info.inputSamples) << " (" << info.inputSamples << ")"
                << " · Out " << ms (info.outputSamples) << " (" << info.outputSamples << ")"
                << " · Buffer " << info.bufferSamples << " @ " << juce::roundToInt (info.sampleRate)
                << " · Total " << ms (info.inputSamples + info.outputSamples)
                << " · xruns " << (info.xrunCount >= 0 ? juce::String (info.xrunCount) : juce::String ("n/a"));
    }

    if (newText != text)
    {
        text = newText;
        repaint();
    }
}

void LatencyDisplay::paint (juce::Graphics& g)
{
    g.fillAll (palette::background);
    g.setColour (isError ? palette::alarm : palette::text);
    g.setFont (juce::FontOptions (13.0f));
    g.drawText (text, getLocalBounds().reduced (12, 0), juce::Justification::centredLeft, true);
}

} // namespace jackal
