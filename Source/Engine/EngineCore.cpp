#include "EngineCore.h"

#include <algorithm>
#include <cmath>

namespace jackal
{

namespace
{
    float dbToGain (float db) noexcept
    {
        return db <= -60.f ? 0.f : std::pow (10.f, db / 20.f);
    }
} // namespace

void EngineCore::prepare (double sampleRate, int maxBlockSize, int /*numInputs*/, int /*numOutputs*/)
{
    maxBlock_ = std::max (1, maxBlockSize);
    transport_.prepare (sampleRate);
    metronome_.prepare (sampleRate, maxBlock_);
    monitor_.prepare (sampleRate);
    sequencer_.prepare (sampleRate, maxBlock_);
    glitch_.prepare (sampleRate, maxBlock_);
    for (auto& p : players_)
        p.prepare (sampleRate, maxBlock_);
    for (int c = 0; c < kNumChains; ++c)
        chains_[static_cast<std::size_t> (c)].prepare (sampleRate, maxBlock_, c);
    for (auto& b : buses_)
        b.assign (static_cast<std::size_t> (maxBlock_), 0.f);
    for (auto& m : master_)
        m.assign (static_cast<std::size_t> (maxBlock_), 0.f);
    inputMeter_.reset();
    outputMeter_.reset();
    publishSnapshot();
}

void EngineCore::releaseAll() noexcept
{
    for (int i = 0; i < kNumPatterns; ++i)
        if (const Pattern* p = sequencer_.patternAt (i))
            if (retired_.push ({ Retired::Kind::Pattern, p }))
                sequencer_.setPattern (i, nullptr, transport_);

    for (auto& pl : players_)
        pl.reset();

    for (int s = 0; s < kNumSampleSlots; ++s)
        if (const Sample* old = bank_.get (s))
            if (retired_.push ({ Retired::Kind::Sample, old }))
                bank_.swap (s, nullptr);
}

void EngineCore::applyCommand (const Command& c, SequencerContext& ctx) noexcept
{
    const int lo = c.ivalue & 0xFF;
    const int hi = (c.ivalue >> 8) & 0xFF;

    switch (c.type)
    {
        case CommandType::Play:
            transport_.play();
            sequencer_.reset();
            glitch_.reset();
            metronome_.reset();
            for (auto& ch : chains_)
                ch.reset();
            break;

        case CommandType::Stop:
            transport_.stop();
            sequencer_.reset();
            glitch_.reset();
            metronome_.reset();
            for (auto& ch : chains_)
                ch.reset();
            break;

        case CommandType::SetBpm:          transport_.setBpm (static_cast<double> (c.value)); break;
        case CommandType::SetInputChannel: monitor_.setInputChannel (c.ivalue); break;

        case CommandType::SetPattern:
        {
            const Pattern* old = sequencer_.patternAt (c.ivalue);
            if (old != nullptr && ! retired_.push ({ Retired::Kind::Pattern, old }))
            {
                ++dropped_;
                break;
            }
            sequencer_.setPattern (c.ivalue, static_cast<const Pattern*> (c.ptr), transport_);
            break;
        }

        case CommandType::QueuePattern:
            sequencer_.queuePattern (c.ivalue, transport_.isPlaying());
            break;

        case CommandType::SetSlotSample:
        {
            if (c.ivalue < 0 || c.ivalue >= kNumSampleSlots)
                break;
            const Sample* old = bank_.get (c.ivalue);
            if (old != nullptr && ! retired_.push ({ Retired::Kind::Sample, old }))
            {
                ++dropped_;
                break;
            }
            if (old != nullptr)
            {
                for (auto& p : players_)
                    p.killSample (old);
                sequencer_.killSample (old);
            }
            bank_.swap (c.ivalue, static_cast<const Sample*> (c.ptr));
            break;
        }

        case CommandType::SetTrackSlot:
            if (lo < kNumTracks)
                tracks_[static_cast<std::size_t> (lo)].slot = hi == 0xFF ? int8_t (-1) : static_cast<int8_t> (std::min (hi, kNumSampleSlots - 1));
            break;

        case CommandType::SetTrackChoke:
            if (lo < kNumTracks)
                tracks_[static_cast<std::size_t> (lo)].choke = static_cast<uint8_t> (std::min (hi, 4));
            break;

        case CommandType::SetTrackFit:
            if (lo < kNumTracks)
                tracks_[static_cast<std::size_t> (lo)].fit = hi != 0;
            break;

        case CommandType::TriggerTrack:
            sequencer_.liveHit (ctx, c.ivalue, static_cast<uint8_t> (std::clamp (static_cast<int> (c.value), 1, 127)));
            break;

        case CommandType::PadDown:
            if (c.ivalue >= 0 && c.ivalue < kNumPads)
            {
                sequencer_.setManualPad (c.ivalue);
                glitch_.padDown (0, c.ivalue, transport_, transport_.positionSamples());
            }
            break;

        case CommandType::PadUp:
            sequencer_.setManualPad (-1);
            glitch_.padUp (0, c.ivalue);
            break;

        case CommandType::SetRecording:
            recording_ = c.ivalue != 0;
            ctx.recording = recording_;
            break;

        case CommandType::SetSeed:
            seed_ = static_cast<uint32_t> (c.ivalue);
            ctx.seed = seed_;
            break;
    }
}

void EngineCore::process (const float* const* in, int numIn, float* const* out, int numOut, int numSamples) noexcept
{
    // Hosts may hand us a bigger block than announced: split, never overrun the buses.
    int done = 0;
    while (done < numSamples)
    {
        const int n = std::min (maxBlock_, numSamples - done);

        const float* inPtrs[32] {};
        float* outPtrs[32] {};
        const int ni = std::min (numIn, 32), no = std::min (numOut, 32);
        for (int c = 0; c < ni; ++c)
            inPtrs[c] = (in != nullptr && in[c] != nullptr) ? in[c] + done : nullptr;
        for (int c = 0; c < no; ++c)
            outPtrs[c] = (out != nullptr && out[c] != nullptr) ? out[c] + done : nullptr;

        processBlock (in != nullptr ? inPtrs : nullptr, ni, outPtrs, no, n);
        done += n;
    }
}

void EngineCore::processBlock (const float* const* in, int numIn, float* const* out, int numOut, int n) noexcept
{
    std::array<bool, kNumTracks> audible {};
    audible.fill (true);

    SequencerContext ctx { transport_, transport_.positionSamples(), n, params_.params, bank_, tracks_, audible,
                           params_.fillHeld.load (std::memory_order_relaxed), seed_, recording_,
                           players_, glitch_, events_, dropped_ };

    // (1) commands
    Command command;
    while (commands_.pop (command))
    {
        ctx.blockStart = transport_.positionSamples();
        applyCommand (command, ctx);
    }
    ctx.blockStart = transport_.positionSamples();

    // (2) MIDI: NoteOn -> live hit (note % 12 = track 0..7), everything else ignored
    MidiEvent midi;
    while (midi_.pop (midi))
    {
        const int type = midi.status & 0xF0;
        if (type == 0x90 && midi.data2 > 0)
        {
            const int track = midi.data1 % 12;
            if (track < kNumTracks)
                sequencer_.liveHit (ctx, track, midi.data2);
        }
    }

    // (3) continuous params, read once
    const float monitorGain = dbToGain (params_.monitorGainDb.load (std::memory_order_relaxed));
    const bool monitorEnabled = params_.monitorEnabled.load (std::memory_order_relaxed);
    const bool metronomeEnabled = params_.metronomeEnabled.load (std::memory_order_relaxed);
    const float metronomeGain = params_.metronomeGain.load (std::memory_order_relaxed);
    const float masterGain = std::pow (10.f, params_.params.get (masterParam (MasterParam::Gain)) / 20.f);

    bool anySolo = false;
    for (int t = 0; t < kNumTracks; ++t)
        anySolo = anySolo || params_.trackSolo[static_cast<std::size_t> (t)].load (std::memory_order_relaxed);
    for (int t = 0; t < kNumTracks; ++t)
    {
        const auto i = static_cast<std::size_t> (t);
        const bool mute = params_.trackMute[i].load (std::memory_order_relaxed);
        const bool solo = params_.trackSolo[i].load (std::memory_order_relaxed);
        audible[i] = anySolo ? solo : ! mute;
    }

    // (4) clear out, buses, master
    for (int ch = 0; ch < numOut; ++ch)
        if (out[ch] != nullptr)
            std::fill (out[ch], out[ch] + n, 0.f);
    for (auto& b : buses_)
        std::fill (b.begin(), b.begin() + n, 0.f);
    for (auto& m : master_)
        std::fill (m.begin(), m.begin() + n, 0.f);

    float* masterPtrs[2] = { master_[0].data(), master_[1].data() };

    // (5) raw input meter (selected channel, before gain)
    const int inputChannel = monitor_.inputChannel();
    if (in != nullptr && inputChannel >= 0 && inputChannel < numIn && in[inputChannel] != nullptr)
        inputMeter_.update (in + inputChannel, 1, n);
    else
        inputMeter_.update (nullptr, 0, n);

    // (6) sequencer -> track players, glitch pad events
    sequencer_.process (ctx);

    // (7) tracks render into their buses
    for (int t = 0; t < kNumTracks; ++t)
        players_[static_cast<std::size_t> (t)].render (buses_[static_cast<std::size_t> (2 * t)].data(),
                                                       buses_[static_cast<std::size_t> (2 * t + 1)].data(), n);

    // (7a) SHAKAL per track (stage 4 seam: VST3 track slots go right after this, on the same bus)
    const bool playing = transport_.isPlaying();
    if (! fxDisabled_)
        for (int t = 0; t < kNumTracks; ++t)
        {
            auto& chain = chains_[static_cast<std::size_t> (t)];
            for (const auto& mark : players_[static_cast<std::size_t> (t)].triggersThisBlock())
                chain.onTrigger (mark.offset, mark.shakalMacro);
            chain.process (buses_[static_cast<std::size_t> (2 * t)].data(), buses_[static_cast<std::size_t> (2 * t + 1)].data(), n,
                           params_.params, transport_, ctx.blockStart, playing, seed_);
        }

    // (8) sum buses into master
    for (int t = 0; t < kNumTracks; ++t)
    {
        const float* l = buses_[static_cast<std::size_t> (2 * t)].data();
        const float* r = buses_[static_cast<std::size_t> (2 * t + 1)].data();
        for (int i = 0; i < n; ++i)
        {
            masterPtrs[0][i] += l[i];
            masterPtrs[1][i] += r[i];
        }
    }

    // (9) glitch pads, (9a) master SHAKAL, then master gain (stage 4 seam: master VST3 slots before the gain)
    glitch_.process (masterPtrs[0], masterPtrs[1], n, transport_, ctx.blockStart);
    if (! fxDisabled_)
        chains_[static_cast<std::size_t> (kMasterChain)].process (masterPtrs[0], masterPtrs[1], n, params_.params, transport_,
                                                                  ctx.blockStart, playing, seed_);
    for (int i = 0; i < n; ++i)
    {
        masterPtrs[0][i] *= masterGain;
        masterPtrs[1][i] *= masterGain;
    }

    // (10) guitar monitor into master (not glitched)
    monitor_.process (in, numIn, masterPtrs, 2, n, monitorGain, monitorEnabled);

    // (11) metronome
    if (transport_.isPlaying() && metronomeEnabled)
        metronome_.process (transport_, transport_.positionSamples(), masterPtrs, 2, n, metronomeGain);

    // (12) master -> out
    if (numOut >= 2)
    {
        if (out[0] != nullptr) std::copy (masterPtrs[0], masterPtrs[0] + n, out[0]);
        if (out[1] != nullptr) std::copy (masterPtrs[1], masterPtrs[1] + n, out[1]);
    }
    else if (numOut == 1 && out[0] != nullptr)
    {
        for (int i = 0; i < n; ++i)
            out[0][i] = 0.5f * (masterPtrs[0][i] + masterPtrs[1][i]);
    }

    // (13) master meter
    outputMeter_.update (out, numOut, n);

    // (14) advance transport
    transport_.advance (n);

    // (15) snapshots and pad state events
    const int pad = glitch_.activePad();
    if (pad != lastPadState_)
    {
        events_.push ({ EngineEventType::PadState, 0, 0, static_cast<uint8_t> (pad + 1) });
        lastPadState_ = pad;
    }
    publishSnapshot();
}

void EngineCore::publishSnapshot() noexcept
{
    const auto s = transport_.state();
    snapPlaying_.store (s.playing, std::memory_order_relaxed);
    snapBpm_.store (s.bpm, std::memory_order_relaxed);
    snapPositionSamples_.store (s.positionSamples, std::memory_order_relaxed);
    snapPositionBeats_.store (s.positionBeats, std::memory_order_relaxed);

    snapCurrentPattern_.store (sequencer_.currentPattern(), std::memory_order_relaxed);
    snapPendingPattern_.store (sequencer_.pendingPattern(), std::memory_order_relaxed);
    for (int t = 0; t < kNumTracks; ++t)
        snapLaneSlot_[static_cast<std::size_t> (t)].store (s.playing ? sequencer_.laneSlot (t) : -1, std::memory_order_relaxed);
    snapGlitchSlot_.store (s.playing ? sequencer_.glitchSlot() : -1, std::memory_order_relaxed);
    snapActivePad_.store (glitch_.activePad(), std::memory_order_relaxed);
    snapPendingPad_.store (glitch_.pendingPad(), std::memory_order_relaxed);
    snapRecording_.store (recording_, std::memory_order_relaxed);
    snapDropped_.store (dropped_, std::memory_order_relaxed);

    uint16_t ov = 0;
    for (int c = 0; c < kNumChains; ++c)
    {
        const auto& ch = chains_[static_cast<std::size_t> (c)];
        snapMacro_[static_cast<std::size_t> (c)].store (ch.macroActual(), std::memory_order_relaxed);
        snapEngaged_[static_cast<std::size_t> (c)].store (ch.engagedMask(), std::memory_order_relaxed);
        if (ch.overrideActive())
            ov = static_cast<uint16_t> (ov | (1u << c));
    }
    snapOverride_.store (ov, std::memory_order_relaxed);
}

ShakalSnapshot EngineCore::shakalSnapshot() const noexcept
{
    ShakalSnapshot s;
    for (int c = 0; c < kNumChains; ++c)
    {
        s.macro[static_cast<std::size_t> (c)] = snapMacro_[static_cast<std::size_t> (c)].load (std::memory_order_relaxed);
        s.engaged[static_cast<std::size_t> (c)] = snapEngaged_[static_cast<std::size_t> (c)].load (std::memory_order_relaxed);
    }
    s.overrideMask = snapOverride_.load (std::memory_order_relaxed);
    return s;
}

TransportState EngineCore::transportSnapshot() const noexcept
{
    return { snapPlaying_.load (std::memory_order_relaxed),
             snapBpm_.load (std::memory_order_relaxed),
             snapPositionSamples_.load (std::memory_order_relaxed),
             snapPositionBeats_.load (std::memory_order_relaxed) };
}

SequencerSnapshot EngineCore::sequencerSnapshot() const noexcept
{
    SequencerSnapshot s;
    s.currentPattern = static_cast<int8_t> (snapCurrentPattern_.load (std::memory_order_relaxed));
    s.pendingPattern = static_cast<int8_t> (snapPendingPattern_.load (std::memory_order_relaxed));
    for (int t = 0; t < kNumTracks; ++t)
        s.laneSlot[static_cast<std::size_t> (t)] = static_cast<int16_t> (snapLaneSlot_[static_cast<std::size_t> (t)].load (std::memory_order_relaxed));
    s.glitchSlot = static_cast<int16_t> (snapGlitchSlot_.load (std::memory_order_relaxed));
    s.activePad = static_cast<int8_t> (snapActivePad_.load (std::memory_order_relaxed));
    s.pendingPad = static_cast<int8_t> (snapPendingPad_.load (std::memory_order_relaxed));
    s.recording = snapRecording_.load (std::memory_order_relaxed);
    s.droppedCommands = snapDropped_.load (std::memory_order_relaxed);
    return s;
}

} // namespace jackal
