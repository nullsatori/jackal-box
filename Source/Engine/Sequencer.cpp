#include "Sequencer.h"

#include "StepHash.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace jackal
{

namespace
{
    const Pattern kEmptyPattern {};

    template <typename T>
    T& at (std::array<T, kNumTracks>& a, int i) noexcept { return a[static_cast<std::size_t> (i)]; }

    template <typename T>
    const T& at (const std::array<T, kNumTracks>& a, int i) noexcept { return a[static_cast<std::size_t> (i)]; }

    int laneLength (const Lane& lane) noexcept { return std::clamp<int> (lane.length, 1, kMaxSteps); }

    bool conditionPasses (StepCondition c, int64_t cycle, bool fill) noexcept
    {
        switch (c)
        {
            case StepCondition::Always:     return true;
            case StepCondition::Fill:       return fill;
            case StepCondition::NotFill:    return ! fill;
            case StepCondition::FirstCycle: return cycle == 0;
            case StepCondition::Cycle1of2:  return cycle % 2 == 0;
            case StepCondition::Cycle2of2:  return cycle % 2 == 1;
        }
        return true;
    }
} // namespace

void Sequencer::prepare (double, int)
{
    reset();
}

void Sequencer::reset() noexcept
{
    pendingPattern_ = -1;
    patternStartBeat_ = 0.0;
    nextK_.fill (0);
    lastSlot_.fill (-1);
    for (auto& r : rolls_)
        r.active = false;
    nextGlitchStep_ = 0;
    currentGlitchPad_ = 0;
    glitchSlot_ = -1;
}

const Pattern* Sequencer::patternAt (int index) const noexcept
{
    if (index < 0 || index >= kNumPatterns)
        return nullptr;
    return patterns_[static_cast<std::size_t> (index)].load (std::memory_order_acquire);
}

const Pattern& Sequencer::current() const noexcept
{
    const Pattern* p = patternAt (currentPattern_);
    return p != nullptr ? *p : kEmptyPattern;
}

void Sequencer::setPattern (int index, const Pattern* next, const Transport& transport) noexcept
{
    if (index < 0 || index >= kNumPatterns)
        return;

    if (index == currentPattern_ && transport.isPlaying())
    {
        const Pattern& before = current();
        const Pattern& after = next != nullptr ? *next : kEmptyPattern;
        resyncLanes (transport, before, after);
    }

    patterns_[static_cast<std::size_t> (index)].store (next, std::memory_order_release);
}

void Sequencer::resyncLanes (const Transport& transport, const Pattern& before, const Pattern& after) noexcept
{
    // A speed change would move step k to a different time; keep the playhead where it is.
    const double nowBeat = transport.beatAtSample (transport.positionSamples());
    for (int t = 0; t < kNumTracks; ++t)
    {
        const auto& lb = before.lanes[static_cast<std::size_t> (t)];
        const auto& la = after.lanes[static_cast<std::size_t> (t)];
        if (lb.speed == la.speed)
            continue;
        const double sb = stepBeats (la.speed);
        const double k = std::ceil ((nowBeat - patternStartBeat_) / sb - 1e-9);
        at (nextK_, t) = std::max<int64_t> (0, static_cast<int64_t> (k));
    }
}

void Sequencer::queuePattern (int index, bool playing) noexcept
{
    if (index < 0 || index >= kNumPatterns)
        return;

    if (playing)
    {
        pendingPattern_ = index;
        return;
    }

    currentPattern_ = index;
    pendingPattern_ = -1;
    patternStartBeat_ = 0.0;
    nextK_.fill (0);
    for (auto& r : rolls_)
        r.active = false;
    nextGlitchStep_ = 0;
}

void Sequencer::killSample (const Sample* s) noexcept
{
    for (auto& r : rolls_)
        if (r.active && r.base.sample == s)
            r.active = false;
}

int64_t Sequencer::onsetSample (const Transport& transport, const Pattern& p, int track, int64_t k) const noexcept
{
    const Lane& lane = p.lanes[static_cast<std::size_t> (track)];
    const double sb = stepBeats (lane.speed);
    const int len = laneLength (lane);
    const Step& step = lane.steps[static_cast<std::size_t> (k % len)];

    const int swing = std::clamp<int> (p.swing, kMinSwing, kMaxSwing);
    const double swingBeats = (k % 2 == 1 && lane.speed != LaneSpeed::Triplet)
                                  ? static_cast<double> (swing - 50) / 50.0 * sb : 0.0;
    const int nudge = std::clamp<int> (step.nudge, -kMaxNudgePercent, kMaxNudgePercent);
    const double nudgeBeats = static_cast<double> (nudge) / 100.0 * sb;

    return transport.sampleAtBeat (patternStartBeat_ + static_cast<double> (k) * sb + swingBeats + nudgeBeats);
}

bool Sequencer::buildTrigger (const SequencerContext& ctx, int track, const Step& step, int slot, TriggerParams& out) const noexcept
{
    const TrackState& ts = at (ctx.tracks, track);
    const Sample* s = ctx.bank.get (ts.slot);
    if (s == nullptr || s->length <= 0)
        return false;

    std::array<float, kNumTrackParams> v {};
    for (int p = 0; p < kNumTrackParams; ++p)
        v[static_cast<std::size_t> (p)] = ctx.params.get (trackParam (track, static_cast<TrackParam> (p)));

    for (const auto& pl : step.plocks)
        if (pl.param < kNumTrackParams)
            v[pl.param] = pl.value;

    auto val = [&v] (TrackParam p) { return v[static_cast<std::size_t> (p)]; };

    out = {};
    out.sample = s;
    out.reverse = step.reverse;
    out.gain = static_cast<float> (std::clamp<int> (step.velocity, 1, 127)) / 127.f;
    out.pitchSemitones = val (TrackParam::Pitch) + static_cast<float> (step.pitch);
    out.decayMs = val (TrackParam::Decay);
    out.toneHz = val (TrackParam::Tone);
    out.pan = val (TrackParam::Pan);
    out.volumeDb = val (TrackParam::Volume);
    out.attackSamples = 32;

    const int ns = s->numSlices();
    if (ns > 0)
    {
        const int i = step.slice < 0 ? (slot < 0 ? 0 : slot) % ns : std::min<int> (step.slice, ns - 1);
        out.startSample = s->sliceStarts[static_cast<std::size_t> (i)];
        out.endSample = s->sliceStarts[static_cast<std::size_t> (i + 1)];
    }
    else
    {
        const double len = static_cast<double> (s->length);
        const double st = std::clamp (static_cast<double> (val (TrackParam::Start)), 0.0, 1.0);
        const double en = std::clamp (static_cast<double> (val (TrackParam::End)), 0.0, 1.0);
        out.startSample = static_cast<int64_t> (std::floor (st * len));
        out.endSample = en <= st ? s->length : static_cast<int64_t> (std::floor (en * len));
    }

    out.rateFit = 1.0;
    if (ts.fit && s->sourceBars > 0.0)
        out.rateFit = (static_cast<double> (s->length) / s->sampleRate) * ctx.transport.bpm() / (s->sourceBars * 4.0 * 60.0);

    return true;
}

void Sequencer::fire (SequencerContext& ctx, int track, int offset, const TriggerParams& params) noexcept
{
    const uint8_t group = at (ctx.tracks, track).choke;
    if (group != 0)
        for (int u = 0; u < kNumTracks; ++u)
            if (u != track && at (ctx.tracks, u).choke == group)
                if (! at (ctx.players, u).choke (offset))
                    ++ctx.dropped;

    if (! at (ctx.players, track).trigger (offset, params))
        ++ctx.dropped;
}

void Sequencer::fireRollHit (SequencerContext& ctx, int track, int64_t atSample) noexcept
{
    RollState& roll = at (rolls_, track);
    const float f = roll.r > 1 ? static_cast<float> (roll.j) / static_cast<float> (roll.r - 1) : 0.f;

    TriggerParams p = roll.base;
    p.gain = std::clamp (roll.gain0 * (1.f + roll.velRamp * f), 0.f, 1.f);
    p.pitchSemitones = roll.pitch0 + roll.pitchRamp * f;

    const int64_t off = std::max<int64_t> (0, atSample - ctx.blockStart);
    fire (ctx, track, static_cast<int> (off), p);

    if (++roll.j >= roll.r)
        roll.active = false;
}

void Sequencer::processRange (SequencerContext& ctx, int64_t r0, int64_t r1) noexcept
{
    const Pattern& pat = current();
    const auto& transport = ctx.transport;
    const int64_t b0 = ctx.blockStart;
    constexpr int64_t kNever = std::numeric_limits<int64_t>::max();

    for (int t = 0; t < kNumTracks; ++t)
    {
        const Lane& lane = pat.lanes[static_cast<std::size_t> (t)];
        const int len = laneLength (lane);
        RollState& roll = at (rolls_, t);
        int64_t& k = at (nextK_, t);

        for (;;)
        {
            const int64_t stepAt = onsetSample (transport, pat, t, k);
            const int64_t rollAt = roll.active ? roll.onset + std::llround (roll.j * roll.stepSamples / roll.r) : kNever;

            if (rollAt < r1 && rollAt < stepAt)
            {
                fireRollHit (ctx, t, std::max (rollAt, r0));
                continue;
            }

            if (stepAt >= r1)
                break;

            const int slot = static_cast<int> (k % len);
            const int64_t cycle = k / len;
            at (lastSlot_, t) = slot;
            const Step& step = lane.steps[static_cast<std::size_t> (slot)];
            ++k;

            // far behind (a pattern arrived late): skip silently; a sample or two behind: play now
            if (stepAt < r0 && r0 - stepAt > ctx.numSamples)
                continue;

            if (! step.active || ! at (ctx.audible, t))
                continue;
            if (! conditionPasses (step.condition, cycle, ctx.fillHeld))
                continue;
            if (step.probability < 100
                && ! (StepHash::unit (ctx.seed, currentPattern_, t, k - 1) * 100.0 < static_cast<double> (step.probability)))
                continue;

            TriggerParams params;
            if (! buildTrigger (ctx, t, step, slot, params))
                continue;

            const int64_t hitAt = std::max (stepAt, r0);
            ctx.events.push ({ EngineEventType::TrackHit, static_cast<uint8_t> (t), static_cast<uint8_t> (slot), step.velocity });

            const int r = std::clamp<int> (step.ratchet, 1, kMaxRatchet);
            if (r == 1)
            {
                roll.active = false;
                fire (ctx, t, static_cast<int> (hitAt - b0), params);
                continue;
            }

            roll.active = true;
            roll.r = r;
            roll.j = 0;
            roll.onset = hitAt;
            roll.stepSamples = stepBeats (lane.speed) * transport.samplesPerBeat();
            roll.base = params;
            roll.gain0 = params.gain;
            roll.velRamp = static_cast<float> (std::clamp<int> (step.ratchetVelRamp, -100, 100)) / 100.f;
            roll.pitch0 = params.pitchSemitones;
            roll.pitchRamp = static_cast<float> (std::clamp<int> (step.ratchetPitchRamp, -24, 24));
            fireRollHit (ctx, t, hitAt);
        }
    }

    // glitch lane
    const int cells = std::clamp<int> (pat.bars, 1, kMaxBars) * 16;
    for (;;)
    {
        const int64_t cellAt = transport.sampleAtBeat (patternStartBeat_ + static_cast<double> (nextGlitchStep_) / 4.0);
        if (cellAt >= r1)
            break;

        const int cell = static_cast<int> (nextGlitchStep_ % cells);
        const int value = std::min<int> (pat.glitch.pad[static_cast<std::size_t> (cell)], kNumPads);
        const int off = static_cast<int> (std::max (cellAt, r0) - b0);
        glitchSlot_ = cell;
        ++nextGlitchStep_;

        if (manualPad_ >= 0)
        {
            if (ctx.recording)
                ctx.events.push ({ EngineEventType::GlitchRecorded, 0, static_cast<uint8_t> (cell), static_cast<uint8_t> (manualPad_ + 1) });
            continue;
        }

        if (value != currentGlitchPad_)
        {
            if (currentGlitchPad_ > 0)
                ctx.glitch.padUp (off, currentGlitchPad_ - 1);
            if (value > 0)
                ctx.glitch.padDown (off, value - 1, transport, b0);
            currentGlitchPad_ = value;
        }
    }
}

void Sequencer::switchPattern (SequencerContext& ctx, double switchBeat, int64_t switchSample) noexcept
{
    if (currentGlitchPad_ > 0 && manualPad_ < 0)
        ctx.glitch.padUp (static_cast<int> (switchSample - ctx.blockStart), currentGlitchPad_ - 1);
    currentGlitchPad_ = 0;

    currentPattern_ = pendingPattern_;
    pendingPattern_ = -1;
    patternStartBeat_ = switchBeat;
    nextK_.fill (0);
    nextGlitchStep_ = 0;
    for (auto& r : rolls_)
        r.active = false;
}

void Sequencer::process (SequencerContext& ctx) noexcept
{
    const auto& transport = ctx.transport;
    playing_ = transport.isPlaying();

    if (! playing_)
    {
        lastSlot_.fill (-1);
        glitchSlot_ = -1;
        return;
    }

    const int64_t b0 = ctx.blockStart, b1 = b0 + ctx.numSamples;

    if (pendingPattern_ >= 0)
    {
        double bar = std::floor (transport.beatAtSample (b0) / kBeatsPerBar) * kBeatsPerBar;
        int64_t barAt = transport.sampleAtBeat (bar);
        while (barAt < b0)
        {
            bar += kBeatsPerBar;
            barAt = transport.sampleAtBeat (bar);
        }

        if (barAt < b1)
        {
            processRange (ctx, b0, barAt);
            switchPattern (ctx, bar, barAt);
            processRange (ctx, barAt, b1);
            return;
        }
    }

    processRange (ctx, b0, b1);
}

void Sequencer::liveHit (SequencerContext& ctx, int track, uint8_t velocity) noexcept
{
    if (track < 0 || track >= kNumTracks)
        return;

    const bool playing = ctx.transport.isPlaying();
    const Lane& lane = current().lanes[static_cast<std::size_t> (track)];
    const int len = laneLength (lane);

    Step step {};
    step.active = true;
    step.velocity = static_cast<uint8_t> (std::clamp<int> (velocity, 1, 127));

    const int playSlot = playing ? std::max (0, at (lastSlot_, track)) : 0;

    TriggerParams params;
    if (buildTrigger (ctx, track, step, playSlot, params))
    {
        fire (ctx, track, 0, params);
        ctx.events.push ({ EngineEventType::TrackHit, static_cast<uint8_t> (track), static_cast<uint8_t> (playSlot), step.velocity });
    }

    if (ctx.recording && playing)
    {
        const double beat = ctx.transport.beatAtSample (ctx.blockStart);
        const double kf = (beat - patternStartBeat_) / stepBeats (lane.speed);
        const auto k = static_cast<int64_t> (std::llround (kf));
        const int slot = static_cast<int> (((k % len) + len) % len);
        ctx.events.push ({ EngineEventType::StepRecorded, static_cast<uint8_t> (track), static_cast<uint8_t> (slot), step.velocity });
    }
}

} // namespace jackal
