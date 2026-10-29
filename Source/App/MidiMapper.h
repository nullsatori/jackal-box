#pragma once

#include "AppSettings.h"
#include "AudioEngine.h"
#include "LearnOverlay.h"
#include "MidiTargets.h"
#include "ProjectController.h"

#include "Model/MidiMap.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace jackal
{

// Message thread, 60 Hz (contract stage 5, §9.5): drains AudioEngine::uiMidi(), runs learn, owns midimap.json,
// publishes the engine's note map and marks bound controls for the look and feel.
class MidiMapper : private juce::Timer
{
public:
    MidiMapper (AudioEngine& engine, ProjectController& controller, AppSettings& settings);
    ~MidiMapper() override;

    void attach (juce::Component& root);   // the main component: learn overlay, right-click menu, feedback
    void start();                          // reads midimap.json, publishes the note map, starts the timer
    void layout();                         // the root was resized

    const MidiMap& map() const noexcept { return map_; }
    void setChannel (int channel);         // 0 = omni
    void setDefaultNotes (bool on);
    void removeBinding (int index);
    void clearAll();

    // learn
    void beginLearn();                                         // Ctrl+L / LEARN: pick a control first
    void learnTarget (const MidiTarget&, juce::Component* shown = nullptr);   // skip the pick (TRACK HIT buttons, menu)
    void cancelLearn();
    bool learning() const noexcept { return state_ != State::Off; }

    bool ledOn() const;   // any MIDI event in the last 120 ms
    uint32_t version() const noexcept { return version_; }   // bumps when the map changes

    static juce::String describe (const MidiTarget&);   // "T7 SHAKAL", "PAD 3", "TRACK HIT 1"
    static juce::String describe (const MidiBinding&);  // "CC 21 (ch 1) -> T7 SHAKAL"

private:
    enum class State { Off, PickTarget, WaitSource };

    void timerCallback() override;
    void handle (const MidiEvent&);
    void picked (juce::Component* comp, const juce::String& target);
    void contextMenu (juce::Component* comp, const juce::String& target);
    void mapChanged (const juce::String& status = {});
    void publishNoteMap();
    void save();
    void refreshFeedback();
    void setLearning (juce::Component*, const juce::String& target = {});
    friend juce::String midifeedback::mappedSource (const juce::String&);
    friend bool midifeedback::isLearning (const juce::String&);
    juce::File file() const;

    AudioEngine& engine_;
    ProjectController& c_;
    AppSettings& settings_;
    juce::Component* root_ = nullptr;
    LearnOverlay overlay_;

    MidiMap map_;
    State state_ = State::Off;
    MidiTarget target_;
    juce::Component::SafePointer<juce::Component> learningComp_;
    juce::String learningTarget_;
    double learnStartMs_ = 0.0, lastEventMs_ = -1.0e9;
    int tick_ = 0;
    uint32_t version_ = 1;
};

} // namespace jackal
