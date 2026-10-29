#pragma once

#include "Engine/PatternData.h"

namespace jackal::ops
{

// Pure edits on a Pattern. Undo lives in the App (ProjectController).
Step& step (Pattern&, int lane, int slot) noexcept;
void toggleStep (Pattern&, int lane, int slot) noexcept;
void setStepActive (Pattern&, int lane, int from, int to, bool active) noexcept;   // inclusive range, any order
void setVelocity (Pattern&, int lane, int slot, int velocity) noexcept;
void rampVelocity (Pattern&, int lane, int from, int to, int v0, int v1) noexcept;   // active steps only
void setRatchet (Pattern&, int lane, int slot, int ratchet) noexcept;
void setPitchOrSlice (Pattern&, int lane, int slot, bool sliced, int value) noexcept;
void setProbability (Pattern&, int lane, int slot, int probability) noexcept;
void shiftLane (Pattern&, int lane, int direction) noexcept;     // cyclic inside length
void nudgeLane (Pattern&, int lane, int deltaPercent) noexcept;  // all steps of the lane
void doubleLength (Pattern&) noexcept;
void copyPattern (const Pattern& from, Pattern& to) noexcept;
void setPLock (Pattern&, int lane, int slot, uint8_t param, float value) noexcept;   // replaces same param, else first free, else oldest
void clearPLocks (Pattern&, int lane, int slot) noexcept;

int nextRatchet (int current, int direction) noexcept;   // 1,2,3,4,6,8,12,16,24,32,48,64
StepCondition nextCondition (StepCondition) noexcept;
bool isEmpty (const Pattern&) noexcept;

} // namespace jackal::ops
