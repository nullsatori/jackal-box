#include "JackalLookAndFeel.h"

namespace jackal
{

namespace
{
    juce::LookAndFeel_V4::ColourScheme makeScheme()
    {
        return { palette::background,  // windowBackground
                 palette::panel,       // widgetBackground
                 palette::panel,       // menuBackground
                 palette::neonCyan.withAlpha (0.35f), // outline
                 palette::text,        // defaultText
                 palette::neonCyan.withAlpha (0.6f),  // defaultFill
                 palette::background,  // highlightedText
                 palette::nixie,       // highlightedFill
                 palette::text };      // menuText
    }
} // namespace

JackalLookAndFeel::JackalLookAndFeel()
    : juce::LookAndFeel_V4 (makeScheme())
{
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxTextColourId, palette::text);
    setColour (juce::Slider::trackColourId, palette::neonCyan.withAlpha (0.6f));
    setColour (juce::Slider::backgroundColourId, palette::ledOff);
    setColour (juce::TextButton::buttonColourId, palette::panel);
    setColour (juce::TextButton::buttonOnColourId, palette::neonCyan);
    setColour (juce::TextButton::textColourOffId, palette::text);
    setColour (juce::TextButton::textColourOnId, palette::neonCyan);
    setColour (juce::ComboBox::backgroundColourId, palette::panel);
    setColour (juce::ComboBox::outlineColourId, palette::gridLine);
    setColour (juce::TextEditor::backgroundColourId, palette::background);
    setColour (juce::TextEditor::outlineColourId, palette::gridLine);
    setColour (juce::TextEditor::focusedOutlineColourId, palette::neonCyan);
    setColour (juce::Label::textColourId, palette::text);
}

juce::Typeface::Ptr JackalLookAndFeel::getTypefaceForFont (const juce::Font& font)
{
    if (font.getTypefaceName() == juce::Font::getDefaultSansSerifFontName())
    {
        auto mono = font;
        mono.setTypefaceName ("Consolas");

        // Consolas ships with Windows; elsewhere fall back to the default font
        if (auto typeface = juce::LookAndFeel_V4::getTypefaceForFont (mono); typeface != nullptr && typeface->getName() == "Consolas")
            return typeface;
    }

    return juce::LookAndFeel_V4::getTypefaceForFont (font);
}

namespace
{
    // stage 5: a bound control gets a 3 px dim dot top left; the one learn waits on gets a magenta outline
    void drawMidiMarks (juce::Graphics& g, juce::Component& c, juce::Rectangle<float> r)
    {
        const auto& props = c.getProperties();
        if (props.contains ("midiMapped"))
        {
            g.setColour (palette::dim);
            g.fillEllipse (r.getX() + 1.f, r.getY() + 1.f, 3.f, 3.f);
        }
        if (static_cast<bool> (props["learning"]))
        {
            g.setColour (palette::magenta);
            g.drawRect (r, 1.f);
        }
    }
} // namespace

void JackalLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height, float pos,
                                          float startAngle, float endAngle, juce::Slider& slider)
{
    const bool locked = static_cast<bool> (slider.getProperties()[lockedProperty]);
    const auto colour = ! slider.isEnabled() ? palette::dim : (locked ? palette::nixie : palette::neonCyan);

    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat().reduced (3.f);
    const float radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f;
    const auto centre = bounds.getCentre();
    const float angle = startAngle + pos * (endAngle - startAngle);

    juce::Path track;
    track.addCentredArc (centre.x, centre.y, radius, radius, 0.f, startAngle, endAngle, true);
    g.setColour (palette::gridLine);
    g.strokePath (track, juce::PathStrokeType (1.5f));

    juce::Path value;
    value.addCentredArc (centre.x, centre.y, radius, radius, 0.f, startAngle, angle, true);
    g.setColour (colour);
    g.strokePath (value, juce::PathStrokeType (1.5f));

    const juce::Point<float> tip (centre.x + std::sin (angle) * radius, centre.y - std::cos (angle) * radius);
    const juce::Point<float> inner (centre.x + std::sin (angle) * radius * 0.55f, centre.y - std::cos (angle) * radius * 0.55f);
    g.drawLine ({ inner, tip }, 1.5f);

    const auto actual = slider.getProperties()[actualProperty];
    if (! actual.isVoid())
    {
        const float a = startAngle + juce::jlimit (0.f, 1.f, static_cast<float> (actual)) * (endAngle - startAngle);
        const float r2 = radius - 3.f;
        juce::Path arc;
        arc.addCentredArc (centre.x, centre.y, r2, r2, 0.f, startAngle, a, true);
        g.setColour (palette::dim);
        g.strokePath (arc, juce::PathStrokeType (1.f));
    }

    if (static_cast<bool> (slider.getProperties()[ringProperty]))
    {
        g.setColour (palette::nixie);
        g.drawEllipse (bounds.withSizeKeepingCentre (radius * 2.f + 4.f, radius * 2.f + 4.f), 1.f);
    }

    drawMidiMarks (g, slider, juce::Rectangle<int> (x, y, width, height).toFloat());
}

void JackalLookAndFeel::drawButtonBackground (juce::Graphics& g, juce::Button& button, const juce::Colour& backgroundColour,
                                              bool highlighted, bool down)
{
    auto r = button.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = button.getToggleState();
    const auto accent = button.findColour (juce::TextButton::buttonOnColourId);

    if (on)
        g.setColour (accent.withAlpha (0.25f));
    else
        g.setColour (backgroundColour.brighter (down ? 0.25f : (highlighted ? 0.1f : 0.f)));
    g.fillRoundedRectangle (r, 2.f);

    g.setColour (on ? accent : palette::gridLine.brighter (highlighted ? 0.4f : 0.f));
    g.drawRoundedRectangle (r, 2.f, 1.f);
    drawMidiMarks (g, button, r);
}

void JackalLookAndFeel::drawToggleButton (juce::Graphics& g, juce::ToggleButton& button, bool highlighted, bool)
{
    auto r = button.getLocalBounds().toFloat().reduced (0.5f);
    const bool on = button.getToggleState();
    g.setColour (on ? palette::neonCyan.withAlpha (0.25f) : palette::panel.brighter (highlighted ? 0.08f : 0.f));
    g.fillRoundedRectangle (r, 2.f);
    g.setColour (on ? palette::neonCyan : palette::gridLine);
    g.drawRoundedRectangle (r, 2.f, 1.f);
    g.setColour (on ? palette::neonCyan : palette::text);
    g.setFont (monoFont (12.f));
    g.drawFittedText (button.getButtonText(), button.getLocalBounds().reduced (4, 0), juce::Justification::centred, 1);
    drawMidiMarks (g, button, r);
}

juce::Font JackalLookAndFeel::getTextButtonFont (juce::TextButton&, int buttonHeight)
{
    return monoFont (juce::jmin (13.f, buttonHeight * 0.6f));
}

juce::Font JackalLookAndFeel::getLabelFont (juce::Label& label)
{
    return label.getFont();
}

juce::Font JackalLookAndFeel::getComboBoxFont (juce::ComboBox&)
{
    return monoFont (12.f);
}

juce::Label* JackalLookAndFeel::createSliderTextBox (juce::Slider& slider)
{
    auto* l = juce::LookAndFeel_V4::createSliderTextBox (slider);
    l->setFont (monoFont (11.f));
    return l;
}

} // namespace jackal
