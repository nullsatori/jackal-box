#pragma once

#include "Project.h"

#include "Engine/Sample.h"

#include <cstdint>
#include <vector>

namespace jackal
{

std::vector<int64_t> sliceGrid (int64_t length, int n);                                      // n equal parts, last = length
std::vector<int64_t> sliceTransients (const float* mono, int64_t length, double sampleRate);  // <= 64 slices, first = 0
double guessSourceBars (double lengthSeconds);                                                // 1, 2, 4 or 8

// Fills sample.sliceStarts / sourceBars from the slot spec. Transient: uses the stored
// boundaries, or detects them (and writes them back into spec) when there are none.
void applySlicing (Sample& sample, SampleSlotSpec& spec);

} // namespace jackal
