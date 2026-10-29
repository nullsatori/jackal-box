#include "EngineCore.h"

#include <algorithm>
#include <cmath>

namespace jackal
{

void EngineCore::prepare (double sampleRate, int maxBlockSize, int /*numInputs*/, int /*numOutputs*/)
{
    maxBlock_ = std::max (1, maxBlockSize);
    transport_.prepare (sampleRate);
    metronome_.prepare (sampleRate, maxBlock_);
    strip_.prepare (sampleRate, maxBlock_, 0);
    recorder_.prepare (sampleRate, maxBlock_);
    loopPlayer_.prepare (sampleRate);
    calibrator_.prepare (sampleRate, maxBlock_);
    for (int c = 0; c < kNumChains; ++c)
        inserts_[static_cast<std::size_t> (c)].prepare (sampleRate, maxBlock_, c);
    pdc_.prepare (kMaxPdcSamples, maxBlock_);
    lastLoopInfo_ = {};
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

    LoopResult lr;
    recorder_.cancel (lr);
    if (lr.aborted != nullptr)
        retired_.push ({ Retired::Kind::Sample, lr.aborted });
    if (pendingRetire_ != nullptr && retired_.push ({ Retired::Kind::Sample, pendingRetire_ }))
        pendingRetire_ = nullptr;

    for (auto& chain : inserts_)
        for (int sl = 0; sl < kSlotsPerChain; ++sl)
            if (InsertProcessor* p = chain.slot (sl).get())
                if (retired_.push ({ Retired::Kind::Insert, p }))
                    chain.slot (sl).swap (nullptr);

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
            playResets();
            break;

        case CommandType::Stop:
            transport_.stop();
            playResets();
            break;

        case CommandType::SetBpm:
            if (recorder_.state() == LoopState::Idle)   // BPM is frozen while a loop is armed or recording
                transport_.setBpm (static_cast<double> (c.value));
            break;

        case CommandType::SetInputChannel: strip_.setInputChannel (c.ivalue); break;

        case CommandType::SetInsert:
        {
            if (lo >= kNumChains || hi >= kSlotsPerChain)
                break;
            auto& slot = inserts_[static_cast<std::size_t> (lo)].slot (hi);
            InsertProcessor* old = slot.get();
            if (old != nullptr && ! retired_.push ({ Retired::Kind::Insert, old }))
            {
                ++dropped_;
                break;
            }
            slot.swap (static_cast<InsertProcessor*> (const_cast<void*> (c.ptr)));
            break;
        }

        case CommandType::ArmLoop:
        {
            auto* target = static_cast<Sample*> (const_cast<void*> (c.ptr));
            if (recorder_.state() != LoopState::Idle || target == nullptr)
            {
                if (target != nullptr)
                    retireSample (target);
                break;
            }
            if (! transport_.isPlaying())
            {
                transport_.play();
                playResets();
            }
            const LoopArm arm { c.ivalue & 0xFF, (c.ivalue >> 8) & 0xFF, ((c.ivalue >> 16) & 0xFF) != 0, target };
            if (! recorder_.arm (arm, transport_, sequencer_, transport_.positionSamples(), bank_.get (arm.slot)))
                retireSample (target);
            break;
        }

        case CommandType::CancelLoop:
        {
            LoopResult r;
            recorder_.cancel (r);
            handleLoopResult (r);
            break;
        }

        case CommandType::StartCalibration:  calibrator_.start(); break;
        case CommandType::CancelCalibration: calibrator_.cancel(); break;

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

void EngineCore::playResets() noexcept
{
    sequencer_.reset();
    glitch_.reset();
    metronome_.reset();
    for (auto& ch : chains_)
        ch.reset();
    for (auto& ins : inserts_)
        ins.reset();
    pdc_.reset();
    strip_.reset();
    loopPlayer_.reset();
    lastLoopInfo_ = {};
    LoopResult r;
    recorder_.abort (r);
    handleLoopResult (r);
}

void EngineCore::retireSample (const Sample* s) noexcept
{
    if (s == nullptr)
        return;
    if (retired_.push ({ Retired::Kind::Sample, s }))
        return;
    // the queue is full: keep it for the next block (at most one can be pending, see contract stage 4 §5.5)
    if (pendingRetire_ == nullptr)
        pendingRetire_ = s;
    else
        ++dropped_;
}

void EngineCore::handleLoopResult (const LoopResult& r) noexcept
{
    if (r.finished != nullptr)
    {
        const int slot = recorder_.slot();
        const Sample* old = bank_.get (slot);
        if (old != nullptr)
        {
            for (auto& p : players_)
                p.killSample (old);
            sequencer_.killSample (old);
        }
        bank_.swap (slot, r.finished);
        retireSample (old);
        events_.push ({ EngineEventType::LoopRecorded, static_cast<uint8_t> (slot), 0, 0 });
    }
    if (r.aborted != nullptr)
    {
        retireSample (r.aborted);
        events_.push ({ EngineEventType::LoopAborted, static_cast<uint8_t> (recorder_.slot()), 0, 0 });
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
    const bool metronomeEnabled = params_.metronomeEnabled.load (std::memory_order_relaxed);
    const float metronomeGain = params_.metronomeGain.load (std::memory_order_relaxed);
    const float masterGain = std::pow (10.f, params_.params.get (masterParam (MasterParam::Gain)) / 20.f);
    const int32_t loopCompDevice = params_.loopCompSamples.load (std::memory_order_relaxed);

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

    // a retire that did not fit last block goes first
    if (pendingRetire_ != nullptr && retired_.push ({ Retired::Kind::Sample, pendingRetire_ }))
        pendingRetire_ = nullptr;

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
    const int inputChannel = strip_.inputChannel();
    const float* rawIn = (in != nullptr && inputChannel >= 0 && inputChannel < numIn) ? in[inputChannel] : nullptr;
    if (rawIn != nullptr)
        inputMeter_.update (in + inputChannel, 1, n);
    else
        inputMeter_.update (nullptr, 0, n);

    // (6) sequencer -> track players, glitch pad events
    sequencer_.process (ctx);

    // (7) tracks render into their buses
    for (int t = 0; t < kNumTracks; ++t)
        players_[static_cast<std::size_t> (t)].render (buses_[static_cast<std::size_t> (2 * t)].data(),
                                                       buses_[static_cast<std::size_t> (2 * t + 1)].data(), n);

    const bool playing = transport_.isPlaying();
    const InsertContext insertCtx { transport_.sampleRate(), transport_.bpm(), ctx.blockStart, playing };

    std::array<int, kNumTracks> trackLatency {};
    bool overflow = false;
    for (int t = 0; t < kNumTracks; ++t)
    {
        const auto i = static_cast<std::size_t> (t);
        float* l = buses_[2 * i].data();
        float* r = buses_[2 * i + 1].data();

        // (7a) SHAKAL
        auto& chain = chains_[i];
        if (! fxDisabled_)
        {
            for (const auto& mark : players_[i].triggersThisBlock())
                chain.onTrigger (mark.offset, mark.shakalMacro);
            chain.process (l, r, n, params_.params, transport_, ctx.blockStart, playing, seed_);
        }

        // (7b) VST3 slots of the track
        inserts_[i].process (l, r, n, params_.params, insertCtx);
        trackLatency[i] = chain.latencySamples() + inserts_[i].latencySamples();
        overflow = overflow || inserts_[i].latencyOverflow();
    }

    // (7c) PDC: every bus aligned to the slowest track chain
    float* busL[kNumTracks];
    float* busR[kNumTracks];
    for (int t = 0; t < kNumTracks; ++t)
    {
        busL[t] = buses_[static_cast<std::size_t> (2 * t)].data();
        busR[t] = buses_[static_cast<std::size_t> (2 * t + 1)].data();
    }
    lTracks_ = pdc_.apply (busL, busR, n, trackLatency);

    // (8) sum buses into master
    for (int t = 0; t < kNumTracks; ++t)
    {
        const float* l = busL[t];
        const float* r = busR[t];
        for (int i = 0; i < n; ++i)
        {
            masterPtrs[0][i] += l[i];
            masterPtrs[1][i] += r[i];
        }
    }

    // (9) glitch pads, (9a) master SHAKAL, (9b) master VST3 slots, then master gain
    glitch_.process (masterPtrs[0], masterPtrs[1], n, transport_, ctx.blockStart);
    if (! fxDisabled_)
        chains_[static_cast<std::size_t> (kMasterChain)].process (masterPtrs[0], masterPtrs[1], n, params_.params, transport_,
                                                                  ctx.blockStart, playing, seed_);
    auto& masterInserts = inserts_[static_cast<std::size_t> (kMasterChain)];
    masterInserts.process (masterPtrs[0], masterPtrs[1], n, params_.params, insertCtx);
    lMaster_ = masterInserts.latencySamples();
    overflow = overflow || masterInserts.latencyOverflow();
    for (int i = 0; i < n; ++i)
    {
        masterPtrs[0][i] *= masterGain;
        masterPtrs[1][i] *= masterGain;
    }
    const int lMasterPath = lTracks_ + lMaster_;

    // (10) guitar strip: monitor, gate, low cut, slots, loop, SHAKAL 9 (stage 5 seam: the guitar stem)
    auto& guitarInserts = inserts_[static_cast<std::size_t> (kGuitarChain)];
    lGuitar_ = guitarInserts.latencySamples();
    overflow = overflow || guitarInserts.latencyOverflow();
    pdcOverflow_ = overflow;
    {
        LoopPatternInfo info;
        if (playing)
        {
            info.valid = true;
            info.startBeat = sequencer_.patternStartBeat();
            info.startSample = transport_.sampleAtBeat (info.startBeat);
            info.loopSlot = sequencer_.currentLoopSlot();
        }
        LoopPatternInfo previous;
        if (info.valid && lastLoopInfo_.valid && lastLoopInfo_.startBeat != info.startBeat)
            previous = lastLoopInfo_;
        lastLoopInfo_ = info;

        const bool tapAmp = params_.params.get (guitarParam (GuitarParam::LoopTap)) >= 0.5f;
        loopPlayer_.update (playing, info, previous, GuitarStrip::startOffsetFor (lMasterPath, lGuitar_, tapAmp));

        GuitarContext gctx;
        gctx.in = in;
        gctx.numIn = numIn;
        gctx.blockStart = ctx.blockStart;
        gctx.playing = playing;
        gctx.lMasterPath = lMasterPath;
        gctx.loopCompDevice = loopCompDevice;
        gctx.seed = seed_;
        gctx.insertCtx = insertCtx;

        LoopResult result;
        strip_.process (masterPtrs[0], masterPtrs[1], n, params_.params, params_, transport_, bank_, gctx,
                        chains_[static_cast<std::size_t> (kGuitarChain)], guitarInserts, recorder_, loopPlayer_, result);
        handleLoopResult (result);
    }

    // (11) metronome, shifted by the master path latency; forced on during the loop count-in
    const bool countIn = recorder_.state() == LoopState::Armed;
    if (playing && (metronomeEnabled || countIn))
        metronome_.process (transport_, transport_.positionSamples() - lMasterPath, masterPtrs, 2, n,
                            countIn ? std::max (metronomeGain, 0.3f) : metronomeGain);

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

    // (12a) calibration pulses go straight to the outputs
    if (calibrator_.state() == CalState::Running && calibrator_.process (rawIn, out, numOut, n))
        events_.push ({ EngineEventType::CalibrationDone, 0, 0, static_cast<uint8_t> (calibrator_.state() == CalState::Done ? 1 : 0) });

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

    snapLoopState_.store (static_cast<int> (recorder_.state()), std::memory_order_relaxed);
    snapLoopSlot_.store (recorder_.state() == LoopState::Idle ? -1 : recorder_.slot(), std::memory_order_relaxed);
    snapLoopBars_.store (recorder_.bars(), std::memory_order_relaxed);
    snapLoopOverdub_.store (recorder_.overdub(), std::memory_order_relaxed);
    snapLoopStart_.store (recorder_.startSample(), std::memory_order_relaxed);
    snapLoopLength_.store (recorder_.lengthSamples(), std::memory_order_relaxed);
    snapRecIndex_.store (recorder_.recIndex(), std::memory_order_relaxed);
    snapPlayIndex_.store (loopPlayer_.playIndex(), std::memory_order_relaxed);
    snapComp_.store (strip_.lastComp(), std::memory_order_relaxed);
    snapPdcTracks_.store (lTracks_, std::memory_order_relaxed);
    snapPdcMaster_.store (lMaster_, std::memory_order_relaxed);
    snapPdcGuitar_.store (lGuitar_, std::memory_order_relaxed);
    snapPdcOverflow_.store (pdcOverflow_, std::memory_order_relaxed);
    snapCal_.store (static_cast<int> (calibrator_.state()), std::memory_order_relaxed);
    snapCalSamples_.store (calibrator_.result(), std::memory_order_relaxed);
}

LoopSnapshot EngineCore::loopSnapshot() const noexcept
{
    LoopSnapshot s;
    s.state = static_cast<LoopState> (snapLoopState_.load (std::memory_order_relaxed));
    s.slot = static_cast<int8_t> (snapLoopSlot_.load (std::memory_order_relaxed));
    s.bars = static_cast<int8_t> (snapLoopBars_.load (std::memory_order_relaxed));
    s.overdub = snapLoopOverdub_.load (std::memory_order_relaxed);
    s.startSample = snapLoopStart_.load (std::memory_order_relaxed);
    s.lengthSamples = snapLoopLength_.load (std::memory_order_relaxed);
    s.recIndex = snapRecIndex_.load (std::memory_order_relaxed);
    s.playIndex = snapPlayIndex_.load (std::memory_order_relaxed);
    s.compTotal = snapComp_.load (std::memory_order_relaxed);
    s.pdcTracks = snapPdcTracks_.load (std::memory_order_relaxed);
    s.pdcMaster = snapPdcMaster_.load (std::memory_order_relaxed);
    s.pdcGuitar = snapPdcGuitar_.load (std::memory_order_relaxed);
    s.pdcOverflow = snapPdcOverflow_.load (std::memory_order_relaxed);
    s.cal = static_cast<CalState> (snapCal_.load (std::memory_order_relaxed));
    s.calSamples = snapCalSamples_.load (std::memory_order_relaxed);
    return s;
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
