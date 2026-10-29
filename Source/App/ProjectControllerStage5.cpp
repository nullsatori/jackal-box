// Stage 5 parts of ProjectController: song, follow, capture, MIDI parameters and actions, export, sample stepping.

#include "ProjectController.h"

#include "ProjectFiles.h"

#include "Model/OfflineRender.h"
#include "Model/ParamSpec.h"
#include "Model/PatternOps.h"
#include "Model/SongOps.h"

namespace jackal
{

namespace
{
    constexpr int kUndoUnits = 10;
    constexpr double kMidiGestureMs = 500.0;

    std::size_t idx (int i) { return static_cast<std::size_t> (i); }

    juce::String letter (int pattern)
    {
        return juce::String::charToString (static_cast<juce::juce_wchar> ('A' + juce::jlimit (0, kNumPatterns - 1, pattern)));
    }

    struct SongAction : juce::UndoableAction
    {
        SongAction (ProjectController& c, const SongData& b, const SongData& a) : controller (c), before (b), after (a) {}

        bool perform() override { controller.applySong (after); return true; }
        bool undo() override { controller.applySong (before); return true; }
        int getSizeInUnits() override { return kUndoUnits; }

        juce::UndoableAction* createCoalescedAction (juce::UndoableAction* next) override
        {
            if (auto* n = dynamic_cast<SongAction*> (next); n != nullptr)
                return new SongAction (controller, before, n->after);
            return nullptr;
        }

        ProjectController& controller;
        SongData before, after;
    };

    juce::String clock (double seconds)
    {
        const int m = static_cast<int> (seconds / 60.0);
        const double s = seconds - m * 60.0;
        return juce::String (m).paddedLeft ('0', 2) + ":" + juce::String (s, 1).paddedLeft ('0', 4);
    }
} // namespace

// ---------------------------------------------------------------------------- song

void ProjectController::editSong (const juce::String& gesture, const std::function<void (SongData&)>& fn)
{
    const SongData before = project_->song;
    SongData after = before;
    fn (after);
    if (after == before)
        return;
    beginGesture (gesture);
    undo_.perform (new SongAction (*this, before, after));
}

void ProjectController::applySong (const SongData& s)
{
    project_->song = s;
    project_->ui.songCursor = juce::jlimit (0, juce::jmax (0, s.length - 1), project_->ui.songCursor);
    songDirty_ = true;
    markDirty();
}

void ProjectController::setSongMode (bool on)
{
    project_->ui.songMode = on;
    publishDirtyPatterns();
    publishDirtySong();
    auto& q = engine_.core().commands();
    if (on && ! engine_.core().transportSnapshot().playing && project_->song.length > 0)
    {
        project_->ui.songCursor = juce::jlimit (0, project_->song.length - 1, project_->ui.songCursor);
        q.push ({ CommandType::SetSongPosition, 0.f, project_->ui.songCursor });
    }
    q.push ({ CommandType::SetSongMode, 0.f, on ? 1 : 0 });
    if (on && project_->song.length == 0)
        setStatus ("song is empty: Shift+click A..H to add");
    markDirty();
}

void ProjectController::setFollow (bool on)
{
    project_->ui.follow = on;
    if (on && project_->ui.songMode && engine_.core().transportSnapshot().playing)
        selectEditPattern (snap_.currentPattern);
    markDirty();
}

void ProjectController::setSongCursor (int index)
{
    if (project_->song.length == 0)
        return;
    index = juce::jlimit (0, project_->song.length - 1, index);
    project_->ui.songCursor = index;
    publishDirtySong();
    engine_.core().commands().push ({ CommandType::SetSongPosition, 0.f, index });
    bump();
}

void ProjectController::selectEditPattern (int index)
{
    index = juce::jlimit (0, kNumPatterns - 1, index);
    if (project_->ui.songMode && project_->ui.follow && index != snap_.currentPattern && engine_.core().transportSnapshot().playing)
        project_->ui.follow = false;   // a manual pick must not run away under the cursor
    project_->ui.currentPattern = index;
    setGridPage (project_->ui.gridPage);
    bump();
}

void ProjectController::patternClicked (int index)
{
    if (project_->ui.songMode)
        selectEditPattern (index);
    else
        queuePattern (index);
}

void ProjectController::appendToSong (int pattern)
{
    if (project_->song.length >= kMaxSongEntries)
    {
        setStatus ("song is full (32)");
        return;
    }
    editSong ("Song + " + letter (pattern), [pattern] (SongData& s) { songops::append (s, pattern, 1); });
    setStatus ("song: " + letter (pattern) + " added (" + juce::String (project_->song.length) + ")");
}

void ProjectController::followSnapshot (const SequencerSnapshot& previous)
{
    auto& ui = project_->ui;

    // the engine left the song on its own (QueuePattern from anywhere)
    if (previous.songMode && ! snap_.songMode && ui.songMode)
    {
        ui.songMode = false;
        bump();
    }

    if (ui.songMode && ui.follow && snap_.currentPattern != previous.currentPattern && snap_.currentPattern >= 0)
    {
        ui.currentPattern = snap_.currentPattern;
        setGridPage (ui.gridPage);
        bump();
    }

    if (snap_.songCursor >= 0 && snap_.songCursor != ui.songCursor && snap_.songCursor != previous.songCursor)
    {
        ui.songCursor = snap_.songCursor;
        bump();
    }

    if (snap_.songIndex != previous.songIndex || snap_.songRepeat != previous.songRepeat || snap_.songEnded != previous.songEnded)
        bump();
}

// ---------------------------------------------------------------------------- capture

void ProjectController::capture (int bars)
{
    if (! engine_.core().transportSnapshot().playing)
    {
        setStatus ("capture: press PLAY and hit some pads first");
        return;
    }
    if (capturing_)
        return;
    captureBuffer_.clear();
    if (! engine_.core().commands().push ({ CommandType::CaptureHits, 0.f, juce::jlimit (1, 4, bars) }))
    {
        setStatus ("engine busy, capture skipped");
        return;
    }
    capturing_ = true;
    captureStartMs_ = juce::Time::getMillisecondCounterHiRes();
}

void ProjectController::finishCapture (bool complete)
{
    capturing_ = false;
    const auto hits = captureBuffer_;
    captureBuffer_.clear();
    if (hits.empty())
    {
        setStatus (complete ? "capture: no hits in the last bars" : "capture incomplete");
        return;
    }

    const int playing = juce::jlimit (0, kNumPatterns - 1, static_cast<int> (snap_.currentPattern));
    beginGesture ("Capture");
    editPatternAt (playing, [&hits] (Pattern& p) {
        for (const auto& e : hits)
        {
            auto& s = ops::step (p, e.track, e.step);
            s.active = true;
            s.velocity = static_cast<uint8_t> (juce::jlimit (1, 127, static_cast<int> (e.value)));
        }
    });
    setStatus (complete ? "captured " + juce::String (static_cast<int> (hits.size())) + " hits into " + letter (playing)
                        : "capture incomplete (" + juce::String (static_cast<int> (hits.size())) + " hits)");
}

// ---------------------------------------------------------------------------- MIDI

void ProjectController::setParamFromMidi (ParamId id, float value)
{
    if (! isDefined (id))
        return;
    const auto& inf = info (id);
    const bool continuous = inf.scale == ParamScale::Linear || inf.scale == ParamScale::Log;
    const double now = juce::Time::getMillisecondCounterHiRes();

    // one transaction per burst of moves on the same control
    if (! continuous || id != midiParam_ || now - midiParamMs_ > kMidiGestureMs)
        beginGesture ("MIDI " + juce::String (std::string (inf.label)));
    midiParam_ = continuous ? id : ParamId (0xFFFF);
    midiParamMs_ = now;

    if (id < kTrackBase)
    {
        if (id == masterParam (MasterParam::Gain))
            setMasterGainDb (value);
        else if (id == masterParam (MasterParam::Shakal))
            setShakal (kMasterChain, value);
        return;
    }
    if (id < kGlitchBase)
    {
        const int track = (id - kTrackBase) / kTrackStride, p = (id - kTrackBase) % kTrackStride;
        if (track < kNumTracks && p < kNumTrackParams)
            setTrackParam (track, static_cast<TrackParam> (p), value);   // p-lock while a step is held
        return;
    }
    if (id >= kFxBase && id < kFxBase + kNumChains * kFxStride)
    {
        const int chain = (id - kFxBase) / kFxStride, p = (id - kFxBase) % kFxStride;
        if (p < kNumFxParams)
            setFx (chain, static_cast<FxParam> (p), value);
        return;
    }
    if (id >= kGuitarBase && id < kGuitarBase + kNumGuitarParams)
    {
        setGuitarParam (static_cast<GuitarParam> (id - kGuitarBase), value);
        return;
    }
    if (id >= kInsertBase && id < kInsertBase + kNumChains * kSlotsPerChain)
    {
        const int n = id - kInsertBase;
        setInsertBypass (n / kSlotsPerChain, n % kSlotsPerChain, value >= 0.5f);
    }
}

float ProjectController::paramValue (ParamId id) const
{
    const auto& p = *project_;
    if (id == masterParam (MasterParam::Gain)) return p.masterGainDb;
    if (id == masterParam (MasterParam::Shakal)) return p.masterShakal;
    if (id >= kTrackBase && id < kGlitchBase)
    {
        const int track = (id - kTrackBase) / kTrackStride, q = (id - kTrackBase) % kTrackStride;
        return track < kNumTracks && q < kNumTrackParams ? p.tracks[idx (track)].params[idx (q)] : 0.f;
    }
    if (id >= kFxBase && id < kFxBase + kNumChains * kFxStride)
    {
        const int chain = (id - kFxBase) / kFxStride, q = (id - kFxBase) % kFxStride;
        const auto& fx = fxSettings (chain);
        if (q == static_cast<int> (FxParam::Bypass)) return fx.bypass ? 1.f : 0.f;
        if (q == static_cast<int> (FxParam::Preset)) return static_cast<float> (fx.preset);
        return q < kNumFxParams ? fx.params[idx (q)] : 0.f;
    }
    if (id >= kGuitarBase && id < kGuitarBase + kNumGuitarParams)
    {
        const auto& g = p.guitar;
        switch (static_cast<GuitarParam> (id - kGuitarBase))
        {
            case GuitarParam::Shakal:     return g.shakal;
            case GuitarParam::GateDb:     return g.gateDb;
            case GuitarParam::HpfHz:      return g.hpfHz;
            case GuitarParam::LoopGainDb: return g.loopGainDb;
            case GuitarParam::LoopTap:    return g.loopTap == LoopTap::Amp ? 1.f : 0.f;
            case GuitarParam::Count:      break;
        }
    }
    if (id >= kInsertBase && id < kInsertBase + kNumChains * kSlotsPerChain)
    {
        const int n = id - kInsertBase;
        return insertSpec (n / kSlotsPerChain, n % kSlotsPerChain).bypass ? 1.f : 0.f;
    }
    return 0.f;
}

void ProjectController::performAction (ActionId action, int index, bool pressed, int velocity)
{
    // momentary actions: both edges (the CC path; notes are played by the engine itself)
    switch (action)
    {
        case ActionId::Fill: setFill (pressed); return;
        case ActionId::Pad:
            if (pressed) padDown (juce::jlimit (0, kNumPads - 1, index));
            else         padUp (juce::jlimit (0, kNumPads - 1, index));
            return;
        case ActionId::TrackHit:
            if (pressed)
                triggerTrack (juce::jlimit (0, kNumTracks - 1, index), velocity > 0 ? velocity : 100);
            return;
        default: break;
    }

    if (! pressed)
        return;

    const bool playing = engine_.core().transportSnapshot().playing;
    const int track = juce::jlimit (0, kNumTracks - 1, index);
    switch (action)
    {
        case ActionId::Play:        if (! playing) togglePlay(); break;
        case ActionId::Stop:        if (playing) togglePlay(); break;
        case ActionId::TogglePlay:  togglePlay(); break;
        case ActionId::Rec:         setRecording (! recording_); break;
        case ActionId::Pattern:     patternClicked (index); break;
        case ActionId::SongMode:    setSongMode (! project_->ui.songMode); break;
        case ActionId::SongNext:
        case ActionId::SongPrev:
        {
            const int from = snap_.songCursor >= 0 ? snap_.songCursor : juce::jmax (0, static_cast<int> (snap_.songIndex));
            setSongCursor (from + (action == ActionId::SongNext ? 1 : -1));
            break;
        }
        case ActionId::LoopRec:     loops_.busy() ? loops_.cancel() : loops_.rec (loops_.bars()); break;   // like the REC button
        case ActionId::LoopDub:     loops_.busy() ? loops_.cancel() : loops_.dub(); break;
        case ActionId::LoopUndo:    loops_.undo(); break;
        case ActionId::Mute:
            beginGesture ("Mute");
            editTrack (track, [] (TrackSettings& s) { s.mute = ! s.mute; });
            break;
        case ActionId::Solo:
            beginGesture ("Solo");
            editTrack (track, [] (TrackSettings& s) { s.solo = ! s.solo; });
            break;
        case ActionId::SelectTrack: selectTrack (track); break;
        case ActionId::Mutate:      mutate(); break;
        case ActionId::Undo:        undo(); break;
        case ActionId::Redo:        redo(); break;
        case ActionId::Reroll:      reroll(); break;
        case ActionId::Capture:     capture (2); break;
        case ActionId::Fill:
        case ActionId::Pad:
        case ActionId::TrackHit:
        case ActionId::Count:       break;
    }
}

// ---------------------------------------------------------------------------- export

void ProjectController::setExportSettings (const ExportSettings& s)
{
    auto& e = project_->exportSettings;
    e = s;
    if (e.sampleRate != 44100 && e.sampleRate != 48000 && e.sampleRate != 96000)
        e.sampleRate = 0;
    if (e.bitDepth != 16 && e.bitDepth != 32)
        e.bitDepth = 24;
    e.tailSeconds = juce::jlimit (0.f, 8.f, std::round (e.tailSeconds * 2.f) / 2.f);
    e.cycles = juce::jlimit (1, 16, e.cycles);
    markDirty();
}

bool ProjectController::exportAllowed() const
{
    return export_ == nullptr && engine_.core().loopSnapshot().state == LoopState::Idle;
}

juce::File ProjectController::exportFolder() const
{
    const auto custom = settings_.exportFolder();
    return custom.isNotEmpty() ? juce::File (custom) : folder_.getChildFile ("export");
}

juce::String ProjectController::startExport (RenderScope scope)
{
    if (export_ != nullptr)
        return "an export is already running";
    if (engine_.core().loopSnapshot().state != LoopState::Idle)
        return "finish the loop recording first";
    if (scope == RenderScope::Song && ! songops::playable (project_->song))
        return "song is empty";

    const auto& es = project_->exportSettings;
    ExportSources src;
    src.project = std::make_unique<Project> (*project_);
    src.sampleRate = es.sampleRate > 0 ? static_cast<double> (es.sampleRate) : sampleRate();
    src.depth = es.bitDepth == 16 ? WavDepth::Pcm16 : es.bitDepth == 32 ? WavDepth::Float32 : WavDepth::Pcm24;
    src.options.scope = scope;
    src.options.patternIndex = project_->ui.currentPattern;
    src.options.cycles = es.cycles;
    src.options.tailSeconds = es.tailSeconds;
    src.options.stems = es.stems;
    src.options.blockSize = 512;

    for (int s = 0; s < kNumSampleSlots; ++s)
        if (const Sample* live = slots_[idx (s)].live)
            src.samples[idx (s)] = new Sample (*live);   // deep copy: the live one stays with the live engine

    for (int c = 0; c < kNumChains; ++c)
        for (int sl = 0; sl < kSlotsPerChain; ++sl)
        {
            const auto& spec = insertSpec (c, sl);
            if (spec.empty())
                continue;
            const auto skipped = "skipped: " + InsertHost::slotName (c, sl) + " " + juce::String (spec.name);
            auto* live = inserts_.live (c, sl);
            auto d = plugins_.find (juce::String (spec.identifier));
            if (live == nullptr || d == nullptr)
            {
                src.warnings.add (skipped + " (missing)");
                continue;
            }
            juce::String error;
            auto inst = plugins_.create (*d, src.sampleRate, src.options.blockSize, error);
            auto insert = inst != nullptr ? Vst3Insert::make (std::move (inst), error) : nullptr;
            if (insert == nullptr)
            {
                src.warnings.add (skipped + " (" + (error.isNotEmpty() ? error : juce::String ("failed")) + ")");
                continue;
            }
            insert->setState (live->state());
            insert->prepare (src.sampleRate, src.options.blockSize);
            src.inserts.slots[idx (c * kSlotsPerChain + sl)] = insert.release();
        }

    const auto name = juce::String (sanitizeFileName (projectName().toStdString()));
    src.folder = exportFolder().getChildFile ((name.isEmpty() ? juce::String ("Jackal") : name) + "_"
                                              + juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M%S"));

    export_ = std::make_unique<ExportJob> (std::move (src));
    export_->begin();
    hasLastExport_ = false;
    setStatus (juce::String (juce::CharPointer_UTF8 ("exporting\xe2\x80\xa6")));
    return {};
}

void ProjectController::cancelExport()
{
    if (export_ != nullptr)
        export_->cancel();
}

ExportState ProjectController::exportState() const
{
    ExportState s;
    s.running = export_ != nullptr;
    if (s.running)
    {
        s.fraction = export_->fraction();
        s.totalSeconds = export_->totalSeconds();
        s.renderedSeconds = s.fraction * s.totalSeconds;
        const double elapsed = export_->elapsedSeconds();
        s.speed = elapsed > 0.05 ? s.renderedSeconds / elapsed : 0.0;
    }
    s.hasResult = hasLastExport_;
    s.result = hasLastExport_ ? &lastExport_ : nullptr;
    s.resultText = lastExportText_;
    s.resultAlarm = hasLastExport_ && (lastExport_.status == RenderStatus::Error || lastExport_.clipped > 0);
    return s;
}

void ProjectController::pollExport()
{
    if (export_ == nullptr || ! export_->isDone())
        return;

    lastExport_ = export_->takeOutcome();
    export_.reset();
    for (auto* p : lastExport_.retiredInserts)   // second plugin instances: deleted on the message thread
        delete p;
    lastExport_.retiredInserts.clear();
    hasLastExport_ = true;

    const auto& r = lastExport_;
    if (r.status == RenderStatus::Done)
    {
        const auto rel = r.folder.isAChildOf (folder_) ? r.folder.getRelativePathFrom (folder_).replaceCharacter ('\\', '/') : r.folder.getFullPathName();
        auto text = "exported " + clock (r.seconds) + juce::String (juce::CharPointer_UTF8 (" \xe2\x86\x92 ")) + rel + "/ (master";
        if (r.stemsWritten > 0)
            text << " + " << r.stemsWritten << " stems";
        text << ", peak " << juce::String (r.peakDb, 1) << " dBFS, clipped " << juce::String (r.clipped) << ")";
        if (! r.warnings.isEmpty())
            text << "  " << r.warnings.joinIntoString ("; ");
        setStatus (text, r.clipped > 0);
    }
    else if (r.status == RenderStatus::Cancelled)
        setStatus ("export cancelled");
    else
        setStatus ("export failed: " + r.error, true);
    lastExportText_ = lastError_;
}

// ---------------------------------------------------------------------------- Ctrl+Up / Ctrl+Down

void ProjectController::stepSampleFile (int track, int delta)
{
    track = juce::jlimit (0, kNumTracks - 1, track);
    const int slot = project_->tracks[idx (track)].sampleSlot;
    const SampleSlotSpec* spec = slot >= 0 ? &project_->slots[idx (slot)] : nullptr;
    if (spec == nullptr || spec->source != "file" || spec->origin.empty())
    {
        setStatus ("no source folder");
        return;
    }
    const juce::File origin (juce::String (spec->origin));
    if (! origin.existsAsFile())
    {
        setStatus ("no source folder (" + origin.getFileName() + " is gone)");
        return;
    }

    auto list = origin.getParentDirectory().findChildFiles (juce::File::findFiles, false, "*");
    list.removeIf ([] (const juce::File& f) { return ! files::isAudioFile (f); });
    std::sort (list.begin(), list.end(), [] (const juce::File& a, const juce::File& b) {
        return a.getFileName().compareNatural (b.getFileName()) < 0;
    });
    const int n = list.size();
    int at = list.indexOf (origin);
    if (n == 0 || at < 0)
    {
        setStatus ("no source folder");
        return;
    }
    at = ((at + delta) % n + n) % n;
    const auto next = list[at];
    importFile (track, next, true);
    setStatus ("sample " + juce::String (at + 1) + "/" + juce::String (n) + ": " + next.getFileName());
}

} // namespace jackal
