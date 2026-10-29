#include "SongStrip.h"

#include "MidiMapper.h"

#include "Model/SongOps.h"

namespace jackal
{

namespace
{
    constexpr int kGap = 2;

    juce::String letter (int p) { return juce::String::charToString (static_cast<juce::juce_wchar> ('A' + juce::jlimit (0, 7, p))); }
} // namespace

SongStrip::SongStrip (ProjectController& controller) : c_ (controller)
{
    for (auto* b : { &ptnButton, &songButton, &followButton, &loopButton, &clearButton })
    {
        styleButton (*b);
        addAndMakeVisible (*b);
    }
    ptnButton.setColour (juce::TextButton::buttonOnColourId, palette::neonCyan);
    songButton.setColour (juce::TextButton::buttonOnColourId, palette::nixie);
    followButton.setColour (juce::TextButton::buttonOnColourId, palette::neonCyan);
    loopButton.setColour (juce::TextButton::buttonOnColourId, palette::neonCyan);

    ptnButton.onClick = [this] { c_.setSongMode (false); };
    songButton.onClick = [this] { c_.setSongMode (true); };
    followButton.onClick = [this] { c_.setFollow (! c_.follow()); };
    loopButton.onClick = [this] { c_.editSong ("Song loop", [] (SongData& s) { s.loop = ! s.loop; }); };
    clearButton.onClick = [this] { c_.editSong ("Clear song", [] (SongData& s) { songops::clear (s); }); };

    ptnButton.setTooltip ("pattern mode: A..H queue on the next bar (P)");
    songButton.setTooltip ("song mode: play the row below (P)");
    followButton.setTooltip ("the edited pattern follows the song (O)");
    loopButton.setTooltip ("loop the song at the end");
    clearButton.setTooltip ("clear the song (undo brings it back)");

    setMidiTarget (songButton, MidiTarget::ofAction (ActionId::SongMode));
    startTimerHz (30);
}

juce::String SongStrip::signature() const
{
    const auto& s = c_.snapshot();
    juce::String sig;
    sig << static_cast<int> (c_.version()) << '|' << s.songIndex << '|' << s.songRepeat << '|' << s.songCursor << '|'
        << static_cast<int> (s.songMode) << '|' << static_cast<int> (s.songEnded) << '|' << static_cast<int> (blink_) << '|'
        << hoverCell_ << '|' << dropAt_ << '|' << static_cast<int> (c_.transport().playing);
    return sig;
}

void SongStrip::timerCallback()
{
    if (++tick_ % 8 == 0)   // ~2 Hz blink
        blink_ = ! blink_;

    const auto sig = signature();
    if (sig == shown_)
        return;
    shown_ = sig;

    const bool song = c_.songMode();
    ptnButton.setToggleState (! song, juce::dontSendNotification);
    songButton.setToggleState (song, juce::dontSendNotification);
    followButton.setToggleState (c_.follow(), juce::dontSendNotification);
    loopButton.setToggleState (c_.song().loop, juce::dontSendNotification);
    clearButton.setEnabled (c_.song().length > 0);
    repaint();
}

juce::Rectangle<int> SongStrip::cell (int i) const
{
    return { cellsArea_.getX() + i * (cellSize_ + kGap), cellsArea_.getCentreY() - cellSize_ / 2, cellSize_, cellSize_ };
}

int SongStrip::cellAt (juce::Point<int> p) const
{
    for (int i = 0; i < kMaxSongEntries; ++i)
        if (cell (i).expanded (kGap / 2, 4).contains (p))
            return i;
    return -1;
}

int SongStrip::dropIndexAt (juce::Point<int> p) const
{
    const int len = c_.song().length;
    for (int i = 0; i < len; ++i)
        if (p.x < cell (i).getCentreX())
            return i;
    return len;
}

std::optional<int> SongStrip::hoverEntry() const
{
    if (hoverCell_ >= 0 && hoverCell_ < c_.song().length)
        return hoverCell_;
    return std::nullopt;
}

void SongStrip::mouseMove (const juce::MouseEvent& e)
{
    hoverCell_ = cellAt (e.getPosition());
}

void SongStrip::mouseExit (const juce::MouseEvent&)
{
    hoverCell_ = -1;
}

void SongStrip::mouseDown (const juce::MouseEvent& e)
{
    pressCell_ = cellAt (e.getPosition());
    dragging_ = false;
    dropAt_ = -1;

    const int len = c_.song().length;
    if (e.mods.isPopupMenu() && pressCell_ >= 0 && pressCell_ < len)
    {
        const int at = pressCell_;
        c_.editSong ("Song remove", [at] (SongData& s) { songops::remove (s, at); });
        pressCell_ = -1;
    }
}

void SongStrip::mouseDrag (const juce::MouseEvent& e)
{
    if (pressCell_ < 0 || pressCell_ >= c_.song().length)
        return;
    if (! dragging_ && e.getDistanceFromDragStart() > 4)
        dragging_ = true;
    if (dragging_)
        dropAt_ = dropIndexAt (e.getPosition());
}

void SongStrip::mouseUp (const juce::MouseEvent& e)
{
    const int from = pressCell_;
    pressCell_ = -1;
    if (from < 0 || e.mods.isPopupMenu())
        return;

    const int len = c_.song().length;
    if (dragging_)
    {
        dragging_ = false;
        const int to = dropAt_;
        dropAt_ = -1;
        if (to >= 0 && to != from && to != from + 1)
            c_.editSong ("Song move", [from, to] (SongData& s) { songops::move (s, from, to); });
        return;
    }

    if (cellAt (e.getPosition()) != from)
        return;

    const int edit = c_.editPatternIndex();
    if (e.mods.isShiftDown() && from <= len)
    {
        if (len >= kMaxSongEntries)
            c_.setStatus ("song is full (32)");
        else
            c_.editSong ("Song insert " + letter (edit), [from, edit] (SongData& s) { songops::insert (s, from, edit, 1); });
        return;
    }
    if (from == len)
    {
        c_.appendToSong (edit);
        return;
    }
    if (from < len)
        c_.setSongCursor (from);
}

void SongStrip::mouseDoubleClick (const juce::MouseEvent& e)
{
    const int i = cellAt (e.getPosition());
    if (i >= 0 && i < c_.song().length)
        c_.selectEditPattern (c_.song().entries[static_cast<std::size_t> (i)].pattern);
}

void SongStrip::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    const int i = cellAt (e.getPosition());
    if (i < 0 || i >= c_.song().length || w.deltaY == 0.f)
        return;
    const int dir = w.deltaY > 0.f ? 1 : -1;
    const auto entry = c_.song().entries[static_cast<std::size_t> (i)];
    if (e.mods.isCtrlDown() || e.mods.isCommandDown())
    {
        const int p = ((entry.pattern + dir) % kNumPatterns + kNumPatterns) % kNumPatterns;
        c_.editSong ("Song pattern", [i, p] (SongData& s) { songops::setPattern (s, i, p); });
    }
    else
    {
        const int n = entry.repeats + dir;
        c_.editSong ("Song repeats", [i, n] (SongData& s) { songops::setRepeats (s, i, n); });
    }
}

void SongStrip::paint (juce::Graphics& g)
{
    g.fillAll (palette::panel);
    g.setColour (palette::gridLine);
    g.drawHorizontalLine (getHeight() - 1, 0.f, static_cast<float> (getWidth()));

    const auto& song = c_.song();
    const auto& snap = c_.snapshot();
    const bool playing = c_.transport().playing;
    const int playingCell = snap.songMode ? snap.songIndex : -1;
    const int cursorCell = playing ? (snap.songMode && snap.songCursor >= 0 && snap.songCursor != snap.songIndex ? snap.songCursor : -1)
                                   : (song.length > 0 ? c_.songCursor() : -1);
    const int edit = c_.editPatternIndex();

    for (int i = 0; i < kMaxSongEntries; ++i)
    {
        const auto r = cell (i).toFloat();
        if (i >= song.length)
        {
            g.setColour (palette::ledOff);
            g.fillRect (r);
            if (i == song.length)
            {
                g.setColour (palette::dim);
                g.setFont (monoFont (14.f));
                g.drawText ("+", r.toNearestInt(), juce::Justification::centred, false);
            }
            continue;
        }

        const auto& en = song.entries[static_cast<std::size_t> (i)];
        if (en.pattern == edit)
        {
            g.setColour (palette::neonCyan.withAlpha (0.15f));
            g.fillRect (r);
        }

        if (i == playingCell)
        {
            g.setColour (palette::nixie);
            g.drawRect (r, 2.f);
        }
        else if (i == cursorCell && blink_)
        {
            g.setColour (palette::nixie);
            g.drawRect (r, 2.f);
        }
        else
        {
            g.setColour (palette::gridLine.brighter (i == hoverCell_ ? 0.5f : 0.f));
            g.drawRect (r, 1.f);
        }

        g.setColour (songEntryValid (song, i) ? palette::text : palette::alarm);
        g.setFont (monoFont (13.f, true));
        g.drawText (letter (en.pattern), r.toNearestInt(), juce::Justification::centred, false);
        if (en.repeats > 1)
        {
            g.setColour (palette::dim);
            g.setFont (monoFont (9.f));
            g.drawText (juce::String (juce::CharPointer_UTF8 ("\xc3\x97")) + juce::String (en.repeats), r.toNearestInt().reduced (2, 1),
                        juce::Justification::bottomRight, false);
        }

        if (i == playingCell && en.repeats > 0)
        {
            const float f = juce::jlimit (0.f, 1.f, static_cast<float> (snap.songRepeat + 1) / static_cast<float> (en.repeats));
            g.setColour (palette::nixie);
            g.fillRect (r.getX(), r.getBottom() + 2.f, r.getWidth() * f, 2.f);
        }
    }

    if (dragging_ && dropAt_ >= 0)
    {
        const int x = dropAt_ < song.length ? cell (dropAt_).getX() - 2 : cell (juce::jmax (0, song.length - 1)).getRight() + 1;
        g.setColour (palette::neonCyan);
        g.fillRect (x, cellsArea_.getY(), 2, cellsArea_.getHeight());
    }

    // counter: entry / length
    const int shownEntry = playingCell >= 0 ? playingCell : (song.length > 0 ? c_.songCursor() : -1);
    g.setColour (snap.songEnded ? palette::nixie : palette::dim);
    g.setFont (monoFont (11.f));
    g.drawText (juce::String (shownEntry + 1) + "/" + juce::String (song.length), counterArea_, juce::Justification::centredRight, false);
}

void SongStrip::resized()
{
    auto r = getLocalBounds().reduced (8, 4);
    ptnButton.setBounds (r.removeFromLeft (48));
    songButton.setBounds (r.removeFromLeft (48));
    r.removeFromLeft (8);
    followButton.setBounds (r.removeFromLeft (56));
    r.removeFromLeft (8);
    loopButton.setBounds (r.removeFromLeft (48));
    r.removeFromLeft (8);

    counterArea_ = r.removeFromRight (40);
    r.removeFromRight (4);
    clearButton.setBounds (r.removeFromRight (48));
    r.removeFromRight (8);

    cellSize_ = juce::jlimit (14, 28, r.getWidth() / kMaxSongEntries - kGap);
    cellsArea_ = r.withWidth (kMaxSongEntries * (cellSize_ + kGap));
}

} // namespace jackal
