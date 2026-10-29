#include "PatternOps.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace jackal::ops
{

namespace
{
    bool valid (int lane, int slot) noexcept
    {
        return lane >= 0 && lane < kNumTracks && slot >= 0 && slot < kMaxSteps;
    }

    Lane& laneRef (Pattern& p, int lane) noexcept { return p.lanes[static_cast<std::size_t> (lane)]; }

    constexpr std::array<int, 12> kRatchets { 1, 2, 3, 4, 6, 8, 12, 16, 24, 32, 48, 64 };
}

Step& step (Pattern& p, int lane, int slot) noexcept
{
    lane = std::clamp (lane, 0, kNumTracks - 1);
    slot = std::clamp (slot, 0, kMaxSteps - 1);
    return laneRef (p, lane).steps[static_cast<std::size_t> (slot)];
}

void toggleStep (Pattern& p, int lane, int slot) noexcept
{
    if (valid (lane, slot))
        step (p, lane, slot).active = ! step (p, lane, slot).active;
}

void setStepActive (Pattern& p, int lane, int from, int to, bool active) noexcept
{
    if (from > to)
        std::swap (from, to);
    for (int s = from; s <= to; ++s)
        if (valid (lane, s))
            step (p, lane, s).active = active;
}

void setVelocity (Pattern& p, int lane, int slot, int velocity) noexcept
{
    if (valid (lane, slot))
        step (p, lane, slot).velocity = static_cast<uint8_t> (std::clamp (velocity, 1, 127));
}

void rampVelocity (Pattern& p, int lane, int from, int to, int v0, int v1) noexcept
{
    if (from > to)
    {
        std::swap (from, to);
        std::swap (v0, v1);
    }
    const int span = to - from;
    for (int s = from; s <= to; ++s)
    {
        if (! valid (lane, s) || ! step (p, lane, s).active)
            continue;
        const double f = span == 0 ? 0.0 : static_cast<double> (s - from) / static_cast<double> (span);
        setVelocity (p, lane, s, static_cast<int> (std::lround (v0 + (v1 - v0) * f)));
    }
}

void setRatchet (Pattern& p, int lane, int slot, int ratchet) noexcept
{
    if (valid (lane, slot))
        step (p, lane, slot).ratchet = static_cast<uint8_t> (std::clamp (ratchet, 1, kMaxRatchet));
}

void setPitchOrSlice (Pattern& p, int lane, int slot, bool sliced, int value) noexcept
{
    if (! valid (lane, slot))
        return;
    if (sliced)
        step (p, lane, slot).slice = static_cast<int8_t> (std::clamp (value, -1, 63));
    else
        step (p, lane, slot).pitch = static_cast<int8_t> (std::clamp (value, -24, 24));
}

void setProbability (Pattern& p, int lane, int slot, int probability) noexcept
{
    if (valid (lane, slot))
        step (p, lane, slot).probability = static_cast<uint8_t> (std::clamp (probability, 0, 100));
}

void shiftLane (Pattern& p, int lane, int direction) noexcept
{
    if (lane < 0 || lane >= kNumTracks || direction == 0)
        return;
    Lane& l = laneRef (p, lane);
    const int len = std::clamp<int> (l.length, 1, kMaxSteps);
    auto first = l.steps.begin();
    if (direction > 0)
        std::rotate (first, first + (len - 1), first + len);
    else
        std::rotate (first, first + 1, first + len);
}

void nudgeLane (Pattern& p, int lane, int deltaPercent) noexcept
{
    if (lane < 0 || lane >= kNumTracks)
        return;
    for (auto& s : laneRef (p, lane).steps)
        s.nudge = static_cast<int8_t> (std::clamp (s.nudge + deltaPercent, -kMaxNudgePercent, kMaxNudgePercent));
}

void doubleLength (Pattern& p) noexcept
{
    if (p.bars >= kMaxBars)
        return;

    const int oldCells = std::clamp<int> (p.bars, 1, kMaxBars) * 16;
    p.bars = static_cast<uint8_t> (std::min (kMaxBars, p.bars * 2));
    const int newCells = p.bars * 16;
    for (int i = oldCells; i < newCells; ++i)
        p.glitch.pad[static_cast<std::size_t> (i)] = p.glitch.pad[static_cast<std::size_t> (i % oldCells)];

    for (auto& l : p.lanes)
    {
        const int len = std::clamp<int> (l.length, 1, kMaxSteps);
        const int doubled = std::min (kMaxSteps, 2 * len);
        for (int i = len; i < doubled; ++i)
            l.steps[static_cast<std::size_t> (i)] = l.steps[static_cast<std::size_t> (i - len)];
        l.length = static_cast<uint8_t> (doubled);
    }
}

void copyPattern (const Pattern& from, Pattern& to) noexcept
{
    to = from;
}

void setPLock (Pattern& p, int lane, int slot, uint8_t param, float value) noexcept
{
    if (! valid (lane, slot))
        return;
    auto& pl = step (p, lane, slot).plocks;

    for (auto& l : pl)
        if (l.param == param)
        {
            l.value = value;
            return;
        }

    for (auto& l : pl)
        if (l.param == kNoPLock)
        {
            l = { param, value };
            return;
        }

    // all four taken: drop the oldest (index 0), keep the list compact
    std::rotate (pl.begin(), pl.begin() + 1, pl.end());
    pl.back() = { param, value };
}

void clearPLocks (Pattern& p, int lane, int slot) noexcept
{
    if (valid (lane, slot))
        step (p, lane, slot).plocks = {};
}

int nextRatchet (int current, int direction) noexcept
{
    int idx = 0;
    for (std::size_t i = 0; i < kRatchets.size(); ++i)
        if (kRatchets[i] <= current)
            idx = static_cast<int> (i);
    idx = std::clamp (idx + (direction > 0 ? 1 : -1), 0, static_cast<int> (kRatchets.size()) - 1);
    return kRatchets[static_cast<std::size_t> (idx)];
}

StepCondition nextCondition (StepCondition c) noexcept
{
    switch (c)
    {
        case StepCondition::Always:     return StepCondition::Fill;
        case StepCondition::Fill:       return StepCondition::NotFill;
        case StepCondition::NotFill:    return StepCondition::FirstCycle;
        case StepCondition::FirstCycle: return StepCondition::Cycle1of2;
        case StepCondition::Cycle1of2:  return StepCondition::Cycle2of2;
        case StepCondition::Cycle2of2:  return StepCondition::Always;
    }
    return StepCondition::Always;
}

bool isEmpty (const Pattern& p) noexcept
{
    for (const auto& l : p.lanes)
        for (const auto& s : l.steps)
            if (s.active)
                return false;
    for (auto v : p.glitch.pad)
        if (v != 0)
            return false;
    return true;
}

} // namespace jackal::ops
