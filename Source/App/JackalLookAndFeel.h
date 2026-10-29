#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

namespace jackal
{

namespace palette
{
    const juce::Colour background { 0xFF0B0E11 };
    const juce::Colour panel      { 0xFF151A1F };
    const juce::Colour neonCyan   { 0xFF2EE6D6 };
    const juce::Colour nixie      { 0xFFFF8A2A };
    const juce::Colour text       { 0xFFC9D1D9 };
    const juce::Colour alarm      { 0xFFFF3B3B };
    // stage 2
    const juce::Colour ledOff     { 0xFF1B2229 };
    const juce::Colour gridLine   { 0xFF232B33 };
    const juce::Colour dim        { 0xFF5A6673 };
    const juce::Colour magenta    { 0xFFFF2FA0 };   // p-locks only
    const juce::Colour rec        { 0xFFFF3B3B };
} // namespace palette

inline juce::Font monoFont (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (size, bold ? juce::Font::bold : juce::Font::plain));
}

// Still a style stub: flat rectangles, 1 px outlines, no gradients. Full art pass after the MVP.
class JackalLookAndFeel : public juce::LookAndFeel_V4
{
public:
    JackalLookAndFeel();
    juce::Typeface::Ptr getTypefaceForFont (const juce::Font& font) override;

    void drawRotarySlider (juce::Graphics&, int x, int y, int width, int height, float sliderPosProportional,
                           float rotaryStartAngle, float rotaryEndAngle, juce::Slider&) override;
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour& backgroundColour,
                               bool shouldDrawButtonAsHighlighted, bool shouldDrawButtonAsDown) override;
    void drawToggleButton (juce::Graphics&, juce::ToggleButton&, bool highlighted, bool down) override;
    juce::Font getTextButtonFont (juce::TextButton&, int buttonHeight) override;
    juce::Font getLabelFont (juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    juce::Label* createSliderTextBox (juce::Slider&) override;

    // A slider with this property set to true draws in nixie orange (p-locked knob).
    static constexpr const char* lockedProperty = "jackalLocked";
    // Optional 0..1: a thin second arc with the value the engine actually applies (SHAKAL macro).
    static constexpr const char* actualProperty = "actual";
    // true: an orange ring around the knob (SHAKAL override from a p-lock is playing).
    static constexpr const char* ringProperty = "jackalRing";
};

} // namespace jackal
