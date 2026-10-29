#include "PatternStrip.h"

#include "Model/PatternOps.h"

namespace jackal
{

PatternStrip::PatternStrip (ProjectController& controller)
    : c_ (controller), strength (controller, "Mutation strength")
{
    for (auto* b : { &doubleButton, &mutateButton, &undoButton, &redoButton })
    {
        styleButton (*b);
        addAndMakeVisible (*b);
    }
    mutateButton.setColour (juce::TextButton::textColourOffId, palette::magenta);

    doubleButton.onClick = [this] { c_.doublePattern(); };
    mutateButton.onClick = [this] { c_.mutate(); };
    undoButton.onClick = [this] { c_.undo(); };
    redoButton.onClick = [this] { c_.redo(); };

    for (int i = 0; i < 4; ++i)
    {
        auto& b = pageButtons[static_cast<std::size_t> (i)];
        b.setButtonText (juce::String (i + 1));
        styleButton (b);
        b.onClick = [this, i] { c_.setGridPage (i); };
        addAndMakeVisible (b);
    }

    strength.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    strength.setRange (0.0, 1.0, 0.01);
    strength.setTooltip ("Mutation strength");
    strength.onValueChange = [this] { c_.setMutationStrength (static_cast<float> (strength.getValue())); };
    addAndMakeVisible (strength);

    startTimerHz (30);
}

void PatternStrip::pasteSelected()
{
    if (clipboard_ >= 0)
        requestCopy (clipboard_, c_.editPatternIndex());
}

void PatternStrip::requestCopy (int from, int to)
{
    if (from == to)
        return;

    const auto now = juce::Time::getMillisecondCounter();
    const bool confirmed = pendingCopyFrom_ == from && pendingCopyTo_ == to && now - pendingCopyTime_ < 3000;
    if (! ops::isEmpty (c_.project().patterns[static_cast<std::size_t> (to)]) && ! confirmed)
    {
        pendingCopyFrom_ = from;
        pendingCopyTo_ = to;
        pendingCopyTime_ = now;
        c_.setStatus (juce::String::charToString (static_cast<juce::juce_wchar> ('A' + to)) + " is not empty: repeat within 3 s to overwrite");
        return;
    }

    pendingCopyFrom_ = pendingCopyTo_ = -1;
    c_.copyPattern (from, to);
    c_.setStatus (juce::String ("copied ") + static_cast<char> ('A' + from) + " -> " + static_cast<char> ('A' + to));
}

void PatternStrip::timerCallback()
{
    if (++blinkTick_ % 8 == 0)
    {
        blink_ = ! blink_;
        if (c_.snapshot().pendingPattern >= 0)
            repaint();
    }

    if (c_.version() == shownVersion_)
        return;
    shownVersion_ = c_.version();

    const auto& pat = c_.editPattern();
    const int pages = juce::jlimit (1, 4, (juce::jmax (pat.bars * 16, static_cast<int> (pat.lanes[static_cast<std::size_t> (c_.selectedTrack())].length)) + 15) / 16);
    for (int i = 0; i < 4; ++i)
    {
        auto& b = pageButtons[static_cast<std::size_t> (i)];
        b.setEnabled (i < pages);
        b.setToggleState (i == c_.gridPage(), juce::dontSendNotification);
    }
    undoButton.setButtonText ("UNDO " + juce::String (c_.undoDepth()));
    undoButton.setEnabled (c_.canUndo());
    redoButton.setEnabled (c_.canRedo());
    if (! strength.isMouseButtonDown())
        strength.setValue (c_.project().ui.mutationStrength, juce::dontSendNotification);
    repaint();
}

juce::Rectangle<int> PatternStrip::patternButton (int i) const
{
    return { 8 + i * 40, 6, 36, getHeight() - 12 };
}

int PatternStrip::patternAt (juce::Point<int> p) const
{
    for (int i = 0; i < kNumPatterns; ++i)
        if (patternButton (i).contains (p))
            return i;
    return -1;
}

void PatternStrip::mouseDown (const juce::MouseEvent& e)
{
    dragFrom_ = patternAt (e.getPosition());
}

void PatternStrip::mouseUp (const juce::MouseEvent& e)
{
    const int to = patternAt (e.getPosition());
    if (dragFrom_ >= 0 && to >= 0)
    {
        if (to == dragFrom_)
            c_.queuePattern (to);
        else
            requestCopy (dragFrom_, to);
    }
    dragFrom_ = -1;
}

void PatternStrip::paint (juce::Graphics& g)
{
    g.fillAll (palette::panel);
    g.setColour (palette::gridLine);
    g.drawHorizontalLine (getHeight() - 1, 0.f, static_cast<float> (getWidth()));

    const auto& snap = c_.snapshot();
    const bool playing = c_.transport().playing;

    for (int i = 0; i < kNumPatterns; ++i)
    {
        const auto r = patternButton (i).toFloat();
        const bool current = playing ? snap.currentPattern == i : c_.editPatternIndex() == i;
        const bool pending = snap.pendingPattern == i;
        const bool editing = c_.editPatternIndex() == i;
        const bool empty = ops::isEmpty (c_.project().patterns[static_cast<std::size_t> (i)]);

        if (current)
        {
            g.setColour (palette::nixie.withAlpha (0.25f));
            g.fillRoundedRectangle (r, 2.f);
        }
        g.setColour (current ? palette::nixie : (pending && blink_ ? palette::nixie : (editing ? palette::neonCyan : palette::gridLine)));
        g.drawRoundedRectangle (r.reduced (0.5f), 2.f, pending || editing ? 1.5f : 1.f);

        g.setColour (empty ? palette::dim : palette::text);
        g.setFont (monoFont (15.f, true));
        g.drawText (juce::String::charToString (static_cast<juce::juce_wchar> ('A' + i)), r.toNearestInt(), juce::Justification::centred, false);
    }
}

void PatternStrip::resized()
{
    auto r = getLocalBounds().reduced (6, 6);
    r.removeFromLeft (8 * 40 + 12);
    doubleButton.setBounds (r.removeFromLeft (40));
    r.removeFromLeft (16);
    for (auto& b : pageButtons)
    {
        b.setBounds (r.removeFromLeft (26));
        r.removeFromLeft (2);
    }
    r.removeFromLeft (16);
    mutateButton.setBounds (r.removeFromLeft (76));
    r.removeFromLeft (2);
    strength.setBounds (r.removeFromLeft (r.getHeight() + 4));
    redoButton.setBounds (r.removeFromRight (56));
    r.removeFromRight (4);
    undoButton.setBounds (r.removeFromRight (76));
}

} // namespace jackal
