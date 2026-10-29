#include "GlitchPadBar.h"

#include "JackalLookAndFeel.h"

namespace jackal
{

const char* GlitchPadBar::padName (int pad)
{
    static const char* names[kNumPads] { "STUT 1/16", "STUT 1/32", "STUT 1/64", "REVERSE", "TAPE STOP", "CRUSH", "GATE", "DIVE" };
    return pad >= 0 && pad < kNumPads ? names[pad] : "";
}

GlitchPadBar::GlitchPadBar (ProjectController& controller) : c_ (controller)
{
    startTimerHz (30);
}

void GlitchPadBar::timerCallback()
{
    const auto& s = c_.snapshot();
    int lanePad = -1;
    if (c_.transport().playing && s.glitchSlot >= 0)
    {
        const auto& pat = c_.project().patterns[static_cast<std::size_t> (juce::jlimit (0, kNumPatterns - 1, static_cast<int> (s.currentPattern)))];
        lanePad = pat.glitch.pad[static_cast<std::size_t> (s.glitchSlot % kMaxSteps)] - 1;
    }

    if (++tick_ % 6 == 0)
        blink_ = ! blink_;

    if (c_.version() != shownVersion_ || s.activePad != shownActive_ || s.pendingPad != shownPending_ || lanePad != shownLane_ || (s.pendingPad >= 0 && tick_ % 6 == 0))
    {
        shownVersion_ = c_.version();
        shownActive_ = s.activePad;
        shownPending_ = s.pendingPad;
        shownLane_ = lanePad;
        repaint();
    }
}

juce::Rectangle<int> GlitchPadBar::padBounds (int pad) const
{
    auto r = getLocalBounds().reduced (8, 8);
    const int w = (r.getWidth() - 7 * 8) / kNumPads;
    return { r.getX() + pad * (w + 8), r.getY(), w, r.getHeight() };
}

juce::String GlitchPadBar::midiTargetAt (juce::Point<int> p) const
{
    const int pad = padAt (p);
    return pad >= 0 ? juce::String (targetToString (MidiTarget::ofAction (ActionId::Pad, pad))) : juce::String();
}

int GlitchPadBar::padAt (juce::Point<int> p) const
{
    for (int i = 0; i < kNumPads; ++i)
        if (padBounds (i).contains (p))
            return i;
    return -1;
}

void GlitchPadBar::mouseDown (const juce::MouseEvent& e)
{
    const int pad = padAt (e.getPosition());
    if (pad < 0)
        return;
    if (e.mods.isCtrlDown())
    {
        c_.clearPadFromLane (pad);
        return;
    }
    held_ = pad;
    c_.padDown (pad);
}

void GlitchPadBar::mouseUp (const juce::MouseEvent&)
{
    if (held_ >= 0)
        c_.padUp (held_);
    held_ = -1;
}

void GlitchPadBar::paint (juce::Graphics& g)
{
    g.fillAll (palette::panel);
    g.setColour (palette::gridLine);
    g.drawHorizontalLine (0, 0.f, static_cast<float> (getWidth()));

    for (int i = 0; i < kNumPads; ++i)
    {
        const auto r = padBounds (i).toFloat();
        const bool active = shownActive_ == i;
        const bool pending = shownPending_ == i;
        const bool lane = shownLane_ == i;

        juce::Colour accent = palette::gridLine;
        if (active)
            accent = palette::nixie;
        else if (pending)
            accent = blink_ ? palette::nixie : palette::gridLine;
        else if (lane)
            accent = palette::neonCyan;

        if (active || lane)
        {
            g.setColour (accent.withAlpha (0.25f));
            g.fillRoundedRectangle (r, 2.f);
        }
        g.setColour (accent);
        g.drawRoundedRectangle (r.reduced (0.5f), 2.f, 1.f);

        g.setColour (active ? palette::nixie : palette::text);
        g.setFont (monoFont (12.f, true));
        g.drawText (padName (i), r.toNearestInt().reduced (4, 4), juce::Justification::centredTop, true);
        g.setColour (palette::dim);
        g.setFont (monoFont (11.f));
        g.drawText (juce::String (i + 1), r.toNearestInt().reduced (4, 4), juce::Justification::centredBottom, false);
        midifeedback::drawMarks (g, r, juce::String (targetToString (MidiTarget::ofAction (ActionId::Pad, i))));
    }
}

} // namespace jackal
