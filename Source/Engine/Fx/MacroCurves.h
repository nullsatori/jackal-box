#pragma once

#include "FxParams.h"

namespace jackal::MacroCurves
{

// m in [0, 1]. Effects below their threshold come back as kNeutral.
FxState resolve (ShakalPreset, float m) noexcept;

} // namespace jackal::MacroCurves
