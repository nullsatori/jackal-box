#include "LoopController.h"

#include "ProjectController.h"

#include "Model/Slicer.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <cmath>

namespace jackal
{

namespace
{
    constexpr std::size_t kHistoryDepth = 16;   // layers kept in total, the oldest go first
    std::size_t idx (int i) { return static_cast<std::size_t> (i); }
}

LoopController::LoopController (ProjectController& controller, AudioEngine& engine) : c_ (controller), engine_ (engine) {}

LoopController::~LoopController()
{
    *alive_ = false;
    pool_.removeAllJobs (false, 10000);
}

int LoopController::slotOfEditPattern() const
{
    const int s = c_.editPattern().loopSlot;
    return s >= 8 && s < kNumSampleSlots ? s : -1;
}

bool LoopController::canUndo() const
{
    const int s = slotOfEditPattern();
    return s >= 0 && ! history_[idx (s)].empty();
}

int LoopController::freeLoopSlot() const
{
    const auto& p = c_.project();
    for (int s = 8; s < kNumSampleSlots; ++s)
    {
        bool taken = false;
        for (const auto& pat : p.patterns)
            taken = taken || pat.loopSlot == s;
        for (const auto& t : p.tracks)
            taken = taken || t.sampleSlot == s;
        if (! taken)
            return s;
    }
    return -1;
}

void LoopController::arm (int slot, int bars, bool overdub, int64_t capacity)
{
    auto* s = new Sample();
    s->numChannels = 2;
    s->sampleRate = c_.sampleRate();
    s->sourceBars = bars;
    s->data[0].assign (static_cast<std::size_t> (capacity), 0.f);
    s->data[1].assign (static_cast<std::size_t> (capacity), 0.f);

    const int ivalue = slot | (bars << 8) | ((overdub ? 1 : 0) << 16);
    if (! engine_.core().commands().push ({ CommandType::ArmLoop, 0.f, ivalue, s }))
    {
        delete s;
        c_.setStatus ("engine busy, loop not armed");
        return;
    }
    inEngine_ = s;
    inEngineSlot_ = slot;
    if (! engine_.core().params().monitorEnabled.load())
        c_.setStatus ("monitor is off: loop will be silent");
}

void LoopController::rec (int bars)
{
    if (busy())
        return;
    int slot = slotOfEditPattern();
    if (slot < 0)
        slot = freeLoopSlot();
    if (slot < 0)
    {
        c_.setStatus ("no free loop slot (8-15)");
        return;
    }

    c_.setPatternLoopSlotRaw (c_.editPatternIndex(), slot);
    auto spec = c_.project().slots[idx (slot)];
    if (spec.source != "loop")
    {
        spec = {};
        spec.source = "loop";
    }
    spec.used = true;
    c_.setSlotSpecRaw (slot, spec);

    bars = bars == 2 || bars == 4 ? bars : 1;
    const double bpm = c_.project().bpm;
    const auto capacity = static_cast<int64_t> (std::ceil (bars * 4.0 * c_.sampleRate() * 60.0 / bpm)) + 1;
    arm (slot, bars, false, capacity);
}

void LoopController::dub()
{
    if (busy())
        return;
    const int slot = slotOfEditPattern();
    const Sample* cur = slot >= 0 ? currentLayer (slot).sample : nullptr;
    if (cur == nullptr || cur->length <= 0)
    {
        rec (bars_);
        return;
    }
    arm (slot, static_cast<int> (std::lround (cur->sourceBars)), true, cur->length);
}

void LoopController::enter()
{
    if (busy())
    {
        cancel();
        return;
    }
    const int slot = slotOfEditPattern();
    if (slot >= 0 && currentLayer (slot).sample != nullptr)
        dub();
    else
        rec (bars_);
}

void LoopController::cancel()
{
    if (busy())
        engine_.core().commands().push ({ CommandType::CancelLoop, 0.f, 0 });
}

void LoopController::onRecorded (int slot)
{
    if (inEngine_ == nullptr || slot != inEngineSlot_)
        return;
    const Sample* finished = inEngine_;
    inEngine_ = nullptr;
    inEngineSlot_ = -1;

    pushHistory (slot, currentLayer (slot));
    const auto path = "loops/" + std::to_string (slot) + "-" + std::to_string (++fileCounter_[idx (slot)]) + ".wav";
    current_[idx (slot)] = { finished, path, ++seq_ };
    c_.adoptLoopSample (slot, finished);
    saveWav (slot, finished, path);
}

void LoopController::onAborted (int slot)
{
    if (slot == inEngineSlot_)
    {
        inEngine_ = nullptr;   // the pointer comes back through the retire queue and is deleted there
        inEngineSlot_ = -1;
    }
}

void LoopController::pushHistory (int slot, Layer layer)
{
    layer.seq = ++seq_;
    history_[idx (slot)].push_back (layer);   // "no loop" is a layer too: UNDO of the first take gives silence

    std::size_t total = 0;
    for (const auto& h : history_)
        total += h.size();
    while (total > kHistoryDepth)
    {
        // drop the oldest layer of all slots
        int oldest = -1;
        for (int s = 0; s < kNumSampleSlots; ++s)
            if (! history_[idx (s)].empty() && (oldest < 0 || history_[idx (s)].front().seq < history_[idx (oldest)].front().seq))
                oldest = s;
        const Layer dropped = history_[idx (oldest)].front();
        history_[idx (oldest)].erase (history_[idx (oldest)].begin());
        release (dropped.sample);
        --total;
    }
}

void LoopController::undo()
{
    const int slot = slotOfEditPattern();
    if (slot < 0 || history_[idx (slot)].empty() || busy())
        return;

    const Layer prev = history_[idx (slot)].back();
    if (! engine_.core().commands().push ({ CommandType::SetSlotSample, 0.f, slot, prev.sample }))
    {
        c_.setStatus ("engine busy");
        return;
    }
    history_[idx (slot)].pop_back();
    const Layer now = currentLayer (slot);
    current_[idx (slot)] = prev;
    release (now.sample);   // the engine hands it back now; deleted then unless a file job holds it

    auto spec = c_.project().slots[idx (slot)];
    spec.path = prev.path;
    spec.used = true;
    spec.source = "loop";
    if (prev.sample != nullptr)
        spec.sourceBars = prev.sample->sourceBars;
    c_.setSlotSpecRaw (slot, spec);
    c_.adoptLoopSample (slot, prev.sample);
}

void LoopController::toTrack()
{
    const int slot = slotOfEditPattern();
    if (slot < 0 || currentLayer (slot).sample == nullptr || busy())
    {
        c_.setStatus ("no loop in this pattern");
        return;
    }

    // the history of this slot ends here; the published loop is resliced into a new sample
    for (const auto& l : history_[idx (slot)])
        release (l.sample);
    history_[idx (slot)].clear();
    const Layer cur = currentLayer (slot);
    current_[idx (slot)] = {};
    release (cur.sample);

    c_.loopToTrack (slot, cur.sample->sourceBars);
}

LoopController::Layer LoopController::currentLayer (int slot) const
{
    const auto& c = current_[idx (slot)];
    if (c.sample != nullptr)
        return c;
    return { c_.liveSample (slot), c_.project().slots[idx (slot)].path, 0 };
}

bool LoopController::keeps (const Sample* s) const
{
    if (s == nullptr)
        return false;
    if (pinned_.count (s) != 0)
        return true;
    for (const auto& c : current_)
        if (c.sample == s)
            return true;
    for (const auto& h : history_)
        for (const auto& l : h)
            if (l.sample == s)
                return true;
    return false;
}

void LoopController::release (const Sample* s)
{
    if (s != nullptr && retired_.count (s) != 0 && ! keeps (s))
    {
        retired_.erase (s);
        delete s;
    }
}

bool LoopController::onRetired (const Sample* s)
{
    if (! keeps (s))
        return false;
    retired_.insert (s);
    return true;
}

void LoopController::saveWav (int slot, const Sample* s, const std::string& path)
{
    pinned_.insert (s);
    const auto file = c_.folder().getChildFile (juce::String (path));
    auto alive = alive_;
    pool_.addJob ([this, alive, s, file, slot, path] {
        file.getParentDirectory().createDirectory();
        bool ok = false;
        {
            std::unique_ptr<juce::OutputStream> out = std::make_unique<juce::FileOutputStream> (file);
            if (static_cast<juce::FileOutputStream*> (out.get())->openedOk())
            {
                static_cast<juce::FileOutputStream*> (out.get())->setPosition (0);
                static_cast<juce::FileOutputStream*> (out.get())->truncate();
                juce::WavAudioFormat wav;
                const auto opts = juce::AudioFormatWriterOptions {}
                                      .withSampleRate (s->sampleRate)
                                      .withNumChannels (2)
                                      .withBitsPerSample (32)
                                      .withSampleFormat (juce::AudioFormatWriterOptions::SampleFormat::floatingPoint);
                if (auto writer = wav.createWriterFor (out, opts))
                {
                    const float* chans[2] = { s->channel (0), s->channel (1) };
                    ok = writer->writeFromFloatArrays (chans, 2, static_cast<int> (s->length));
                }
            }
        }
        juce::MessageManager::callAsync ([this, alive, s, slot, path, ok] {
            if (! *alive)
                return;
            pinned_.erase (s);
            if (ok && current_[idx (slot)].sample == s)
            {
                auto spec = c_.project().slots[idx (slot)];
                spec.used = true;
                spec.source = "loop";
                spec.path = path;
                spec.sourceBars = s->sourceBars;
                c_.setSlotSpecRaw (slot, spec);
            }
            if (! ok)
                c_.setStatus ("could not write " + juce::String (path));
            release (s);
        });
    });
}

void LoopController::reset()
{
    if (inEngine_ != nullptr)
    {
        inEngine_ = nullptr;   // pushAll() stops the transport: the engine aborts and hands it back
        inEngineSlot_ = -1;
    }
    std::vector<const Sample*> refs;
    for (auto& h : history_)
    {
        for (const auto& l : h)
            refs.push_back (l.sample);
        h.clear();
    }
    for (auto& c : current_)
    {
        refs.push_back (c.sample);
        c = {};
    }
    for (auto* s : refs)
        release (s);
    fileCounter_.fill (0);
}

} // namespace jackal
