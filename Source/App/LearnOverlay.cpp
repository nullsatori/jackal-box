#include "LearnOverlay.h"

#include "JackalLookAndFeel.h"
#include "MidiTargets.h"

namespace jackal
{

LearnOverlay::LearnOverlay()
{
    setInterceptsMouseClicks (true, false);
    setWantsKeyboardFocus (false);
}

void LearnOverlay::setPicking (bool on)
{
    picking_ = on;
    setMouseCursor (on ? juce::MouseCursor::CrosshairCursor : juce::MouseCursor::NormalCursor);
    repaint();
}

std::pair<juce::Component*, juce::String> LearnOverlay::targetAt (juce::Point<int> local)
{
    auto* parent = getParentComponent();
    if (parent == nullptr)
        return {};

    inLookup_ = true;   // our own hitTest says no while we look underneath
    auto* c = parent->getComponentAt (parent->getLocalPoint (this, local));
    inLookup_ = false;

    for (; c != nullptr && c != parent; c = c->getParentComponent())
    {
        if (c->getProperties().contains (kMidiTargetProperty))
            return { c, c->getProperties()[kMidiTargetProperty].toString() };
        if (auto* area = dynamic_cast<MidiTargetArea*> (c))
            if (auto t = area->midiTargetAt (c->getLocalPoint (this, local)); t.isNotEmpty())
                return { c, t };
    }
    return {};
}

bool LearnOverlay::hitTest (int x, int y)
{
    if (inLookup_)
        return false;
    if (picking_)
        return true;
    // the press that is being routed right now: only right clicks on mappable controls
    if (! juce::ModifierKeys::currentModifiers.isPopupMenu())
        return false;
    return targetAt ({ x, y }).first != nullptr;
}

void LearnOverlay::mouseDown (const juce::MouseEvent& e)
{
    const auto [comp, target] = targetAt (e.getPosition());
    if (picking_ && ! e.mods.isPopupMenu())
    {
        if (onPick)
            onPick (comp, target);
        return;
    }
    if (comp != nullptr && onContextMenu)
        onContextMenu (comp, target);
}

void LearnOverlay::paint (juce::Graphics& g)
{
    if (! picking_)
        return;
    g.setColour (palette::magenta.withAlpha (0.8f));
    g.drawRect (getLocalBounds(), 2);
}

} // namespace jackal
