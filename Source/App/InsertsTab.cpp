#include "InsertsTab.h"

#include "JackalLookAndFeel.h"

namespace jackal
{

InsertsTab::InsertsTab (ProjectController& c) : c_ (c)
{
    for (int i = 0; i < 6; ++i)
    {
        const int section = i / 2, slot = i % 2;
        std::function<int()> chain = section == 0 ? std::function<int()> ([this] { return c_.selectedTrack(); })
                                   : section == 1 ? std::function<int()> ([] { return kMasterChain; })
                                                  : std::function<int()> ([] { return kGuitarChain; });
        auto b = std::make_unique<InsertSlotButton> (c, chain, slot);
        b->onBrowse = [this] (int ch, int sl) { if (onBrowse) onBrowse (ch, sl); };
        addAndMakeVisible (*b);
        slots_[static_cast<std::size_t> (i)] = std::move (b);
    }
    startTimerHz (10);
}

void InsertsTab::timerCallback()
{
    const auto snap = c_.loopSnapshot();
    const double sr = c_.sampleRate();
    juce::String t;
    t << "PDC  tracks " << snap.pdcTracks << " " << juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) << " master " << snap.pdcMaster
      << " " << juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) << " guitar " << snap.pdcGuitar
      << " " << juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) << " comp " << juce::String (snap.compTotal / sr * 1000.0, 1) << " ms";
    if (t != pdcText_ || snap.pdcOverflow != overflow_)
    {
        pdcText_ = t;
        overflow_ = snap.pdcOverflow;
        repaint();
    }
}

void InsertsTab::paint (juce::Graphics& g)
{
    g.fillAll (palette::panel);
    g.setFont (monoFont (12.f, true));
    const auto& tr = c_.project().tracks[static_cast<std::size_t> (c_.selectedTrack())];
    const juce::String names[3] { "TRACK " + juce::String (tr.name.empty() ? juce::String (c_.selectedTrack() + 1) : juce::String (tr.name)), "MASTER", "GUITAR" };
    for (int i = 0; i < 3; ++i)
    {
        g.setColour (palette::neonCyan);
        g.drawText (names[i], titles_[static_cast<std::size_t> (i)], juce::Justification::centredLeft, false);
    }
    auto bottom = getLocalBounds().reduced (8, 6).removeFromBottom (36);
    g.setFont (monoFont (11.f));
    g.setColour (palette::dim);
    g.drawFittedText (pdcText_, bottom.removeFromTop (18), juce::Justification::centredLeft, 1);
    if (overflow_)
    {
        g.setColour (palette::alarm);
        g.setFont (monoFont (12.f, true));
        g.drawText ("PDC LIMIT", bottom, juce::Justification::centredLeft, false);
    }
}

void InsertsTab::resized()
{
    auto r = getLocalBounds().reduced (8, 6);
    for (int s = 0; s < 3; ++s)
    {
        titles_[static_cast<std::size_t> (s)] = r.removeFromTop (20);
        for (int k = 0; k < 2; ++k)
        {
            slots_[static_cast<std::size_t> (s * 2 + k)]->setBounds (r.removeFromTop (56));
            r.removeFromTop (4);
        }
        r.removeFromTop (8);
    }
}

} // namespace jackal
