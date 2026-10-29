#include "Mutation.h"

#include "Engine/StepHash.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace jackal
{

void mutate (Pattern& p, const MutationInput& in, float strength, uint64_t seed) noexcept
{
    const double s = std::clamp (static_cast<double> (strength), 0.0, 1.0);
    if (s <= 0.0)
        return;

    SplitMix64 rng (seed);
    constexpr std::array<int, 5> kRatchets { 2, 3, 4, 6, 8 };

    for (int t = 0; t < kNumTracks; ++t)
    {
        auto& lane = p.lanes[static_cast<std::size_t> (t)];
        const bool sliced = in.sliced[static_cast<std::size_t> (t)];
        const int numSlices = std::max (1, in.numSlices[static_cast<std::size_t> (t)]);
        const int len = std::clamp<int> (lane.length, 1, kMaxSteps);

        for (int i = 0; i < len; ++i)
        {
            Step& st = lane.steps[static_cast<std::size_t> (i)];
            if (! st.active)
                continue;

            const double u1 = rng.unit(), u2 = rng.unit(), u3 = rng.unit();
            const double u4 = rng.unit(), u5 = rng.unit(), u6 = rng.unit();

            if (const double th = 0.5 * s; u1 < th)
            {
                const auto bucket = std::min<std::size_t> (kRatchets.size() - 1, static_cast<std::size_t> (u1 / th * static_cast<double> (kRatchets.size())));
                st.ratchet = static_cast<uint8_t> (kRatchets[bucket]);
            }

            if (u2 < 0.25 * s && sliced)
                st.reverse = ! st.reverse;

            if (const double th = 0.6 * s; u3 < th && sliced)
                st.slice = static_cast<int8_t> (std::min (numSlices - 1, std::min (63, static_cast<int> (std::floor (u3 / th * numSlices)))));

            if (const double th = 0.2 * s; u4 < th)
                st.nudge = static_cast<int8_t> (std::clamp (static_cast<int> (std::lround ((u4 / th * 2.0 - 1.0) * 25.0)), -kMaxNudgePercent, kMaxNudgePercent));

            if (const double th = 0.15 * s; u5 < th)
                st.velocity = static_cast<uint8_t> (std::clamp (st.velocity + static_cast<int> (std::lround ((u5 / th * 2.0 - 1.0) * 30.0)), 1, 127));

            if (const double th = 0.1 * s; u6 < th)
                st.probability = static_cast<uint8_t> (std::clamp (50 + static_cast<int> (std::lround (u6 / th * 40.0)), 0, 100));
        }
    }
}

} // namespace jackal
