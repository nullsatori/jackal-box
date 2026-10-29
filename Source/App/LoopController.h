#pragma once

#include "AudioEngine.h"

#include "Engine/Sample.h"

#include <juce_core/juce_core.h>
#include <juce_events/juce_events.h>

#include <array>
#include <memory>
#include <set>
#include <string>
#include <vector>

namespace jackal
{

class ProjectController;

// Guitar loops: arming, layer history (undo), WAV files of the loops, "loop to track".
// Message thread only. Owns every loop Sample that is not in the engine.
class LoopController
{
public:
    LoopController (ProjectController& controller, AudioEngine& engine);
    ~LoopController();

    void rec (int bars);   // a new loop for the edit pattern (the old one goes to the history)
    void dub();            // a layer over the current loop
    void enter();          // Enter: no loop -> REC, loop -> DUB, armed -> cancel
    void cancel();
    void undo();           // Backspace: previous layer of the edit pattern's loop
    void toTrack();        // the loop becomes a sliced sample of the selected track

    int bars() const noexcept { return bars_; }
    void setBars (int b) { bars_ = b == 2 || b == 4 ? b : 1; }
    bool canUndo() const;
    int slotOfEditPattern() const;
    bool busy() const noexcept { return inEngine_ != nullptr; }

    // engine events and retires (ProjectController::drainEngine)
    void onRecorded (int slot);
    void onAborted (int slot);
    bool onRetired (const Sample* s);   // true: kept (history, file job or the current loop), do not delete

    void reset();   // a project was opened: history and in-flight state are dropped

private:
    struct Layer
    {
        const Sample* sample = nullptr;
        std::string path;
        uint64_t seq = 0;
    };

    Layer currentLayer (int slot) const;   // the published loop, also one loaded from a file
    bool keeps (const Sample* s) const;
    void release (const Sample* s);   // a reference went away: delete when the engine gave it back and nobody keeps it
    void pushHistory (int slot, Layer layer);
    void saveWav (int slot, const Sample* s, const std::string& path);
    int freeLoopSlot() const;
    void arm (int slot, int bars, bool overdub, int64_t capacity);

    ProjectController& c_;
    AudioEngine& engine_;
    std::array<std::vector<Layer>, 16> history_ {};
    std::array<Layer, 16> current_ {};
    std::array<int, 16> fileCounter_ {};
    std::set<const Sample*> pinned_, retired_;
    Sample* inEngine_ = nullptr;
    int inEngineSlot_ = -1;
    int bars_ = 1;
    uint64_t seq_ = 0;
    juce::ThreadPool pool_ { 1 };
    std::shared_ptr<bool> alive_ = std::make_shared<bool> (true);
};

} // namespace jackal
