#pragma once

#include "../SampleBank.h"
#include "../Transport.h"

#include <cstdint>

namespace jackal
{

// Where a pattern's loop restarts: the pattern start beat and the bank slot of its loop.
struct LoopPatternInfo
{
    bool valid = false;
    double startBeat = 0.0;
    int64_t startSample = 0;
    int loopSlot = -1;
};

// Plays the loop of the current pattern inside the guitar strip.
// Stateless timing: the loop index for an output position q is computed from the musical position
// q - startOffset and the pattern's cycle grid, so a sample swapped in between restarts continues seamlessly.
class LoopPlayer
{
public:
    void prepare (double sampleRate) noexcept;
    void reset() noexcept;

    // Once per block, before render. `previous` covers the tail of the pattern that played before a switch.
    void update (bool playing, const LoopPatternInfo& current, const LoopPatternInfo& previous, int startOffset) noexcept;
    // `fresh` stands in for bank slot `freshSlot` (a loop finished in this very block, not swapped into the bank yet).
    void render (float* left, float* right, int n, int64_t blockStart, float gainDb, const Transport&, const SampleBank&,
                 int freshSlot = -1, const Sample* fresh = nullptr) noexcept;
    int64_t playIndex() const noexcept { return playIndex_; }

private:
    double sampleRate_ = 48000.0;
    float gainA_ = 0.f, gain_ = 1.f;
    bool gainInit_ = false;
    bool playing_ = false;
    LoopPatternInfo cur_ {}, prev_ {};
    int startOffset_ = 0;
    int64_t playIndex_ = -1;
};

} // namespace jackal
