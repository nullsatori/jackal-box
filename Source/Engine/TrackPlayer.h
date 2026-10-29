#pragma once

#include "Voice.h"

#include <array>
#include <cstdint>
#include <vector>

namespace jackal
{

// One track: two voices (the second only carries the 64-sample declick tail of a retrigger).
// Triggers and chokes are queued with block offsets and applied in render().
class TrackPlayer
{
public:
    static constexpr int kMaxEventsPerBlock = 128;
    static constexpr int kDeclickSamples = 64;

    void prepare (double sampleRate, int maxBlockSize);
    void reset() noexcept;                                        // kill voices, drop queued events

    bool trigger (int offset, const TriggerParams&) noexcept;    // false = event list full (dropped)
    bool choke (int offset) noexcept;                             // release(64) at offset
    void killSample (const Sample*) noexcept;                     // immediate kill of voices on this sample
    void render (float* left, float* right, int numSamples) noexcept;
    bool hitThisBlock() const noexcept { return hit_; }
    bool anyActive() const noexcept { return voices_[0].active() || voices_[1].active(); }

private:
    struct Event { int offset = 0; bool isChoke = false; TriggerParams params {}; };

    void applyEvent (const Event&) noexcept;

    std::array<Voice, 2> voices_ {};
    int current_ = 0;
    std::vector<Event> events_;   // capacity reserved in prepare(), never grows on the audio thread
    int numEvents_ = 0;
    bool hit_ = false;
};

} // namespace jackal
