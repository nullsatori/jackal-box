#include "TrackHeaders.h"

namespace jackal
{

TrackHeaders::Row::Row (ProjectController& controller, int t)
    : c (controller), track (t), volume (controller, "Volume"), shakal (controller, "Shakal")
{
    for (auto* b : { &mute, &solo })
    {
        styleButton (*b);
        b->setClickingTogglesState (false);
        addAndMakeVisible (*b);
    }
    mute.setColour (juce::TextButton::buttonOnColourId, palette::nixie);
    solo.setColour (juce::TextButton::buttonOnColourId, palette::neonCyan);

    mute.onClick = [this] {
        c.beginGesture ("Mute");
        c.editTrack (track, [] (TrackSettings& s) { s.mute = ! s.mute; });
    };
    solo.onClick = [this] {
        c.beginGesture ("Solo");
        c.editTrack (track, [] (TrackSettings& s) { s.solo = ! s.solo; });
    };

    volume.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    volume.setRange (-60.0, 12.0, 0.1);
    volume.setDoubleClickReturnValue (true, 0.0);
    volume.setTooltip ("Volume dB");
    volume.onValueChange = [this] { c.setTrackParam (track, TrackParam::Volume, static_cast<float> (volume.getValue())); };
    addAndMakeVisible (volume);

    shakal.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    shakal.setRange (0.0, 1.0, 0.0);
    shakal.setDoubleClickReturnValue (true, 0.0);
    shakal.setTooltip ("SHAKAL (hold a step and turn: p-lock)");
    shakal.onValueChange = [this] {
        if (! refreshing)
            c.setShakal (track, static_cast<float> (shakal.getValue()));
    };
    addAndMakeVisible (shakal);

    // stage 5: MIDI learn targets (bound to this track, not to "the selected one")
    setMidiTarget (mute, MidiTarget::ofAction (ActionId::Mute, track));
    setMidiTarget (solo, MidiTarget::ofAction (ActionId::Solo, track));
    setMidiTarget (volume, MidiTarget::ofParam (trackParam (track, TrackParam::Volume)));
    setMidiTarget (shakal, MidiTarget::ofParam (trackParam (track, TrackParam::Shakal)));

    refresh();
}

juce::String TrackHeaders::Row::midiTargetAt (juce::Point<int> p) const
{
    return p.x >= 18 && p.x < 88 ? juce::String (targetToString (MidiTarget::ofAction (ActionId::SelectTrack, track))) : juce::String();
}

void TrackHeaders::Row::refresh()
{
    const auto& t = c.project().tracks[static_cast<std::size_t> (track)];
    mute.setToggleState (t.mute, juce::dontSendNotification);
    solo.setToggleState (t.solo, juce::dontSendNotification);
    if (! volume.isMouseButtonDown())
        volume.setValue (t.params[static_cast<std::size_t> (TrackParam::Volume)], juce::dontSendNotification);

    const juce::ScopedValueSetter<bool> guard (refreshing, true);
    float value = t.params[static_cast<std::size_t> (TrackParam::Shakal)];
    bool held = false;
    if (const auto hold = c.plockTarget(); hold && hold->track == track)
    {
        held = true;
        const auto& st = c.editPattern().lanes[static_cast<std::size_t> (track)].steps[static_cast<std::size_t> (hold->slot)];
        for (const auto& pl : st.plocks)
            if (pl.param == static_cast<uint8_t> (TrackParam::Shakal))
                value = pl.value;
    }
    shakal.setLocked (held);
    if (! shakal.isMouseButtonDown())
        shakal.setValue (value, juce::dontSendNotification);
    repaint();
}

void TrackHeaders::Row::paint (juce::Graphics& g)
{
    const bool selected = c.selectedTrack() == track;
    auto r = getLocalBounds().toFloat().reduced (2.f, 1.f);
    g.setColour (selected ? palette::panel.brighter (0.06f) : palette::panel);
    g.fillRect (r);
    g.setColour (selected ? palette::neonCyan.withAlpha (0.6f) : palette::gridLine);
    g.drawRect (r, 1.f);

    // hit LED
    g.setColour (palette::nixie.withAlpha (juce::jlimit (0.f, 1.f, led)));
    g.fillRect (juce::Rectangle<float> (6.f, r.getCentreY() - 3.f, 6.f, 6.f));
    g.setColour (palette::gridLine);
    g.drawRect (juce::Rectangle<float> (6.f, r.getCentreY() - 3.f, 6.f, 6.f), 1.f);

    const auto& t = c.project().tracks[static_cast<std::size_t> (track)];
    g.setColour (selected ? palette::neonCyan : palette::text);
    g.setFont (monoFont (13.f, true));
    g.drawText (juce::String (t.name.empty() ? "T" + std::to_string (track + 1) : t.name), juce::Rectangle<int> (18, 0, 70, getHeight()),
                juce::Justification::centredLeft, true);
    midifeedback::drawMarks (g, juce::Rectangle<float> (16.f, 2.f, 72.f, static_cast<float> (getHeight()) - 4.f),
                             juce::String (targetToString (MidiTarget::ofAction (ActionId::SelectTrack, track))));
}

void TrackHeaders::Row::resized()
{
    auto r = getLocalBounds().reduced (4, 6);
    r.removeFromLeft (86);
    const int knob = juce::jmin (r.getHeight(), 30);
    shakal.setBounds (r.removeFromRight (knob));
    r.removeFromRight (2);
    volume.setBounds (r.removeFromRight (knob));
    r.removeFromRight (4);
    const int bw = 18;
    solo.setBounds (r.removeFromRight (bw).withSizeKeepingCentre (bw, 18));
    r.removeFromRight (2);
    mute.setBounds (r.removeFromRight (bw).withSizeKeepingCentre (bw, 18));
}

void TrackHeaders::Row::mouseDown (const juce::MouseEvent&)
{
    c.selectTrack (track);
}

void TrackHeaders::Row::mouseDoubleClick (const juce::MouseEvent&)
{
    c.triggerTrack (track, 100);
}

TrackHeaders::TrackHeaders (ProjectController& controller) : c_ (controller)
{
    for (int t = 0; t < kNumTracks; ++t)
    {
        rows_[static_cast<std::size_t> (t)] = std::make_unique<Row> (controller, t);
        addAndMakeVisible (*rows_[static_cast<std::size_t> (t)]);
    }
    startTimerHz (60);
}

void TrackHeaders::timerCallback()
{
    const bool changed = c_.version() != shownVersion_;
    shownVersion_ = c_.version();

    for (int t = 0; t < kNumTracks; ++t)
    {
        auto& row = *rows_[static_cast<std::size_t> (t)];
        const float led = static_cast<float> (1.0 - c_.hitAgeMs (t) / 120.0);
        if (std::abs (led - row.led) > 0.01f && (led > 0.f || row.led > 0.f))
        {
            row.led = juce::jmax (0.f, led);
            row.repaint();
        }
        if (changed)
            row.refresh();

        const bool ring = (c_.shakalSnapshot().overrideMask >> t) & 1u;
        if (static_cast<bool> (row.shakal.getProperties()[JackalLookAndFeel::ringProperty]) != ring)
        {
            row.shakal.getProperties().set (JackalLookAndFeel::ringProperty, ring);
            row.shakal.repaint();
        }
    }
}

void TrackHeaders::paint (juce::Graphics& g)
{
    g.fillAll (palette::background);
}

void TrackHeaders::resized()
{
    const int h = getHeight();
    for (int t = 0; t < kNumTracks; ++t)
        rows_[static_cast<std::size_t> (t)]->setBounds (0, t * h / kNumTracks, getWidth(), (t + 1) * h / kNumTracks - t * h / kNumTracks);
}

int TrackHeaders::rowAt (juce::Point<int> p) const
{
    if (! getLocalBounds().contains (p))
        return -1;
    return juce::jlimit (0, kNumTracks - 1, p.y * kNumTracks / juce::jmax (1, getHeight()));
}

} // namespace jackal
