#pragma once

#include "../ProjectController.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace jackal
{

// One insert slot: empty "+", or LED, plugin name, latency, BYP and x. Same widget in the guitar strip
// (32 px high) and in the VST tab (56 px). Click on the name: editor; on "+" or MISSING: the browser.
class InsertSlotButton : public juce::Component, private juce::Timer
{
public:
    InsertSlotButton (ProjectController& c, std::function<int()> chain, int slot);

    std::function<void (int chain, int slot)> onBrowse;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseUp (const juce::MouseEvent&) override;

private:
    void timerCallback() override;
    void showMenu();

    ProjectController& c_;
    std::function<int()> chain_;
    int slot_;
    juce::TextButton bypass { "BYP" }, remove { "x" };
    juce::Rectangle<int> nameArea_;
    uint32_t shownVersion_ = 0;
    int shownLatency_ = -1;
    bool shownOverflow_ = false;
};

} // namespace jackal
