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

} // namespace jackal
