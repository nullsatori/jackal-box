#include "MonitorPanel.h"

#include "JackalLookAndFeel.h"

#include <cmath>

namespace jackal
{

namespace
{
    constexpr float kFloorDb = -60.f;

    float toDb (float linear)
    {
        return linear > 0.f ? juce::jmax (kFloorDb, 20.f * std::log10 (linear)) : kFloorDb;
    }
} // namespace

MonitorPanel::MonitorPanel (AudioEngine& e, AppSettings& s)
    : engine (e), settings (s)
{
    for (auto* comp : std::initializer_list<juce::Component*> { &monitorToggle, &gainLabel, &gain, &channelLabel, &channel, &status })
    {
        comp->setMouseClickGrabsKeyboardFocus (false);
        addAndMakeVisible (comp);
    }
    monitorToggle.setWantsKeyboardFocus (false);
    channel.setWantsKeyboardFocus (false);

    auto& params = engine.core().params();

    monitorToggle.setToggleState (settings.monitorEnabled(), juce::dontSendNotification);
    monitorToggle.onClick = [this, &params] {
        const bool on = monitorToggle.getToggleState();
        params.monitorEnabled.store (on);
        settings.setMonitorEnabled (on);
    };

    gain.setRange (-60.0, 12.0, 0.1);
    gain.setValue (settings.monitorGainDb(), juce::dontSendNotification);
    gain.setTextBoxStyle (juce::Slider::TextBoxRight, false, 64, 24);
    gain.textFromValueFunction = [] (double v) { return v <= -60.0 ? juce::String ("-inf") : juce::String (v, 1) + " dB"; };
    gain.valueFromTextFunction = [] (const juce::String& t) { return t.trim().startsWith ("-inf") ? -60.0 : t.getDoubleValue(); };
    gain.updateText();
    gain.onValueChange = [this, &params] {
        const auto db = static_cast<float> (gain.getValue());
        params.monitorGainDb.store (db);
        settings.setMonitorGainDb (db);
    };

    channel.onChange = [this] {
        const int index = channel.getSelectedItemIndex();
        if (index >= 0)
        {
            engine.core().commands().push ({ CommandType::SetInputChannel, 0.f, index });
            settings.setInputChannel (index);
        }
    };

    status.setColour (juce::Label::textColourId, juce::Colours::yellow);

    params.monitorEnabled.store (monitorToggle.getToggleState());
    params.monitorGainDb.store (static_cast<float> (gain.getValue()));

    refreshChannels();
    startTimerHz (30);
}

void MonitorPanel::refreshChannels()
{
    const auto names = engine.activeInputChannelNames();
    channel.clear (juce::dontSendNotification);

    for (int i = 0; i < names.size(); ++i)
        channel.addItem (names[i], i + 1);

    int wanted = settings.inputChannel();
    if (wanted < 0 || wanted >= names.size())
        wanted = 0;

    if (names.isEmpty())
    {
        status.setText (juce::String::fromUTF8 ("Вход не открыт"), juce::dontSendNotification);
        engine.core().commands().push ({ CommandType::SetInputChannel, 0.f, -1 });
    }
    else
    {
        status.setText ({}, juce::dontSendNotification);
        channel.setSelectedItemIndex (wanted, juce::dontSendNotification);
        engine.core().commands().push ({ CommandType::SetInputChannel, 0.f, wanted });
    }
}

void MonitorPanel::timerCallback()
{
    const auto& meter = engine.core().inputMeter();
    const float peakDb = toDb (meter.peak());
    const float rmsDb = toDb (meter.rms());

    shownPeakDb = juce::jmax (peakDb, shownPeakDb - 1.5f); // ~45 dB/s fall
    shownRmsDb = juce::jmax (rmsDb, shownRmsDb - 1.5f);

    const auto now = juce::Time::getMillisecondCounter();
    if (peakDb >= holdPeakDb)
    {
        holdPeakDb = peakDb;
        holdSince = now;
    }
    else if (now - holdSince > 1000)
    {
        holdPeakDb = juce::jmax (peakDb, holdPeakDb - 1.5f);
    }

    repaint (meterArea);
}

void MonitorPanel::paint (juce::Graphics& g)
{
    g.fillAll (palette::panel);

    if (meterArea.isEmpty())
        return;

    const auto area = meterArea.toFloat();
    g.setColour (palette::background);
    g.fillRect (area);

    auto yFor = [&area] (float db) {
        const float norm = (db - kFloorDb) / (0.f - kFloorDb);
        return area.getBottom() - juce::jlimit (0.f, 1.f, norm) * area.getHeight();
    };

    // red zone above -1 dB
    g.setColour (palette::alarm.withAlpha (0.25f));
    g.fillRect (area.withTop (yFor (0.f)).withBottom (yFor (-1.f)));

    const float half = area.getWidth() * 0.5f;
    g.setColour (palette::neonCyan.withAlpha (0.55f));
    g.fillRect (juce::Rectangle<float> (area.getX(), yFor (shownRmsDb), half - 2.f, area.getBottom() - yFor (shownRmsDb)));

    g.setColour (shownPeakDb > -1.f ? palette::alarm : palette::neonCyan);
    g.fillRect (juce::Rectangle<float> (area.getX() + half, yFor (shownPeakDb), half, area.getBottom() - yFor (shownPeakDb)));

    g.setColour (holdPeakDb > -1.f ? palette::alarm : palette::nixie);
    g.fillRect (juce::Rectangle<float> (area.getX(), yFor (holdPeakDb) - 1.f, area.getWidth(), 2.f));

    // scale -60..0
    g.setColour (palette::text.withAlpha (0.6f));
    g.setFont (juce::FontOptions (11.0f));
    for (int db = 0; db >= -60; db -= 12)
    {
        const float y = yFor (static_cast<float> (db));
        g.drawText (juce::String (db), juce::Rectangle<float> (area.getRight() + 4.f, y - 6.f, 32.f, 12.f), juce::Justification::centredLeft);
    }
}

void MonitorPanel::resized()
{
    auto r = getLocalBounds().reduced (12);
    meterArea = r.removeFromRight (48).withTrimmedRight (36).withTrimmedTop (4);
    r.removeFromRight (24);

    monitorToggle.setBounds (r.removeFromTop (28));
    r.removeFromTop (8);
    auto row = r.removeFromTop (28);
    gainLabel.setBounds (row.removeFromLeft (48));
    gain.setBounds (row);
    r.removeFromTop (8);
    row = r.removeFromTop (28);
    channelLabel.setBounds (row.removeFromLeft (48));
    channel.setBounds (row);
    r.removeFromTop (8);
    status.setBounds (r.removeFromTop (24));
}

} // namespace jackal
