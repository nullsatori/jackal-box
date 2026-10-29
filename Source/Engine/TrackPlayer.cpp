#include "TrackPlayer.h"

namespace jackal
{

void TrackPlayer::prepare (double sampleRate, int /*maxBlockSize*/)
{
    for (auto& v : voices_)
        v.prepare (sampleRate);
    events_.assign (static_cast<std::size_t> (kMaxEventsPerBlock), Event {});
    numEvents_ = 0;
    current_ = 0;
    hit_ = false;
}

void TrackPlayer::reset() noexcept
{
    for (auto& v : voices_)
        v.kill();
    numEvents_ = 0;
}

bool TrackPlayer::trigger (int offset, const TriggerParams& params) noexcept
{
    if (numEvents_ >= static_cast<int> (events_.size()))
        return false;

    auto& e = events_[static_cast<std::size_t> (numEvents_++)];
    e.offset = offset;
    e.isChoke = false;
    e.params = params;
    return true;
}

bool TrackPlayer::choke (int offset) noexcept
{
    if (numEvents_ >= static_cast<int> (events_.size()))
        return false;

    auto& e = events_[static_cast<std::size_t> (numEvents_++)];
    e.offset = offset;
    e.isChoke = true;
    return true;
}

void TrackPlayer::killSample (const Sample* sample) noexcept
{
    if (sample == nullptr)
        return;

    for (auto& v : voices_)
        if (v.sample() == sample)
            v.kill();

    // queued triggers that still point at the old sample must not start
    for (int i = 0; i < numEvents_; ++i)
        if (! events_[static_cast<std::size_t> (i)].isChoke && events_[static_cast<std::size_t> (i)].params.sample == sample)
            events_[static_cast<std::size_t> (i)].params.sample = nullptr;
}

void TrackPlayer::applyEvent (const Event& e) noexcept
{
    if (e.isChoke)
    {
        for (auto& v : voices_)
            v.release (kDeclickSamples);
        return;
    }

    auto& old = voices_[static_cast<std::size_t> (current_)];
    old.release (kDeclickSamples);

    current_ ^= 1;
    voices_[static_cast<std::size_t> (current_)].start (e.params);   // kills whatever tail was left there
    hit_ = true;
}

void TrackPlayer::render (float* left, float* right, int numSamples) noexcept
{
    hit_ = false;

    // stable insertion sort by offset: chokes from other tracks arrive out of order
    for (int i = 1; i < numEvents_; ++i)
    {
        const Event e = events_[static_cast<std::size_t> (i)];
        int j = i - 1;
        while (j >= 0 && events_[static_cast<std::size_t> (j)].offset > e.offset)
        {
            events_[static_cast<std::size_t> (j + 1)] = events_[static_cast<std::size_t> (j)];
            --j;
        }
        events_[static_cast<std::size_t> (j + 1)] = e;
    }

    int cursor = 0;
    for (int i = 0; i < numEvents_; ++i)
    {
        const auto& e = events_[static_cast<std::size_t> (i)];
        int at = e.offset < 0 ? 0 : (e.offset > numSamples ? numSamples : e.offset);

        if (at > cursor)
        {
            for (auto& v : voices_)
                v.render (left, right, cursor, at);
            cursor = at;
        }
        applyEvent (e);
    }

    if (cursor < numSamples)
        for (auto& v : voices_)
            v.render (left, right, cursor, numSamples);

    numEvents_ = 0;
}

} // namespace jackal
