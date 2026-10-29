#pragma once

#include "AppSettings.h"
#include "AudioEngine.h"
#include "LoopController.h"
#include "SampleLoader.h"
#include "Plugins/InsertHost.h"
#include "Plugins/PluginHost.h"
#include "Plugins/PluginScanner.h"

#include "Model/Project.h"

#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

#include <array>
#include <bitset>
#include <functional>
#include <memory>
#include <optional>

namespace jackal
{

// Everything the master edit action snapshots (gain, SHAKAL macro and chain, seed for REROLL).
struct MasterState
{
    float gainDb = kMasterGainDefaultDb;
    float shakal = 0.f;
    FxSettings fx {};
    uint32_t seed = 0;
    bool operator== (const MasterState&) const = default;
};

struct StepRef
{
    int track = 0, slot = 0;
    bool operator== (const StepRef&) const = default;
};

// Owns the Project, the undo history and everything the engine gets from the UI.
// Every edit goes through here; UI components only read the model and call these methods.
class ProjectController : private juce::Timer
{
public:
    ProjectController (AudioEngine& engine, AppSettings& settings);
    ~ProjectController() override;

    void start();      // opens the last project or makes a new demo project
    void shutdown();   // saves, stops audio, retires every engine pointer

    // ---- model access
    const Project& project() const noexcept { return *project_; }
    int selectedTrack() const noexcept { return project_->ui.selectedTrack; }
    int editPatternIndex() const noexcept { return project_->ui.currentPattern; }
    const Pattern& editPattern() const noexcept { return project_->patterns[static_cast<std::size_t> (editPatternIndex())]; }
    int gridPage() const noexcept { return project_->ui.gridPage; }
    uint32_t version() const noexcept { return version_; }   // bumps on every model change
    bool isDirty() const noexcept { return dirty_; }
    juce::String projectName() const;
    const juce::File& folder() const noexcept { return folder_; }

    // ---- selection (UI state, not undoable)
    void selectTrack (int track);
    void setGridPage (int page);
    void setLastTouched (StepRef s) { lastTouched_ = s; bump(); }
    StepRef lastTouched() const noexcept { return lastTouched_; }
    void setPLockTarget (std::optional<StepRef> s) { plockTarget_ = s; bump(); }
    std::optional<StepRef> plockTarget() const noexcept { return plockTarget_; }

    // ---- edits (undoable)
    void beginGesture (const juce::String& name);
    void editPattern (const std::function<void (Pattern&)>& fn) { editPatternAt (editPatternIndex(), fn); }
    void editPatternAt (int index, const std::function<void (Pattern&)>& fn);
    void editTrack (int track, const std::function<void (TrackSettings&)>& fn);
    void setTrackParam (int track, TrackParam p, float value);    // writes a p-lock instead while a step is held
    void setSwing (int swing);
    void copyPattern (int from, int to);
    void doublePattern();
    void mutate();
    void undo();
    void redo();
    bool canUndo() const { return undo_.canUndo(); }
    bool canRedo() const { return undo_.canRedo(); }
    int undoDepth() const { return undo_.getUndoDescriptions().size(); }
    void setSliceMode (int track, SliceMode mode);
    void setSourceBars (int track, double bars);
    void setFitToTempo (int track, bool fit);
    void setChoke (int track, int group);
    void importFile (int track, const juce::File& file);

    // ---- SHAKAL (stage 3), undoable. chain 0..7 = track, kMasterChain = master.
    void setFx (int chain, FxParam p, float value);    // Bypass and Preset included
    void setShakal (int chain, float value);           // track: p-lock while a step is held
    float shakalValue (int chain) const;
    const FxSettings& fxSettings (int chain) const;
    void setMasterGainDb (float db);
    void reroll();
    void applyMaster (const MasterState&);
    ShakalSnapshot shakalSnapshot() const { return engine_.core().shakalSnapshot(); }

    // ---- guitar strip (stage 4), undoable like the SHAKAL knobs. Chain kGuitarChain in setFx/setShakal.
    void setGuitarParam (GuitarParam p, float value);
    const GuitarSettings& guitar() const noexcept { return project_->guitar; }
    void applyGuitar (const GuitarSettings&);   // undo actions; inserts are kept as they are
    LoopSnapshot loopSnapshot() const { return engine_.core().loopSnapshot(); }

    // ---- VST3 inserts (stage 4). chain 0..7 track, kMasterChain, kGuitarChain. Not undoable.
    const InsertSlotSpec& insertSpec (int chain, int slot) const;
    juce::String loadPlugin (int chain, int slot, const juce::PluginDescription&);
    void removePlugin (int chain, int slot);
    void setInsertBypass (int chain, int slot, bool bypass);
    void toggleEditor (int chain, int slot);
    bool insertMissing (int chain, int slot) const;
    int insertLatency (int chain, int slot) const;
    void setLastInsert (int chain, int slot) { lastInsert_ = { chain, slot }; }
    std::pair<int, int> lastInsert() const noexcept { return lastInsert_; }
    PluginHost& plugins() noexcept { return plugins_; }
    PluginScanner& scanner() noexcept { return scanner_; }
    InsertHost& inserts() noexcept { return inserts_; }
    LoopController& loops() noexcept { return loops_; }

    // ---- used by LoopController (internal edits, no undo)
    void setPatternLoopSlotRaw (int pattern, int slot);
    void setSlotSpecRaw (int slot, const SampleSlotSpec& spec);
    void adoptLoopSample (int slot, const Sample* s);   // the engine swapped it in (or an undo republished it)
    void loopToTrack (int slot, double sourceBars);     // one undo transaction "Loop to track"
    const Sample* liveSample (int slot) const { return slots_[static_cast<std::size_t> (slot)].live; }
    double sampleRate() const;

    // ---- UI state, not undoable
    void setTrackPanelTab (int tab);
    int trackPanelTab() const noexcept { return project_->ui.trackPanelTab; }

    // ---- not undoable
    void setBpm (double bpm);
    void setMutationStrength (float s);

    // ---- transport / performance
    void togglePlay();
    void queuePattern (int index);
    void setRecording (bool on);
    bool recording() const noexcept { return recording_; }
    void setFill (bool held);
    bool fillHeld() const noexcept { return fill_; }
    void padDown (int pad);
    void padUp (int pad);
    void triggerTrack (int track, int velocity);
    void clearPadFromLane (int pad);

    // ---- engine state for the UI (refreshed at 30 Hz)
    const SequencerSnapshot& snapshot() const noexcept { return snap_; }
    TransportState transport() const { return engine_.core().transportSnapshot(); }
    double hitAgeMs (int track) const;
    float cpuLoad() { return engine_.core().cpuLoad().load (std::memory_order_relaxed); }

    // ---- samples, as the UI sees them
    const WavePeaks* peaksForTrack (int track) const;
    const WavePeaks* peaksForSlot (int slot) const;
    int numSlicesForTrack (int track) const;
    std::vector<int64_t> sliceStartsForTrack (int track) const;
    bool trackSliced (int track) const { return numSlicesForTrack (track) > 0; }

    // ---- files
    void newProject();
    bool openProject (const juce::File& folder);
    bool save();
    bool saveAs (const juce::File& folder);

    std::function<void (bool ok)> onCalibration;   // CalibrationDone from the engine

    juce::String lastError() const noexcept { return lastError_; }
    void setStatus (const juce::String& text) { lastError_ = text; bump(); }

    // Undo actions call these.
    void applyPattern (int index, const Pattern& p);
    void applyTrack (int track, const TrackSettings& t);
    void applySlot (int slot, const SampleSlotSpec& s);

private:
    struct SlotRuntime
    {
        const Sample* live = nullptr;
        WavePeaks peaks;
        std::vector<int64_t> sliceStarts;
        int generation = 0;
    };

    void timerCallback() override;
    void publishDirtyPatterns();
    void drainEngine();
    void pushTrack (int track);
    void pushFx (int chain, const FxSettings& fx);
    void pushMaster();
    MasterState masterState() const;
    void editMaster (const std::function<void (MasterState&)>& fn);
    void pushAll();
    void pushGuitar();
    void loadInserts();
    void captureInsertStates();
    InsertSlotSpec& insertSpecRef (int chain, int slot);
    void reloadSlot (int slot);
    void reslice (int slot);
    void publishSample (int slot, std::unique_ptr<Sample> s);
    void onLoaded (LoadedSample&& r);
    void takeProject (std::unique_ptr<Project> p, const juce::File& folder);
    void bump() { ++version_; }
    void markDirty() { dirty_ = true; bump(); }
    int slotForImport (int track) const;

    AudioEngine& engine_;
    AppSettings& settings_;
    std::unique_ptr<Project> project_;
    juce::File folder_;
    juce::UndoManager undo_;
    std::bitset<kNumPatterns> dirtyPatterns_;
    std::array<SlotRuntime, kNumSampleSlots> slots_ {};
    std::unique_ptr<SampleLoader> loader_;
    PluginHost plugins_;
    PluginScanner scanner_;
    InsertHost inserts_;
    LoopController loops_;
    std::pair<int, int> lastInsert_ { -1, -1 };

    SequencerSnapshot snap_ {};
    std::array<double, kNumTracks> hitTime_ {};
    StepRef lastTouched_ {};
    std::optional<StepRef> plockTarget_;
    juce::String lastError_;
    uint32_t version_ = 1;
    bool dirty_ = false;
    bool recording_ = false;
    bool fill_ = false;
    bool stopped_ = false;
    int tick_ = 0;
    double lastSaveMs_ = 0.0;
};

} // namespace jackal
