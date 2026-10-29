#include "MidiMapper.h"

#include "JackalLookAndFeel.h"
#include "Plugins/InsertHost.h"

#include "Model/MidiDispatcher.h"
#include "Model/MidiMapJson.h"
#include "Model/ParamSpec.h"

namespace jackal
{

namespace
{
    constexpr double kLearnTimeoutMs = 10000.0;
    constexpr double kLedMs = 120.0;

    juce::String chainName (int chain)
    {
        if (chain == kMasterChain) return "MASTER";
        if (chain == kGuitarChain) return "GTR";
        return "T" + juce::String (chain + 1);
    }

    juce::String upper (std::string_view s) { return juce::String (std::string (s)).toUpperCase(); }

    MidiMapper* instance = nullptr;   // for the painted targets (midifeedback); one mapper per app
} // namespace

namespace midifeedback
{
    juce::String mappedSource (const juce::String& target)
    {
        MidiTarget t;
        if (instance == nullptr || ! targetFromString (target.toStdString(), t))
            return {};
        const auto* b = findByTarget (instance->map_, t);
        return b != nullptr ? juce::String (sourceToString (b->source)) : juce::String();
    }

    bool isLearning (const juce::String& target)
    {
        return instance != nullptr && instance->state_ == MidiMapper::State::WaitSource && instance->learningTarget_ == target;
    }

    void drawMarks (juce::Graphics& g, juce::Rectangle<float> r, const juce::String& target)
    {
        if (mappedSource (target).isNotEmpty())
        {
            g.setColour (palette::dim);
            g.fillEllipse (r.getX() + 2.f, r.getY() + 2.f, 3.f, 3.f);
        }
        if (isLearning (target))
        {
            g.setColour (palette::magenta);
            g.drawRect (r, 1.f);
        }
    }
} // namespace midifeedback

MidiMapper::MidiMapper (AudioEngine& engine, ProjectController& controller, AppSettings& settings)
    : engine_ (engine), c_ (controller), settings_ (settings)
{
    overlay_.onPick = [this] (juce::Component* comp, const juce::String& t) { picked (comp, t); };
    overlay_.onContextMenu = [this] (juce::Component* comp, const juce::String& t) { contextMenu (comp, t); };
    instance = this;
}

MidiMapper::~MidiMapper()
{
    instance = nullptr;
    stopTimer();
    if (root_ != nullptr)
        root_->removeChildComponent (&overlay_);
}

void MidiMapper::attach (juce::Component& root)
{
    root_ = &root;
    root.addAndMakeVisible (overlay_);
    overlay_.setAlwaysOnTop (true);
    overlay_.setBounds (root.getLocalBounds());
}

void MidiMapper::layout()
{
    if (root_ != nullptr)
        overlay_.setBounds (root_->getLocalBounds());
}

juce::File MidiMapper::file() const
{
    return settings_.folder().getChildFile ("midimap.json");
}

void MidiMapper::start()
{
    const auto f = file();
    if (f.existsAsFile())
    {
        auto parsed = midiMapFromJson (f.loadFileAsString().toStdString());
        if (parsed.ok)
        {
            map_ = std::move (parsed.map);
            if (parsed.skipped > 0)
                c_.setStatus ("midimap.json: " + juce::String (parsed.skipped) + " unknown bindings skipped");
        }
        else
        {
            map_ = {};
            c_.setStatus ("midimap.json: " + juce::String (parsed.error) + " (empty map)", true);
        }
    }
    publishNoteMap();
    refreshFeedback();
    startTimerHz (60);
}

// ---------------------------------------------------------------------------- map edits

void MidiMapper::mapChanged (const juce::String& status)
{
    save();
    publishNoteMap();
    ++version_;
    refreshFeedback();
    if (status.isNotEmpty())
        c_.setStatus (status);
    else
        c_.touch();   // painted targets repaint on the controller version
}

void MidiMapper::setChannel (int channel)
{
    map_.channel = static_cast<uint8_t> (juce::jlimit (0, 16, channel));
    mapChanged();
}

void MidiMapper::setDefaultNotes (bool on)
{
    map_.defaultNotes = on;
    mapChanged();
}

void MidiMapper::removeBinding (int index)
{
    if (index < 0 || index >= static_cast<int> (map_.bindings.size()))
        return;
    const auto text = describe (map_.bindings[static_cast<std::size_t> (index)]);
    map_.bindings.erase (map_.bindings.begin() + index);
    mapChanged ("MIDI cleared: " + text);
}

void MidiMapper::clearAll()
{
    map_.bindings.clear();
    mapChanged ("MIDI map cleared");
}

void MidiMapper::save()
{
    const auto f = file();
    f.getParentDirectory().createDirectory();
    if (! f.replaceWithText (juce::String (toJson (map_))))
        c_.setStatus ("cannot write " + f.getFullPathName(), true);
}

void MidiMapper::publishNoteMap()
{
    auto* m = new MidiNoteMap (buildNoteMap (map_));
    if (! engine_.core().commands().push ({ CommandType::SetNoteMap, 0.f, 0, m }))
    {
        delete m;
        c_.setStatus ("engine busy, MIDI note map not updated", true);
    }
}

// ---------------------------------------------------------------------------- learn

void MidiMapper::beginLearn()
{
    if (root_ == nullptr)
        return;
    setLearning (nullptr);
    state_ = State::PickTarget;
    learnStartMs_ = juce::Time::getMillisecondCounterHiRes();
    overlay_.setBounds (root_->getLocalBounds());
    overlay_.setPicking (true);
    overlay_.toFront (false);
    c_.setStatus (juce::String (juce::CharPointer_UTF8 ("LEARN: click a knob or button\xe2\x80\xa6 (Esc cancels)")));
}

void MidiMapper::learnTarget (const MidiTarget& t, juce::Component* shown)
{
    overlay_.setPicking (false);
    target_ = t;
    state_ = State::WaitSource;
    learnStartMs_ = juce::Time::getMillisecondCounterHiRes();
    // a painted area (A..H, pads) draws its own outline; only real controls get the property
    const bool control = shown != nullptr && shown->getProperties().contains (kMidiTargetProperty);
    setLearning (control ? shown : nullptr, juce::String (targetToString (t)));
    c_.setStatus ("LEARN: " + describe (t) + juce::String (juce::CharPointer_UTF8 (" \xe2\x80\x94 move a control\xe2\x80\xa6")));
}

void MidiMapper::cancelLearn()
{
    if (state_ == State::Off)
        return;
    state_ = State::Off;
    overlay_.setPicking (false);
    setLearning (nullptr);
    c_.setStatus ("learn cancelled");
}

void MidiMapper::setLearning (juce::Component* comp, const juce::String& target)
{
    learningTarget_ = target;
    if (auto* old = learningComp_.getComponent())
    {
        old->getProperties().remove (kLearningProperty);
        old->repaint();
    }
    learningComp_ = comp;
    if (comp != nullptr)
    {
        comp->getProperties().set (kLearningProperty, true);
        comp->repaint();
    }
}

void MidiMapper::picked (juce::Component* comp, const juce::String& target)
{
    MidiTarget t;
    if (comp == nullptr || ! targetFromString (target.toStdString(), t))
    {
        c_.setStatus (juce::String (juce::CharPointer_UTF8 ("LEARN: not mappable, click a knob or button\xe2\x80\xa6")));
        return;
    }
    learnTarget (t, comp);
}

void MidiMapper::contextMenu (juce::Component* comp, const juce::String& target)
{
    MidiTarget t;
    if (comp == nullptr || ! targetFromString (target.toStdString(), t))
        return;

    juce::PopupMenu m;
    m.addSectionHeader (describe (t));
    m.addItem (1, "MIDI learn");
    if (const auto* b = findByTarget (map_, t))
        m.addItem (2, "Clear MIDI (" + juce::String (sourceToString (b->source)) + ")");

    juce::Component::SafePointer<juce::Component> safe (comp);
    m.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (comp), [this, t, safe] (int r) {
        if (r == 1)
            learnTarget (t, safe.getComponent());
        else if (r == 2)
        {
            unbindTarget (map_, t);
            mapChanged ("MIDI cleared: " + describe (t));
        }
    });
}

// ---------------------------------------------------------------------------- events

void MidiMapper::timerCallback()
{
    MidiEvent e;
    while (engine_.uiMidi().pop (e))
        handle (e);

    const double now = juce::Time::getMillisecondCounterHiRes();
    if (state_ == State::WaitSource && now - learnStartMs_ > kLearnTimeoutMs)
    {
        state_ = State::Off;
        setLearning (nullptr);
        c_.setStatus ("LEARN: nothing came in 10 s (check the MIDI input in AUDIO/MIDI)");
    }

    // track knobs change their target with the selected track: keep the dots honest at 2 Hz
    if (++tick_ % 30 == 0)
        refreshFeedback();
}

void MidiMapper::handle (const MidiEvent& e)
{
    const int type = e.status & 0xF0;
    if (type == 0x80 || type == 0x90 || type == 0xA0 || type == 0xB0 || type == 0xC0 || type == 0xD0 || type == 0xE0)
        lastEventMs_ = juce::Time::getMillisecondCounterHiRes();

    if (state_ == State::WaitSource)
    {
        const bool noteOn = type == 0x90 && e.data2 > 0;
        if (noteOn || type == 0xB0)
        {
            MidiSource src;
            src.type = noteOn ? MidiSourceType::Note : MidiSourceType::Cc;
            src.number = static_cast<uint8_t> (e.data1 & 0x7F);
            src.channel = map_.channel == 0 ? static_cast<uint8_t> ((e.status & 0x0F) + 1) : uint8_t (0);
            bind (map_, { src, target_ });
            state_ = State::Off;
            setLearning (nullptr);
            mapChanged (juce::String (sourceToString (src)) + juce::String (juce::CharPointer_UTF8 (" \xe2\x86\x92 ")) + describe (target_));
            return;   // the event that taught the binding does not also act
        }
        return;   // NoteOff and the rest do not count as a source
    }

    const auto out = dispatch (map_, e);
    if (out.kind == MidiOutcome::Kind::Param)
        c_.setParamFromMidi (out.param, out.value);
    else if (out.kind == MidiOutcome::Kind::Action)
        c_.performAction (out.action, out.index, out.pressed, out.velocity);
}

bool MidiMapper::ledOn() const
{
    return juce::Time::getMillisecondCounterHiRes() - lastEventMs_ < kLedMs;
}

// ---------------------------------------------------------------------------- feedback

void MidiMapper::refreshFeedback()
{
    if (root_ == nullptr)
        return;

    std::function<void (juce::Component&)> walk = [&] (juce::Component& comp) {
        auto& props = comp.getProperties();
        if (props.contains (kMidiTargetProperty))
        {
            MidiTarget t;
            juce::String mapped;
            if (targetFromString (props[kMidiTargetProperty].toString().toStdString(), t))
                if (const auto* b = findByTarget (map_, t))
                    mapped = juce::String (sourceToString (b->source));
            if (props[kMidiMappedProperty].toString() != mapped)
            {
                if (mapped.isEmpty())
                    props.remove (kMidiMappedProperty);
                else
                    props.set (kMidiMappedProperty, mapped);
                comp.repaint();
            }
        }
        for (auto* child : comp.getChildren())
            walk (*child);
    };
    walk (*root_);
}

// ---------------------------------------------------------------------------- text

juce::String MidiMapper::describe (const MidiTarget& t)
{
    if (t.kind == MidiTarget::Kind::Action)
    {
        const int n = t.index + 1;
        switch (t.action)
        {
            case ActionId::Pattern:     return "PATTERN " + juce::String::charToString (static_cast<juce::juce_wchar> ('A' + t.index));
            case ActionId::Pad:         return "PAD " + juce::String (n);
            case ActionId::TrackHit:    return "TRACK HIT " + juce::String (n);
            case ActionId::Mute:        return "T" + juce::String (n) + " MUTE";
            case ActionId::Solo:        return "T" + juce::String (n) + " SOLO";
            case ActionId::SelectTrack: return "SELECT T" + juce::String (n);
            case ActionId::TogglePlay:  return "PLAY";
            case ActionId::SongMode:    return "SONG";
            case ActionId::LoopRec:     return "LOOP REC";
            case ActionId::LoopDub:     return "LOOP DUB";
            case ActionId::LoopUndo:    return "LOOP UNDO";
            case ActionId::Reroll:      return "RR";
            default:                    return upper (actionName (t.action));
        }
    }

    const ParamId id = t.param;
    if (! isDefined (id))
        return juce::String (address (id));
    const auto label = upper (info (id).label);

    if (id < kTrackBase)
        return "MASTER " + label;
    if (id < kGlitchBase)
        return "T" + juce::String ((id - kTrackBase) / kTrackStride + 1) + " " + label;
    if (id >= kFxBase && id < kFxBase + kNumChains * kFxStride)
        return chainName ((id - kFxBase) / kFxStride) + " " + label;
    if (id >= kGuitarBase && id < kInsertBase)
        return "GTR " + label;
    if (id >= kInsertBase)
    {
        const int n = id - kInsertBase;
        return InsertHost::slotName (n / kSlotsPerChain, n % kSlotsPerChain) + " BYP";
    }
    return label;
}

juce::String MidiMapper::describe (const MidiBinding& b)
{
    return juce::String (sourceToString (b.source)) + juce::String (juce::CharPointer_UTF8 (" \xe2\x86\x92 ")) + describe (b.target);
}

} // namespace jackal
