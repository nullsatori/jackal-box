#pragma once

#include "ProjectController.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

namespace jackal
{

// 8 rows x 16 step buttons of the current page. Draws all 128 cells itself (no child components)
// and implements the gesture table of the stage 2 contract (§9.3).
class StepGrid : public juce::Component, private juce::Timer
{
public:
    explicit StepGrid (ProjectController& controller);

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;

    int rowAt (juce::Point<int> p) const;           // -1 outside
    std::optional<StepRef> hoverStep() const { return hover_; }

private:
    enum class Mode { None, Pending, Paint, Velocity, Erase, Ramp, PLockHold };

    void timerCallback() override;
    juce::Rectangle<float> cellBounds (int row, int col) const;
    std::optional<StepRef> cellAt (juce::Point<float> p, bool clampToRow = false) const;
    bool editable (StepRef s) const;
    const Step& stepAt (StepRef s) const;
    void paintAlong (int row, int fromSlot, int toSlot, bool active);

    ProjectController& c_;
    Mode mode_ = Mode::None;
    StepRef down_ {};
    int lastPaintSlot_ = -1;
    bool paintValue_ = true;
    float startY_ = 0.f;
    int startVel_ = 100;
    juce::uint32 downTime_ = 0;
    std::optional<StepRef> hover_;

    uint32_t shownVersion_ = 0;
    std::array<int16_t, kNumTracks> shownSlots_ {};
    int shownPattern_ = -1;
};

} // namespace jackal
