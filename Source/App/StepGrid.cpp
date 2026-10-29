#include "StepGrid.h"

#include "JackalLookAndFeel.h"

#include "Model/PatternOps.h"

namespace jackal
{

namespace
{
    constexpr int kGroupGap = 10;
    constexpr float kCellGap = 3.f;
    constexpr int kLongPressMs = 250;

    const char* conditionLabel (StepCondition c)
    {
        switch (c)
        {
            case StepCondition::Always:     return "";
            case StepCondition::Fill:       return "F";
            case StepCondition::NotFill:    return "!F";
            case StepCondition::FirstCycle: return "1st";
            case StepCondition::Cycle1of2:  return "1:2";
            case StepCondition::Cycle2of2:  return "2:2";
        }
        return "";
    }
} // namespace

StepGrid::StepGrid (ProjectController& controller) : c_ (controller)
{
    shownSlots_.fill (-2);
    startTimerHz (60);
}

void StepGrid::timerCallback()
{
    // long press: hold a step for p-locks
    if (mode_ == Mode::Pending && juce::Time::getMillisecondCounter() - downTime_ >= static_cast<juce::uint32> (kLongPressMs))
    {
        mode_ = Mode::PLockHold;
        c_.setPLockTarget (down_);
    }

    const auto& snap = c_.snapshot();
    if (c_.version() != shownVersion_ || snap.laneSlot != shownSlots_ || snap.currentPattern != shownPattern_)
    {
        shownVersion_ = c_.version();
        shownSlots_ = snap.laneSlot;
        shownPattern_ = snap.currentPattern;
        repaint();
    }
}

juce::Rectangle<float> StepGrid::cellBounds (int row, int col) const
{
    const float w = static_cast<float> (getWidth() - 3 * kGroupGap);
    const float cw = w / 16.f;
    const float rh = static_cast<float> (getHeight()) / kNumTracks;
    const float x = static_cast<float> (col) * cw + static_cast<float> ((col / 4) * kGroupGap);
    return { x + kCellGap * 0.5f, static_cast<float> (row) * rh + kCellGap, cw - kCellGap, rh - 2.f * kCellGap };
}

int StepGrid::rowAt (juce::Point<int> p) const
{
    if (! getLocalBounds().contains (p))
        return -1;
    return juce::jlimit (0, kNumTracks - 1, p.y * kNumTracks / juce::jmax (1, getHeight()));
}

std::optional<StepRef> StepGrid::cellAt (juce::Point<float> p, bool clampToRow) const
{
    const int row = clampToRow ? down_.track : static_cast<int> (p.y * kNumTracks / juce::jmax (1, getHeight()));
    if (row < 0 || row >= kNumTracks)
        return std::nullopt;

    for (int col = 0; col < 16; ++col)
    {
        auto r = cellBounds (row, col).expanded (kCellGap * 0.5f + (col % 4 == 3 ? kGroupGap * 0.5f : 0.f), kCellGap);
        if (clampToRow)
            r = r.withY (-1.0e6f).withHeight (2.0e6f);
        if (r.contains (p))
            return StepRef { row, c_.gridPage() * 16 + col };
    }
    if (clampToRow)
        return p.x < 0 ? StepRef { row, c_.gridPage() * 16 } : StepRef { row, c_.gridPage() * 16 + 15 };
    return std::nullopt;
}

bool StepGrid::editable (StepRef s) const
{
    return s.slot < c_.editPattern().lanes[static_cast<std::size_t> (s.track)].length;
}

const Step& StepGrid::stepAt (StepRef s) const
{
    return c_.editPattern().lanes[static_cast<std::size_t> (s.track)].steps[static_cast<std::size_t> (s.slot)];
}

void StepGrid::paintAlong (int row, int fromSlot, int toSlot, bool active)
{
    const int len = c_.editPattern().lanes[static_cast<std::size_t> (row)].length;
    const int lo = juce::jmin (fromSlot, toSlot), hi = juce::jmin (juce::jmax (fromSlot, toSlot), len - 1);
    if (lo > hi)
        return;
    c_.editPattern ([=] (Pattern& p) { ops::setStepActive (p, row, lo, hi, active); });
}

void StepGrid::mouseDown (const juce::MouseEvent& e)
{
    auto cell = cellAt (e.position);
    if (! cell || ! editable (*cell))
        return;

    down_ = *cell;
    c_.selectTrack (cell->track);
    c_.setLastTouched (*cell);
    const auto s = *cell;

    if (e.mods.isPopupMenu())
    {
        c_.beginGesture ("Erase");
        paintAlong (s.track, s.slot, s.slot, false);
        lastPaintSlot_ = s.slot;
        mode_ = Mode::Erase;
        return;
    }

    if (e.mods.isAltDown())
    {
        c_.beginGesture ("Reverse");
        c_.editPattern ([s] (Pattern& p) { auto& st = ops::step (p, s.track, s.slot); st.reverse = ! st.reverse; });
        mode_ = Mode::None;
        return;
    }

    if (e.mods.isCtrlDown())
    {
        c_.beginGesture ("Condition");
        c_.editPattern ([s] (Pattern& p) { auto& st = ops::step (p, s.track, s.slot); st.condition = ops::nextCondition (st.condition); });
        mode_ = Mode::None;
        return;
    }

    if (e.mods.isShiftDown())
    {
        mode_ = Mode::Ramp;
        return;
    }

    c_.beginGesture ("Step");
    mode_ = Mode::Pending;
    startY_ = e.position.y;
    startVel_ = stepAt (s).velocity;
    downTime_ = juce::Time::getMillisecondCounter();
}

void StepGrid::mouseDrag (const juce::MouseEvent& e)
{
    const auto cur = cellAt (e.position, true);
    if (! cur)
        return;

    switch (mode_)
    {
        case Mode::Pending:
            if (cur->slot != down_.slot)
            {
                const bool newValue = ! stepAt (down_).active;
                paintAlong (down_.track, down_.slot, down_.slot, newValue);
                paintValue_ = newValue;
                lastPaintSlot_ = down_.slot;
                mode_ = Mode::Paint;
                paintAlong (down_.track, lastPaintSlot_, cur->slot, paintValue_);
                lastPaintSlot_ = cur->slot;
            }
            else if (std::abs (e.position.y - startY_) > 3.f)
            {
                mode_ = Mode::Velocity;
            }
            break;

        case Mode::Velocity:
            if (cur->slot != down_.slot)
            {
                paintValue_ = true;
                lastPaintSlot_ = down_.slot;
                mode_ = Mode::Paint;
                paintAlong (down_.track, lastPaintSlot_, cur->slot, paintValue_);
                lastPaintSlot_ = cur->slot;
            }
            else
            {
                const int vel = juce::jlimit (1, 127, startVel_ + juce::roundToInt (startY_ - e.position.y));
                const auto s = down_;
                c_.editPattern ([=] (Pattern& p) {
                    auto& st = ops::step (p, s.track, s.slot);
                    st.active = true;
                    st.velocity = static_cast<uint8_t> (vel);
                });
            }
            break;

        case Mode::Paint:
        case Mode::Erase:
            if (cur->slot != lastPaintSlot_)
            {
                paintAlong (down_.track, lastPaintSlot_, cur->slot, mode_ == Mode::Paint ? paintValue_ : false);
                lastPaintSlot_ = cur->slot;
            }
            break;

        case Mode::None:
        case Mode::Ramp:
        case Mode::PLockHold:
            break;
    }
}

void StepGrid::mouseUp (const juce::MouseEvent& e)
{
    switch (mode_)
    {
        case Mode::Pending:
        {
            const auto s = down_;
            c_.editPattern ([s] (Pattern& p) {
                auto& st = ops::step (p, s.track, s.slot);
                st.active = ! st.active;
                if (st.active)
                    st.velocity = 100;
            });
            break;
        }

        case Mode::PLockHold:
            c_.setPLockTarget (std::nullopt);
            break;

        case Mode::Ramp:
        {
            const auto end = cellAt (e.position, true);
            if (end)
            {
                const auto from = down_;
                const auto rowRect = cellBounds (from.track, 0);
                const float f = juce::jlimit (0.f, 1.f, (e.position.y - rowRect.getY()) / juce::jmax (1.f, rowRect.getHeight()));
                const int v1 = juce::jlimit (1, 127, juce::roundToInt (127.f - f * 126.f));
                const int v0 = stepAt (from).velocity;
                const int to = juce::jmin (end->slot, c_.editPattern().lanes[static_cast<std::size_t> (from.track)].length - 1);
                c_.beginGesture ("Velocity ramp");
                c_.editPattern ([=] (Pattern& p) { ops::rampVelocity (p, from.track, from.slot, to, v0, v1); });
            }
            break;
        }

        case Mode::None:
        case Mode::Paint:
        case Mode::Velocity:
        case Mode::Erase:
            break;
    }
    mode_ = Mode::None;
}

void StepGrid::mouseMove (const juce::MouseEvent& e)
{
    hover_ = cellAt (e.position);
}

void StepGrid::mouseExit (const juce::MouseEvent&)
{
    hover_.reset();
}

void StepGrid::mouseDoubleClick (const juce::MouseEvent& e)
{
    if (auto cell = cellAt (e.position); cell && editable (*cell))
        c_.triggerTrack (cell->track, stepAt (*cell).velocity);
}

void StepGrid::mouseWheelMove (const juce::MouseEvent& e, const juce::MouseWheelDetails& w)
{
    auto cell = cellAt (e.position);
    if (! cell || ! editable (*cell))
        return;

    const float d = w.deltaY != 0.f ? w.deltaY : w.deltaX;
    if (d == 0.f)
        return;
    const int dir = d > 0.f ? 1 : -1;
    const auto s = *cell;
    const bool sliced = c_.trackSliced (s.track);
    const int numSlices = c_.numSlicesForTrack (s.track);

    c_.setLastTouched (s);
    c_.beginGesture ("Wheel");
    c_.editPattern ([=] (Pattern& p) {
        auto& st = ops::step (p, s.track, s.slot);
        if (e.mods.isAltDown())
            st.ratchet = static_cast<uint8_t> (ops::nextRatchet (st.ratchet, dir));
        else if (e.mods.isCtrlDown())
        {
            if (sliced)
                st.slice = static_cast<int8_t> (juce::jlimit (-1, juce::jmin (63, numSlices - 1), st.slice + dir));
            else
                st.pitch = static_cast<int8_t> (juce::jlimit (-24, 24, st.pitch + dir));
        }
        else if (e.mods.isShiftDown())
            st.probability = static_cast<uint8_t> (juce::jlimit (0, 100, st.probability + 5 * dir));
        else
            st.velocity = static_cast<uint8_t> (juce::jlimit (1, 127, st.velocity + 8 * dir));
    });
}

void StepGrid::paint (juce::Graphics& g)
{
    g.fillAll (palette::background);

    const auto& pat = c_.editPattern();
    const auto& snap = c_.snapshot();
    const bool playingThis = c_.transport().playing && snap.currentPattern == c_.editPatternIndex();
    const int page = c_.gridPage();
    const auto hold = c_.plockTarget();

    for (int row = 0; row < kNumTracks; ++row)
    {
        const auto& lane = pat.lanes[static_cast<std::size_t> (row)];
        const bool sliced = c_.trackSliced (row);

        if (row == c_.selectedTrack())
        {
            auto rowRect = cellBounds (row, 0).getUnion (cellBounds (row, 15)).expanded (2.f, 2.f);
            g.setColour (palette::neonCyan.withAlpha (0.25f));
            g.drawRect (rowRect, 1.f);
        }

        for (int col = 0; col < 16; ++col)
        {
            const int slot = page * 16 + col;
            const auto r = cellBounds (row, col);

            if (slot >= lane.length)
            {
                g.setColour (palette::background.brighter (0.03f));
                g.fillRect (r);
                continue;
            }

            const auto& st = lane.steps[static_cast<std::size_t> (slot)];
            if (st.active)
            {
                g.setColour (palette::neonCyan.withAlpha (0.35f + 0.65f * static_cast<float> (st.velocity) / 127.f));
                g.fillRoundedRectangle (r, 2.f);
            }
            else
            {
                g.setColour (palette::ledOff);
                g.fillRoundedRectangle (r, 2.f);
                g.setColour (palette::gridLine);
                g.drawRoundedRectangle (r, 2.f, 1.f);
            }

            if (playingThis && snap.laneSlot[static_cast<std::size_t> (row)] == slot)
            {
                g.setColour (palette::nixie);
                g.drawRoundedRectangle (r.reduced (1.f), 2.f, 2.f);
            }

            if (hold && hold->track == row && hold->slot == slot)
            {
                g.setColour (palette::magenta);
                g.drawRoundedRectangle (r.reduced (1.f), 2.f, 2.f);
            }

            if (! st.active)
                continue;

            g.setFont (monoFont (10.f));
            g.setColour (palette::background);
            const auto inner = r.reduced (3.f, 2.f).toNearestInt();
            if (st.ratchet > 1)
                g.drawText ("x" + juce::String (st.ratchet), inner, juce::Justification::topLeft, false);
            if (st.condition != StepCondition::Always)
                g.drawText (conditionLabel (st.condition), inner, juce::Justification::topRight, false);
            else if (st.probability < 100)
                g.drawText (juce::String (st.probability) + "%", inner, juce::Justification::topRight, false);
            if (sliced)
            {
                juce::String lab = st.slice < 0 ? juce::String() : "S" + juce::String (st.slice + 1);
                if (st.reverse)
                    lab = "<" + lab;
                if (lab.isNotEmpty())
                    g.drawText (lab, inner, juce::Justification::bottomLeft, false);
            }
            if (st.nudge != 0)
                g.drawText ((st.nudge > 0 ? "+" : "") + juce::String (st.nudge), inner, juce::Justification::centredBottom, false);

            bool locked = false;
            for (const auto& pl : st.plocks)
                locked = locked || pl.param != kNoPLock;
            if (locked)
            {
                g.setColour (palette::magenta);
                g.fillEllipse (r.getRight() - 8.f, r.getBottom() - 8.f, 5.f, 5.f);
            }
        }
    }
}

} // namespace jackal
