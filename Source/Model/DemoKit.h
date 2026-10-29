#pragma once

#include "Project.h"

#include "Engine/Sample.h"

#include <memory>
#include <string_view>
#include <vector>

namespace jackal
{

// Built-in sounds are synthesized by code, deterministically. The repo ships no audio files.
std::unique_ptr<Sample> makeBuiltin (std::string_view name, double sampleRate);   // nullptr for unknown names
const std::vector<std::string_view>& builtinNames();

// Demo kit on slots 0-7 and patterns A (groove) and B (rolls). Heap: a Project is ~200 KB.
std::unique_ptr<Project> makeDemoProject (double sampleRate);

} // namespace jackal
