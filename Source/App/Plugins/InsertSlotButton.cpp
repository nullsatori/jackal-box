#include "InsertSlotButton.h"

#include "../MidiTargets.h"

#include "../JackalLookAndFeel.h"
#include "../Widgets.h"

namespace jackal
{

InsertSlotButton::InsertSlotButton (ProjectController& c, std::function<int()> chain, int slot)
    : c_ (c), chain_ (std::move (chain)), slot_ (slot)
{
    for (auto* b : { &bypass, &remove })
    {
        styleButton (*b);
        addChildComponent (*b);
    }
    bypass.setColour (juce::TextButton::buttonOnColourId, palette::nixie);
    bypass.onClick = [this] {
        const int ch = chain_();
        c_.setInsertBypass (ch, slot_, ! c_.insertSpec (ch, slot_).bypass);
    };
    remove.onClick = [this] { c_.removePlugin (chain_(), slot_); };
    startTimerHz (10);
}

void InsertSlotButton::timerCallback()
{
    const auto snap = c_.loopSnapshot();
    const int lat = c_.insertLatency (chain_(), slot_);
    if (c_.version() != shownVersion_ || lat != shownLatency_ || snap.pdcOverflow != shownOverflow_)
    {
        shownVersion_ = c_.version();
        shownLatency_ = lat;
        shownOverflow_ = snap.pdcOverflow;
        const bool used = ! c_.insertSpec (chain_(), slot_).empty();
        bypass.setVisible (used);
        remove.setVisible (used);
        bypass.setToggleState (c_.insertSpec (chain_(), slot_).bypass, juce::dontSendNotification);
        setMidiTarget (bypass, MidiTarget::ofParam (insertParam (chain_(), slot_, InsertParam::Bypass)));   // stage 5
        repaint();
    }
}

void InsertSlotButton::resized()
{
    auto r = getLocalBounds().reduced (2);
    const bool tall = getHeight() >= 48;
    auto buttons = tall ? r.removeFromBottom (20) : r.removeFromRight (56);
    remove.setBounds (buttons.removeFromRight (tall ? 24 : 20).reduced (1));
    bypass.setBounds (buttons.removeFromRight (tall ? 40 : 34).reduced (1));
    nameArea_ = r;
}

void InsertSlotButton::paint (juce::Graphics& g)
{
    const int ch = chain_();
    const auto& spec = c_.insertSpec (ch, slot_);
    auto r = getLocalBounds().toFloat().reduced (0.5f);
    g.setColour (palette::background);
    g.fillRect (r);
    g.setColour (palette::gridLine);
    g.drawRect (r, 1.f);

    auto area = nameArea_;
    if (spec.empty())
    {
        g.setColour (palette::dim);
        g.setFont (monoFont (16.f, true));
        g.drawText ("+", area, juce::Justification::centred, false);
        return;
    }

    const bool missing = c_.insertMissing (ch, slot_);
    const auto led = area.removeFromLeft (14).withSizeKeepingCentre (8, 8).toFloat();
    g.setColour (missing ? palette::alarm : spec.bypass ? palette::ledOff : palette::neonCyan);
    g.fillEllipse (led);

    const int lat = c_.insertLatency (ch, slot_);
    if (lat > 0)
    {
        g.setFont (monoFont (10.f));
        g.setColour (shownOverflow_ ? palette::alarm : palette::dim);
        auto latArea = getHeight() >= 48 ? area.removeFromBottom (14) : area.removeFromRight (52);
        g.drawText (juce::String (lat) + " smp", latArea, juce::Justification::centredRight, false);
    }

    g.setFont (monoFont (12.f, true));
    g.setColour (missing ? palette::alarm : palette::text);
    const auto label = missing ? "MISSING " + juce::String (spec.name) : juce::String (spec.name);
    g.drawFittedText (label, area.reduced (2, 0), juce::Justification::centredLeft, getHeight() >= 48 ? 2 : 1);
}

void InsertSlotButton::mouseUp (const juce::MouseEvent& e)
{
    const int ch = chain_();
    if (e.mods.isPopupMenu())
    {
        showMenu();
        return;
    }
    if (! nameArea_.contains (e.getPosition()))
        return;
    c_.setLastInsert (ch, slot_);
    if (c_.insertSpec (ch, slot_).empty() || c_.insertMissing (ch, slot_))
    {
        if (onBrowse)
            onBrowse (ch, slot_);
    }
    else
        c_.toggleEditor (ch, slot_);
}

void InsertSlotButton::showMenu()
{
    const int ch = chain_();
    const bool used = ! c_.insertSpec (ch, slot_).empty();
    juce::PopupMenu m;
    m.addItem (1, used ? "Replace..." : "Load...");
    m.addItem (2, "Bypass", used, used && c_.insertSpec (ch, slot_).bypass);
    m.addItem (3, "Remove", used);
    m.addItem (4, "Open editor", used && ! c_.insertMissing (ch, slot_));
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [this, ch] (int r) {
        switch (r)
        {
            case 1: if (onBrowse) onBrowse (ch, slot_); break;
            case 2: c_.setInsertBypass (ch, slot_, ! c_.insertSpec (ch, slot_).bypass); break;
            case 3: c_.removePlugin (ch, slot_); break;
            case 4: c_.toggleEditor (ch, slot_); break;
            default: break;
        }
    });
}

} // namespace jackal
