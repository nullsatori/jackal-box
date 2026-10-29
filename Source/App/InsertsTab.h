#pragma once

#include "ProjectController.h"
#include "Plugins/InsertSlotButton.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <functional>
#include <memory>

namespace jackal
{

// VST tab of the right panel: two slots of the selected track, of the master and of the guitar, and the PDC line.
class InsertsTab : public juce::Component, private juce::Timer
{
public:
    explicit InsertsTab (ProjectController& c);

    std::function<void (int chain, int slot)> onBrowse;

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;

    ProjectController& c_;
    std::array<std::unique_ptr<InsertSlotButton>, 6> slots_;
    std::array<juce::Rectangle<int>, 3> titles_;
    juce::String pdcText_;
    bool overflow_ = false;
};

} // namespace jackal
