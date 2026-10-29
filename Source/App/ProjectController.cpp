#include "ProjectController.h"

#include "ProjectFiles.h"

#include "Model/DemoKit.h"
#include "Model/Mutation.h"
#include "Model/ParamSpec.h"
#include "Model/PatternOps.h"
#include "Model/ProjectJson.h"
#include "Model/Slicer.h"

#include <random>

namespace jackal
{

namespace
{
    constexpr int kUndoUnitsPerAction = 10;
    constexpr double kAutosaveMs = 30000.0;

    std::size_t idx (int i) { return static_cast<std::size_t> (i); }

    struct PatternAction : juce::UndoableAction
    {
        PatternAction (ProjectController& c, int i, const Pattern& b, const Pattern& a)
            : controller (c), index (i), before (std::make_unique<Pattern> (b)), after (std::make_unique<Pattern> (a)) {}

        bool perform() override { controller.applyPattern (index, *after); return true; }
        bool undo() override { controller.applyPattern (index, *before); return true; }
        int getSizeInUnits() override { return kUndoUnitsPerAction; }

        juce::UndoableAction* createCoalescedAction (juce::UndoableAction* next) override
        {
            if (auto* n = dynamic_cast<PatternAction*> (next); n != nullptr && n->index == index)
                return new PatternAction (controller, index, *before, *n->after);
            return nullptr;
        }

        ProjectController& controller;
        int index;
        std::unique_ptr<Pattern> before, after;
    };

    struct TrackAction : juce::UndoableAction
    {
        TrackAction (ProjectController& c, int t, TrackSettings b, TrackSettings a)
            : controller (c), track (t), before (std::move (b)), after (std::move (a)) {}

        bool perform() override { controller.applyTrack (track, after); return true; }
        bool undo() override { controller.applyTrack (track, before); return true; }
        int getSizeInUnits() override { return kUndoUnitsPerAction; }

        juce::UndoableAction* createCoalescedAction (juce::UndoableAction* next) override
        {
            if (auto* n = dynamic_cast<TrackAction*> (next); n != nullptr && n->track == track)
                return new TrackAction (controller, track, before, n->after);
            return nullptr;
        }

        ProjectController& controller;
        int track;
        TrackSettings before, after;
    };

    struct SlotAction : juce::UndoableAction
    {
        SlotAction (ProjectController& c, int s, SampleSlotSpec b, SampleSlotSpec a)
            : controller (c), slot (s), before (std::move (b)), after (std::move (a)) {}

        bool perform() override { controller.applySlot (slot, after); return true; }
        bool undo() override { controller.applySlot (slot, before); return true; }
        int getSizeInUnits() override { return kUndoUnitsPerAction; }

        ProjectController& controller;
        int slot;
        SampleSlotSpec before, after;
    };
} // namespace

ProjectController::ProjectController (AudioEngine& engine, AppSettings& settings)
    : engine_ (engine), settings_ (settings), project_ (std::make_unique<Project>())
{
    undo_.setMaxNumberOfStoredUnits (200 * kUndoUnitsPerAction, 200);
    loader_ = std::make_unique<SampleLoader> ([this] (LoadedSample&& r) { onLoaded (std::move (r)); });
}

ProjectController::~ProjectController()
{
    shutdown();
}

void ProjectController::start()
{
    const juce::File last (settings_.lastProjectPath());
    if (settings_.lastProjectPath().isEmpty() || ! openProject (last))
    {
        const auto error = lastError_;
        newProject();
        if (settings_.lastProjectPath().isNotEmpty() && error.isNotEmpty())
            setStatus (error);
    }

    lastSaveMs_ = juce::Time::getMillisecondCounterHiRes();
    startTimerHz (60);
}

void ProjectController::shutdown()
{
    if (stopped_)
        return;
    stopped_ = true;
    stopTimer();

    if (dirty_)
        save();

    loader_ = nullptr;
    engine_.stopAudio();
    engine_.core().releaseAll();
    drainEngine();
    for (auto& s : slots_)
        s.live = nullptr;
}

juce::String ProjectController::projectName() const
{
    return folder_.getFileNameWithoutExtension();
}

double ProjectController::sampleRate() const
{
    const auto info = engine_.latency();
    return info.sampleRate > 0.0 ? info.sampleRate : 48000.0;
}

// ---------------------------------------------------------------------------- selection

void ProjectController::selectTrack (int track)
{
    project_->ui.selectedTrack = juce::jlimit (0, kNumTracks - 1, track);
    bump();
}

void ProjectController::setGridPage (int page)
{
    const int bars = editPattern().bars;
    const int pages = juce::jmax (1, (std::max<int> (bars * 16, editPattern().lanes[idx (selectedTrack())].length) + 15) / 16);
    project_->ui.gridPage = juce::jlimit (0, juce::jmin (3, pages - 1), page);
    bump();
}

// ---------------------------------------------------------------------------- undoable edits

void ProjectController::beginGesture (const juce::String& name)
{
    undo_.beginNewTransaction (name);
}

void ProjectController::editPatternAt (int index, const std::function<void (Pattern&)>& fn)
{
    if (index < 0 || index >= kNumPatterns)
        return;
    const Pattern& before = project_->patterns[idx (index)];
    auto after = std::make_unique<Pattern> (before);
    fn (*after);
    if (*after == before)
        return;
    undo_.perform (new PatternAction (*this, index, before, *after));
}

void ProjectController::editTrack (int track, const std::function<void (TrackSettings&)>& fn)
{
    if (track < 0 || track >= kNumTracks)
        return;
    const TrackSettings before = project_->tracks[idx (track)];
    TrackSettings after = before;
    fn (after);
    if (after == before)
        return;
    undo_.perform (new TrackAction (*this, track, before, after));
}

void ProjectController::setTrackParam (int track, TrackParam p, float value)
{
    value = clampTrackParam (p, value);

    if (plockTarget_.has_value() && plockTarget_->track == track)
    {
        const auto target = *plockTarget_;
        editPattern ([&] (Pattern& pat) { ops::setPLock (pat, target.track, target.slot, static_cast<uint8_t> (p), value); });
        return;
    }

    editTrack (track, [&] (TrackSettings& t) { t.params[static_cast<std::size_t> (p)] = value; });
}

void ProjectController::setSwing (int swing)
{
    editPattern ([swing] (Pattern& p) { p.swing = static_cast<uint8_t> (juce::jlimit (kMinSwing, kMaxSwing, swing)); });
}

void ProjectController::copyPattern (int from, int to)
{
    if (from == to)
        return;
    beginGesture ("Copy pattern");
    const Pattern src = project_->patterns[idx (from)];
    editPatternAt (to, [&src] (Pattern& p) { ops::copyPattern (src, p); });
}

void ProjectController::doublePattern()
{
    beginGesture ("x2");
    editPattern ([] (Pattern& p) { ops::doubleLength (p); });
}

void ProjectController::mutate()
{
    std::random_device rd;
    const uint64_t seed = (static_cast<uint64_t> (rd()) << 32) ^ rd();

    MutationInput in;
    for (int t = 0; t < kNumTracks; ++t)
    {
        in.numSlices[idx (t)] = numSlicesForTrack (t);
        in.sliced[idx (t)] = in.numSlices[idx (t)] > 0;
    }

    const float strength = project_->ui.mutationStrength;
    beginGesture ("Mutation #" + juce::String::toHexString (static_cast<juce::int64> (seed)));
    editPattern ([&] (Pattern& p) { jackal::mutate (p, in, strength, seed); });
}

void ProjectController::undo()
{
    undo_.undo();
}

void ProjectController::redo()
{
    undo_.redo();
}

void ProjectController::setSliceMode (int track, SliceMode mode)
{
    const int slot = project_->tracks[idx (track)].sampleSlot;
    if (slot < 0)
        return;
    auto before = project_->slots[idx (slot)];
    auto after = before;
    after.sliceMode = mode;
    if (mode == SliceMode::Transient)
        after.transientBoundaries.clear();
    if (after == before)
        return;
    beginGesture ("Slice");
    undo_.perform (new SlotAction (*this, slot, before, after));
}

void ProjectController::setSourceBars (int track, double bars)
{
    const int slot = project_->tracks[idx (track)].sampleSlot;
    if (slot < 0)
        return;
    auto before = project_->slots[idx (slot)];
    auto after = before;
    after.sourceBars = bars;
    if (after == before)
        return;
    beginGesture ("Bars");
    undo_.perform (new SlotAction (*this, slot, before, after));
}

void ProjectController::setFitToTempo (int track, bool fit)
{
    beginGesture ("Fit");
    editTrack (track, [fit] (TrackSettings& t) { t.fitToTempo = fit; });
}

void ProjectController::setChoke (int track, int group)
{
    beginGesture ("Choke");
    editTrack (track, [group] (TrackSettings& t) { t.chokeGroup = static_cast<uint8_t> (juce::jlimit (0, 4, group)); });
}

int ProjectController::slotForImport (int track) const
{
    const int current = project_->tracks[idx (track)].sampleSlot;
    if (current >= 0 && project_->slots[idx (current)].source == "file")
    {
        bool shared = false;
        for (int t = 0; t < kNumTracks; ++t)
            shared = shared || (t != track && project_->tracks[idx (t)].sampleSlot == current);
        if (! shared)
            return current;
    }

    for (int s = 0; s < kNumSampleSlots; ++s)
    {
        if (project_->slots[idx (s)].used)
            continue;
        bool referenced = false;
        for (const auto& t : project_->tracks)
            referenced = referenced || t.sampleSlot == s;
        if (! referenced)
            return s;
    }

    // a built-in slot used only by this track can be replaced
    if (current >= 0)
    {
        bool shared = false;
        for (int t = 0; t < kNumTracks; ++t)
            shared = shared || (t != track && project_->tracks[idx (t)].sampleSlot == current);
        if (! shared)
            return current;
    }
    return -1;
}

void ProjectController::importFile (int track, const juce::File& file)
{
    if (! files::isAudioFile (file))
    {
        setStatus ("only .wav .aif .aiff: " + file.getFileName());
        return;
    }

    juce::String error;
    const auto rel = files::importSample (folder_, file, error);
    if (rel.isEmpty())
    {
        setStatus (error);
        return;
    }

    const int slot = slotForImport (track);
    if (slot < 0)
    {
        setStatus ("no free sample slot");
        return;
    }

    const auto& tr = project_->tracks[idx (track)];
    const auto before = project_->slots[idx (slot)];
    SampleSlotSpec after;
    after.used = true;
    after.source = "file";
    after.path = rel.toStdString();
    after.sliceMode = before.source == "file" ? before.sliceMode : SliceMode::Off;
    const bool loopish = juce::String (tr.name).equalsIgnoreCase ("BREAK") || after.sliceMode != SliceMode::Off || tr.fitToTempo;
    if (loopish && after.sliceMode == SliceMode::Off)
        after.sliceMode = SliceMode::Grid16;
    after.sourceBars = 0.0;   // guessed by the loader when the file is decoded (loop-like tracks only)

    beginGesture ("Import " + file.getFileName());
    undo_.perform (new SlotAction (*this, slot, before, after));
    editTrack (track, [slot] (TrackSettings& t) { t.sampleSlot = static_cast<int8_t> (slot); });
    setStatus ("loaded " + file.getFileName() + " into slot " + juce::String (slot + 1));
}

// ---------------------------------------------------------------------------- apply (called by undo actions)

void ProjectController::applyPattern (int index, const Pattern& p)
{
    project_->patterns[idx (index)] = p;
    dirtyPatterns_.set (idx (index));
    markDirty();
}

void ProjectController::applyTrack (int track, const TrackSettings& t)
{
    project_->tracks[idx (track)] = t;
    pushTrack (track);
    markDirty();
}

void ProjectController::applySlot (int slot, const SampleSlotSpec& s)
{
    const auto old = project_->slots[idx (slot)];
    project_->slots[idx (slot)] = s;

    const bool sameSource = old.used == s.used && old.source == s.source && old.path == s.path;
    if (sameSource && slots_[idx (slot)].live != nullptr)
        reslice (slot);
    else
        reloadSlot (slot);
    markDirty();
}

// ---------------------------------------------------------------------------- not undoable

void ProjectController::setBpm (double bpm)
{
    project_->bpm = juce::jlimit (kMinBpm, kMaxBpm, bpm);
    engine_.core().commands().push ({ CommandType::SetBpm, static_cast<float> (project_->bpm), 0 });
    markDirty();
}

void ProjectController::setMasterGainDb (float db)
{
    project_->masterGainDb = juce::jlimit (kMasterGainSpec.minValue, kMasterGainSpec.maxValue, db);
    engine_.core().params().params.set (masterParam (MasterParam::Gain), project_->masterGainDb);
    markDirty();
}

void ProjectController::setMutationStrength (float s)
{
    project_->ui.mutationStrength = juce::jlimit (0.f, 1.f, s);
    markDirty();
}

// ---------------------------------------------------------------------------- transport / performance

void ProjectController::togglePlay()
{
    const bool playing = engine_.core().transportSnapshot().playing;
    engine_.core().commands().push ({ playing ? CommandType::Stop : CommandType::Play, 0.f, 0 });
}

void ProjectController::queuePattern (int index)
{
    index = juce::jlimit (0, kNumPatterns - 1, index);
    publishDirtyPatterns();
    engine_.core().commands().push ({ CommandType::QueuePattern, 0.f, index });
    project_->ui.currentPattern = index;
    setGridPage (project_->ui.gridPage);
    bump();
}

void ProjectController::setRecording (bool on)
{
    recording_ = on;
    if (on)
        beginGesture ("Record");
    engine_.core().commands().push ({ CommandType::SetRecording, 0.f, on ? 1 : 0 });
    bump();
}

void ProjectController::setFill (bool held)
{
    fill_ = held;
    engine_.core().params().fillHeld.store (held, std::memory_order_relaxed);
    bump();
}

void ProjectController::padDown (int pad)
{
    if (recording_)
        beginGesture ("Record pad");
    engine_.core().commands().push ({ CommandType::PadDown, 0.f, pad });
}

void ProjectController::padUp (int pad)
{
    engine_.core().commands().push ({ CommandType::PadUp, 0.f, pad });
}

void ProjectController::triggerTrack (int track, int velocity)
{
    engine_.core().commands().push ({ CommandType::TriggerTrack, static_cast<float> (juce::jlimit (1, 127, velocity)), track });
}

void ProjectController::clearPadFromLane (int pad)
{
    beginGesture ("Clear pad");
    editPattern ([pad] (Pattern& p) {
        for (auto& c : p.glitch.pad)
            if (c == pad + 1)
                c = 0;
    });
}

double ProjectController::hitAgeMs (int track) const
{
    return juce::Time::getMillisecondCounterHiRes() - hitTime_[idx (track)];
}

// ---------------------------------------------------------------------------- samples for the UI

const WavePeaks* ProjectController::peaksForTrack (int track) const
{
    const int slot = project_->tracks[idx (track)].sampleSlot;
    if (slot < 0 || slots_[idx (slot)].live == nullptr)
        return nullptr;
    return &slots_[idx (slot)].peaks;
}

int ProjectController::numSlicesForTrack (int track) const
{
    const int slot = project_->tracks[idx (track)].sampleSlot;
    if (slot < 0)
        return 0;
    const auto& s = slots_[idx (slot)].sliceStarts;
    return s.size() > 1 ? static_cast<int> (s.size()) - 1 : 0;
}

std::vector<int64_t> ProjectController::sliceStartsForTrack (int track) const
{
    const int slot = project_->tracks[idx (track)].sampleSlot;
    return slot < 0 ? std::vector<int64_t> {} : slots_[idx (slot)].sliceStarts;
}

// ---------------------------------------------------------------------------- engine publishing

void ProjectController::pushTrack (int track)
{
    auto& core = engine_.core();
    const auto& t = project_->tracks[idx (track)];
    for (int p = 0; p < kNumTrackParams; ++p)
        core.params().params.set (trackParam (track, static_cast<TrackParam> (p)), t.params[idx (p)]);
    core.params().trackMute[idx (track)].store (t.mute);
    core.params().trackSolo[idx (track)].store (t.solo);
    core.commands().push ({ CommandType::SetTrackSlot, 0.f, track | ((t.sampleSlot < 0 ? 0xFF : t.sampleSlot) << 8) });
    core.commands().push ({ CommandType::SetTrackChoke, 0.f, track | (t.chokeGroup << 8) });
    core.commands().push ({ CommandType::SetTrackFit, 0.f, track | ((t.fitToTempo ? 1 : 0) << 8) });
}

void ProjectController::pushAll()
{
    auto& core = engine_.core();
    core.commands().push ({ CommandType::Stop, 0.f, 0 });
    core.commands().push ({ CommandType::SetBpm, static_cast<float> (project_->bpm), 0 });
    core.commands().push ({ CommandType::SetSeed, 0.f, static_cast<int32_t> (project_->seed) });
    core.commands().push ({ CommandType::QueuePattern, 0.f, project_->ui.currentPattern });
    core.params().params.set (masterParam (MasterParam::Gain), project_->masterGainDb);

    for (int t = 0; t < kNumTracks; ++t)
        pushTrack (t);
    for (int s = 0; s < kNumSampleSlots; ++s)
        reloadSlot (s);
    dirtyPatterns_.set();
    publishDirtyPatterns();
}

void ProjectController::publishDirtyPatterns()
{
    for (int i = 0; i < kNumPatterns; ++i)
    {
        if (! dirtyPatterns_.test (idx (i)))
            continue;
        auto* p = new Pattern (project_->patterns[idx (i)]);
        if (engine_.core().commands().push ({ CommandType::SetPattern, 0.f, i, p }))
            dirtyPatterns_.reset (idx (i));
        else
            delete p;   // queue full: try again next frame
    }
}

void ProjectController::publishSample (int slot, std::unique_ptr<Sample> s)
{
    auto& rt = slots_[idx (slot)];
    const Sample* ptr = s.release();
    if (! engine_.core().commands().push ({ CommandType::SetSlotSample, 0.f, slot, ptr }))
    {
        delete ptr;
        setStatus ("engine busy, sample not loaded");
        return;
    }

    rt.live = ptr;
    rt.sliceStarts = ptr != nullptr ? ptr->sliceStarts : std::vector<int64_t> {};
    rt.peaks = ptr != nullptr ? computePeaks (*ptr) : WavePeaks {};
    bump();
}

void ProjectController::reloadSlot (int slot)
{
    auto& rt = slots_[idx (slot)];
    ++rt.generation;
    auto spec = project_->slots[idx (slot)];

    if (! spec.used || spec.source.empty())
    {
        if (rt.live != nullptr)
            publishSample (slot, nullptr);
        rt.live = nullptr;
        rt.sliceStarts.clear();
        return;
    }

    if (spec.source.rfind ("builtin:", 0) == 0)
    {
        auto s = makeBuiltin (spec.source.substr (8), sampleRate());
        if (s == nullptr)
        {
            setStatus ("unknown built-in sound " + juce::String (spec.source));
            return;
        }
        applySlicing (*s, spec);
        project_->slots[idx (slot)].transientBoundaries = spec.transientBoundaries;
        publishSample (slot, std::move (s));
        return;
    }

    const auto file = folder_.getChildFile (juce::String (spec.path));
    if (! file.existsAsFile())
    {
        setStatus ("missing sample " + juce::String (spec.path));
        return;
    }

    bool guess = spec.sourceBars <= 0.0;
    for (const auto& t : project_->tracks)
        if (t.sampleSlot == slot)
            guess = guess && (t.fitToTempo || spec.sliceMode != SliceMode::Off || juce::String (t.name).equalsIgnoreCase ("BREAK"));
    loader_->load (slot, rt.generation, file, spec, guess);
}

void ProjectController::reslice (int slot)
{
    auto& rt = slots_[idx (slot)];
    if (rt.live == nullptr)
    {
        reloadSlot (slot);
        return;
    }

    auto copy = std::make_unique<Sample> (*rt.live);   // the live one is immutable; slicing makes a new one
    auto& spec = project_->slots[idx (slot)];
    applySlicing (*copy, spec);
    ++rt.generation;
    publishSample (slot, std::move (copy));
}

void ProjectController::onLoaded (LoadedSample&& r)
{
    if (stopped_ || r.slot < 0 || r.slot >= kNumSampleSlots || r.generation != slots_[idx (r.slot)].generation)
        return;

    if (r.sample == nullptr)
    {
        setStatus (r.error);
        return;
    }

    auto& spec = project_->slots[idx (r.slot)];
    if (spec.path != r.spec.path)
        return;   // the slot was changed while decoding
    spec.transientBoundaries = r.spec.transientBoundaries;
    spec.sourceBars = r.spec.sourceBars;
    publishSample (r.slot, std::move (r.sample));
}

// ---------------------------------------------------------------------------- timer

void ProjectController::drainEngine()
{
    auto& core = engine_.core();

    Retired r;
    while (core.retired().pop (r))
    {
        if (r.kind == Retired::Kind::Pattern)
            delete static_cast<const Pattern*> (r.ptr);
        else
        {
            for (auto& s : slots_)
                if (s.live == r.ptr)
                    s.live = nullptr;
            delete static_cast<const Sample*> (r.ptr);
        }
    }

    EngineEvent e;
    const double now = juce::Time::getMillisecondCounterHiRes();
    while (core.events().pop (e))
    {
        switch (e.type)
        {
            case EngineEventType::TrackHit:
                if (e.track < kNumTracks)
                    hitTime_[e.track] = now;
                break;

            case EngineEventType::StepRecorded:
            {
                const int playing = juce::jlimit (0, kNumPatterns - 1, static_cast<int> (snap_.currentPattern));
                const int track = e.track, slot = e.step, vel = e.value;
                editPatternAt (playing, [=] (Pattern& p) {
                    auto& s = ops::step (p, track, slot);
                    s.active = true;
                    s.velocity = static_cast<uint8_t> (juce::jlimit (1, 127, vel));
                });
                break;
            }

            case EngineEventType::GlitchRecorded:
            {
                const int playing = juce::jlimit (0, kNumPatterns - 1, static_cast<int> (snap_.currentPattern));
                const int cell = e.step, pad = e.value;
                editPatternAt (playing, [=] (Pattern& p) { p.glitch.pad[static_cast<std::size_t> (cell % kMaxSteps)] = static_cast<uint8_t> (pad); });
                break;
            }

            case EngineEventType::PadState:
                break;
        }
    }
}

void ProjectController::timerCallback()
{
    publishDirtyPatterns();

    if (++tick_ % 2 == 0)
    {
        drainEngine();
        const auto s = engine_.core().sequencerSnapshot();
        snap_ = s;
    }

    const double now = juce::Time::getMillisecondCounterHiRes();
    if (dirty_ && now - lastSaveMs_ > kAutosaveMs)
    {
        save();
        lastSaveMs_ = now;
    }
}

// ---------------------------------------------------------------------------- files

void ProjectController::takeProject (std::unique_ptr<Project> p, const juce::File& folder)
{
    project_ = std::move (p);
    folder_ = folder;
    undo_.clearUndoHistory();
    plockTarget_.reset();
    lastTouched_ = {};
    pushAll();
    dirty_ = false;
    settings_.setLastProjectPath (folder_.getFullPathName());
    bump();
}

void ProjectController::newProject()
{
    if (dirty_)
        save();
    auto folder = files::newProjectFolder();
    auto p = makeDemoProject (sampleRate());
    p->name = folder.getFileNameWithoutExtension().toStdString();
    takeProject (std::move (p), folder);
    save();
}

bool ProjectController::openProject (const juce::File& folder)
{
    juce::String text;
    if (const auto r = files::readProject (folder, text); r.failed())
    {
        lastError_ = r.getErrorMessage();
        return false;
    }

    auto parsed = fromJson (text.toStdString());
    if (! parsed.ok)
    {
        lastError_ = juce::String (parsed.error);
        return false;
    }

    if (dirty_ && folder_ != folder)
        save();
    takeProject (std::move (parsed.project), folder);
    return true;
}

bool ProjectController::save()
{
    if (folder_ == juce::File())
        return false;
    const auto r = files::writeProject (folder_, juce::String (toJson (*project_)));
    if (r.failed())
    {
        setStatus (r.getErrorMessage());
        return false;
    }
    dirty_ = false;
    lastSaveMs_ = juce::Time::getMillisecondCounterHiRes();
    settings_.setLastProjectPath (folder_.getFullPathName());
    bump();
    return true;
}

bool ProjectController::saveAs (const juce::File& target)
{
    auto folder = target;
    if (! folder.hasFileExtension ("jackal"))
        folder = folder.withFileExtension ("jackal");
    if (folder == folder_)
        return save();

    // carry imported samples along: the project folder must stay self-contained
    const auto oldSamples = folder_.getChildFile ("samples");
    if (oldSamples.isDirectory())
        oldSamples.copyDirectoryTo (folder.getChildFile ("samples"));

    folder_ = folder;
    project_->name = folder.getFileNameWithoutExtension().toStdString();
    return save();
}

} // namespace jackal
