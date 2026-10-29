#pragma once

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace jackal::keymap
{

// All key bindings in one place (contract stage 2, §9.6).
// Held keys (0 = fill, 1..8 = glitch pads) are handled in MainComponent::keyStateChanged.
enum CommandId : juce::CommandID
{
    togglePlay = 0x2001,
    toggleRec,
    stepBase = 0x2100,        // + 0..15: steps 1..16 of the selected row on the current page
    hitBase = 0x2200,         // + 0..7: live hit, velocity 100
    hitAccentBase = 0x2210,   // + 0..7: live hit with Shift, velocity 127
    patternBase = 0x2300,     // + 0..7: queue pattern A..H (stage 5: in SONG mode only picks it for editing)
    rowUp = 0x2400, rowDown, shiftLeft, shiftRight, nudgeLeft, nudgeRight, pageUp, pageDown,
    doubleLen, mutateCmd, undoCmd, redoCmd, redoAlt,
    saveCmd, saveAsCmd, openCmd, newCmd, closeOverlay, deletePLocks,
    nextTab,  // stage 3: Tab cycles the panel tabs (six since stage 4)
    // stage 4
    loopEnter = 0x2500, loopRec, loopUndo, browseTrack, browseGuitar, toggleEditorCmd,
    // stage 5 (§9.9)
    songModeCmd = 0x2600, followCmd, exportCmd, learnCmd, captureCmd, sampleUp, sampleDown,
    appendBase = 0x2700        // + 0..7: Shift+F1..F8 append A..H to the song
};

constexpr std::array<char, 16> kStepKeys { 'Q', 'W', 'E', 'R', 'T', 'Y', 'U', 'I', 'A', 'S', 'D', 'F', 'G', 'H', 'J', 'K' };
constexpr std::array<char, 8> kHitKeys { 'Z', 'X', 'C', 'V', 'B', 'N', 'M', ',' };
constexpr std::array<char, 8> kPadKeys { '1', '2', '3', '4', '5', '6', '7', '8' };
constexpr char kFillKey = '0';

} // namespace jackal::keymap
