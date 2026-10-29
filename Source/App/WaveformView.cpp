#include "WaveformView.h"

#include "JackalLookAndFeel.h"

namespace jackal
{

void WaveformView::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (palette::background);
    g.fillRect (r);
    g.setColour (palette::gridLine);
    g.drawRect (r, 1.f);

    const int track = c_.selectedTrack();
    const auto* peaks = c_.peaksForTrack (track);
    if (peaks == nullptr || peaks->mins.empty())
    {
        g.setColour (palette::dim);
        g.setFont (monoFont (11.f));
        g.drawText ("no sample: drop a .wav here or LOAD", getLocalBounds(), juce::Justification::centred, true);
        return;
    }

    const float mid = r.getCentreY();
    const float half = r.getHeight() * 0.45f;
    const int n = static_cast<int> (peaks->mins.size());
    g.setColour (palette::neonCyan.withAlpha (0.8f));
    for (int x = 0; x < getWidth(); ++x)
    {
        const int b = juce::jlimit (0, n - 1, x * n / juce::jmax (1, getWidth()));
        const float lo = peaks->mins[static_cast<std::size_t> (b)], hi = peaks->maxs[static_cast<std::size_t> (b)];
        g.drawVerticalLine (x, mid - hi * half, mid - lo * half + 1.f);
    }

    const auto starts = c_.sliceStartsForTrack (track);
    const double len = static_cast<double> (juce::jmax<int64_t> (1, peaks->length));
    if (starts.size() > 1)
    {
        g.setColour (palette::nixie.withAlpha (0.7f));
        for (std::size_t i = 1; i + 1 < starts.size(); ++i)
            g.drawVerticalLine (static_cast<int> (static_cast<double> (starts[i]) / len * getWidth()), r.getY(), r.getBottom());
        g.setFont (monoFont (10.f));
        g.drawText (juce::String (static_cast<int> (starts.size()) - 1) + " slices", getLocalBounds().reduced (4, 2), juce::Justification::topRight, false);
    }
    else
    {
        const auto& t = c_.project().tracks[static_cast<std::size_t> (track)];
        const float st = t.params[static_cast<std::size_t> (TrackParam::Start)];
        const float en = t.params[static_cast<std::size_t> (TrackParam::End)];
        g.setColour (palette::background.withAlpha (0.6f));
        g.fillRect (r.withWidth (r.getWidth() * st));
        if (en > st)
            g.fillRect (r.withLeft (r.getX() + r.getWidth() * en));
    }
}

} // namespace jackal
