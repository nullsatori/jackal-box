#include "TrackPanel.h"

#include "MidiTargets.h"

#include "Model/ParamSpec.h"
#include "Model/PatternOps.h"

namespace jackal
{

namespace
{
    constexpr std::array<TrackParam, 7> kKnobParams { TrackParam::Volume, TrackParam::Pan, TrackParam::Pitch, TrackParam::Decay,
                                                      TrackParam::Tone, TrackParam::Start, TrackParam::End };

    const char* conditionNames[] { "always", "fill", "not fill", "1st", "1:2", "2:2" };
}

TrackPanel::TrackPanel (ProjectController& controller)
    : c_ (controller), wave (controller),
      velocity (controller, "Velocity"), ratchet (controller, "Ratchet"), velRamp (controller, "Vel ramp"),
      pitchRamp (controller, "Pitch ramp"), pitch (controller, "Step pitch"), sliceField (controller, "Slice"),
      probability (controller, "Probability"), nudge (controller, "Nudge"),
      fx (controller, -1), mst (controller, kMasterChain), gtr (controller, kGuitarChain), vst (controller),
      masterGain (controller, "Master gain")
{
    for (int i = 0; i < static_cast<int> (tabs.size()); ++i)
    {
        auto& b = tabs[static_cast<std::size_t> (i)];
        styleButton (b);
        b.setColour (juce::TextButton::buttonOnColourId, palette::neonCyan);
        b.onClick = [this, i] { c_.setTrackPanelTab (i); };
        addAndMakeVisible (b);
    }
    addChildComponent (fx);
    addChildComponent (mst);
    addChildComponent (gtr);
    addChildComponent (vst);

    masterGain.setRange (kMasterGainSpec.minValue, kMasterGainSpec.maxValue, 0.1);
    masterGain.setDoubleClickReturnValue (true, kMasterGainDefaultDb);
    masterGain.textFromValueFunction = [] (double v) { return juce::String (v, 1) + " dB"; };
    masterGain.onValueChange = [this] { if (! refreshing_) c_.setMasterGainDb (static_cast<float> (masterGain.getValue())); };
    addChildComponent (masterGain);
    masterGainLabel.setText ("MASTER GAIN", juce::dontSendNotification);
    masterGainLabel.setFont (monoFont (11.f));
    masterGainLabel.setColour (juce::Label::textColourId, palette::dim);
    addChildComponent (masterGainLabel);

    name.setFont (monoFont (14.f, true));
    name.setInputRestrictions (16);
    name.onReturnKey = [this] { unfocusAllComponents(); };
    name.onFocusLost = [this] {
        const auto text = name.getText().toUpperCase().substring (0, 16).toStdString();
        c_.beginGesture ("Rename");
        c_.editTrack (c_.selectedTrack(), [&] (TrackSettings& t) { t.name = text; });
    };
    addAndMakeVisible (name);
    addAndMakeVisible (wave);

    styleButton (load);
    load.onClick = [this] { openFileChooser(); };
    addAndMakeVisible (load);

    slice.addItemList ({ "SLICE off", "SLICE 16", "SLICE 32", "SLICE transient" }, 1);
    slice.onChange = [this] { if (! refreshing_) c_.setSliceMode (c_.selectedTrack(), static_cast<SliceMode> (slice.getSelectedItemIndex())); };
    addAndMakeVisible (slice);

    bars.addItemList ({ "1 bar", "2 bars", "4 bars", "8 bars" }, 1);
    bars.onChange = [this] {
        if (refreshing_) return;
        const double values[] { 1.0, 2.0, 4.0, 8.0 };
        c_.setSourceBars (c_.selectedTrack(), values[juce::jlimit (0, 3, bars.getSelectedItemIndex())]);
    };
    addAndMakeVisible (bars);

    choke.addItemList ({ "CHOKE -", "CHOKE 1", "CHOKE 2", "CHOKE 3", "CHOKE 4" }, 1);
    choke.onChange = [this] { if (! refreshing_) c_.setChoke (c_.selectedTrack(), choke.getSelectedItemIndex()); };
    addAndMakeVisible (choke);

    styleButton (fit);
    fit.onClick = [this] { c_.setFitToTempo (c_.selectedTrack(), fit.getToggleState()); };
    addAndMakeVisible (fit);

    for (std::size_t i = 0; i < kKnobParams.size(); ++i)
    {
        const auto p = kKnobParams[i];
        const auto& spec = trackParamSpec (p);
        auto k = std::make_unique<Knob> (controller, juce::String (std::string (spec.label)));
        k->setRange (spec.minValue, spec.maxValue, p == TrackParam::Pitch ? 0.01 : 0.0);
        if (p == TrackParam::Decay)
            k->setSkewFactorFromMidPoint (300.0);
        if (p == TrackParam::Tone)
            k->setSkewFactorFromMidPoint (2000.0);
        k->setNumDecimalPlacesToDisplay (p == TrackParam::Decay || p == TrackParam::Tone ? 0 : 2);
        k->setDoubleClickReturnValue (true, spec.defaultValue);
        k->onValueChange = [this, p, raw = k.get()] {
            if (! refreshing_)
                c_.setTrackParam (c_.selectedTrack(), p, static_cast<float> (raw->getValue()));
        };
        addAndMakeVisible (*k);
        knobs[i] = std::move (k);

        auto& l = knobLabels[i];
        l.setText (juce::String (std::string (spec.label)), juce::dontSendNotification);
        l.setFont (monoFont (11.f));
        l.setColour (juce::Label::textColourId, palette::dim);
        l.setJustificationType (juce::Justification::centred);
        addAndMakeVisible (l);
    }

    stepTitle.setFont (monoFont (13.f, true));
    stepTitle.setColour (juce::Label::textColourId, palette::neonCyan);
    addAndMakeVisible (stepTitle);

    struct FieldSpec { Field* f; const char* label; double lo, hi; std::function<void (Step&, int)> set; };
    const FieldSpec specs[] {
        { &velocity, "VEL", 1, 127, [] (Step& s, int v) { s.velocity = static_cast<uint8_t> (v); } },
        { &ratchet, "RATCH", 1, 64, [] (Step& s, int v) { s.ratchet = static_cast<uint8_t> (v); } },
        { &velRamp, "V.RMP", -100, 100, [] (Step& s, int v) { s.ratchetVelRamp = static_cast<int8_t> (v); } },
        { &pitchRamp, "P.RMP", -24, 24, [] (Step& s, int v) { s.ratchetPitchRamp = static_cast<int8_t> (v); } },
        { &pitch, "PITCH", -24, 24, [] (Step& s, int v) { s.pitch = static_cast<int8_t> (v); } },
        { &sliceField, "SLICE", -1, 63, [] (Step& s, int v) { s.slice = static_cast<int8_t> (v); } },
        { &probability, "PROB", 0, 100, [] (Step& s, int v) { s.probability = static_cast<uint8_t> (v); } },
        { &nudge, "NUDGE", -25, 25, [] (Step& s, int v) { s.nudge = static_cast<int8_t> (v); } },
    };
    for (std::size_t i = 0; i < 8; ++i)
    {
        auto spec = specs[i];
        spec.f->setRange (spec.lo, spec.hi, 1.0);
        spec.f->onValueChange = [this, f = spec.f, set = spec.set] {
            if (! refreshing_)
                editStep ([&] (Step& s) { set (s, juce::roundToInt (f->getValue())); });
        };
        addAndMakeVisible (*spec.f);
        auto& l = fieldLabels[i];
        l.setText (spec.label, juce::dontSendNotification);
        l.setFont (monoFont (11.f));
        l.setColour (juce::Label::textColourId, palette::dim);
        addAndMakeVisible (l);
    }

    styleButton (reverse);
    reverse.onClick = [this] {
        c_.beginGesture ("Reverse");
        editStep ([this] (Step& s) { s.reverse = reverse.getToggleState(); });
    };
    addAndMakeVisible (reverse);

    for (int i = 0; i < 6; ++i)
        condition.addItem (conditionNames[i], i + 1);
    condition.onChange = [this] {
        if (refreshing_) return;
        c_.beginGesture ("Condition");
        const auto cond = static_cast<StepCondition> (condition.getSelectedItemIndex());
        editStep ([cond] (Step& s) { s.condition = cond; });
    };
    addAndMakeVisible (condition);

    for (int i = 0; i < kMaxPlocksPerStep; ++i)
    {
        auto& b = plockButtons[static_cast<std::size_t> (i)];
        styleButton (b);
        b.setColour (juce::TextButton::textColourOffId, palette::magenta);
        b.setTooltip ("click to remove this p-lock");
        b.onClick = [this, i] {
            c_.beginGesture ("Remove p-lock");
            editStep ([i] (Step& s) {
                auto& pl = s.plocks;
                for (int k = i; k + 1 < kMaxPlocksPerStep; ++k)
                    pl[static_cast<std::size_t> (k)] = pl[static_cast<std::size_t> (k + 1)];
                pl.back() = {};
            });
        };
        addAndMakeVisible (b);
    }

    startTimerHz (30);
    refresh();
}

std::vector<juce::Component*> TrackPanel::srcComponents()
{
    std::vector<juce::Component*> v { &name, &wave, &load, &slice, &bars, &choke, &fit };
    for (auto& k : knobs) v.push_back (k.get());
    for (auto& l : knobLabels) v.push_back (&l);
    return v;
}

std::vector<juce::Component*> TrackPanel::stepComponents()
{
    std::vector<juce::Component*> v { &stepTitle, &velocity, &ratchet, &velRamp, &pitchRamp, &pitch, &sliceField, &probability, &nudge,
                                      &reverse, &condition };
    for (auto& l : fieldLabels) v.push_back (&l);
    return v;
}

void TrackPanel::showTab (int tab)
{
    shownTab_ = tab;
    for (int i = 0; i < static_cast<int> (tabs.size()); ++i)
        tabs[static_cast<std::size_t> (i)].setToggleState (i == tab, juce::dontSendNotification);
    for (auto* comp : srcComponents())
        comp->setVisible (tab == 0);
    fx.setVisible (tab == 1);
    vst.setVisible (tab == 2);
    for (auto* comp : stepComponents())
        comp->setVisible (tab == 3);
    mst.setVisible (tab == 4);
    masterGain.setVisible (tab == 4);
    masterGainLabel.setVisible (tab == 4);
    gtr.setVisible (tab == 5);
    refresh();
    repaint();
}

void TrackPanel::editStep (const std::function<void (Step&)>& fn)
{
    const auto s = c_.lastTouched();
    c_.editPattern ([&] (Pattern& p) { fn (ops::step (p, s.track, s.slot)); });
}

void TrackPanel::openFileChooser()
{
    chooser = std::make_unique<juce::FileChooser> ("Load a sample", juce::File(), "*.wav;*.aif;*.aiff");
    const int track = c_.selectedTrack();
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                          [this, track] (const juce::FileChooser& fc) {
                              const auto f = fc.getResult();
                              if (f.existsAsFile())
                                  c_.importFile (track, f);
                          });
}

void TrackPanel::timerCallback()
{
    if (c_.version() != shownVersion_)
    {
        shownVersion_ = c_.version();
        refresh();
    }
}

void TrackPanel::refresh()
{
    if (c_.trackPanelTab() != shownTab_)
    {
        showTab (c_.trackPanelTab());
        return;
    }
    const juce::ScopedValueSetter<bool> guard (refreshing_, true);
    if (! masterGain.isMouseButtonDown())
        masterGain.setValue (c_.project().masterGainDb, juce::dontSendNotification);
    const int t = c_.selectedTrack();
    const auto& tr = c_.project().tracks[static_cast<std::size_t> (t)];

    if (! name.hasKeyboardFocus (true))
        name.setText (juce::String (tr.name), juce::dontSendNotification);

    const int slot = tr.sampleSlot;
    const SampleSlotSpec* spec = slot >= 0 ? &c_.project().slots[static_cast<std::size_t> (slot)] : nullptr;
    slice.setSelectedItemIndex (spec != nullptr ? static_cast<int> (spec->sliceMode) : 0, juce::dontSendNotification);
    slice.setEnabled (spec != nullptr && spec->used);
    const double b = spec != nullptr ? spec->sourceBars : 0.0;
    bars.setSelectedItemIndex (b >= 8 ? 3 : b >= 4 ? 2 : b >= 2 ? 1 : 0, juce::dontSendNotification);
    bars.setEnabled (spec != nullptr && spec->used);
    choke.setSelectedItemIndex (tr.chokeGroup, juce::dontSendNotification);
    fit.setToggleState (tr.fitToTempo, juce::dontSendNotification);

    // knobs show the p-lock of the held step if there is one
    const auto hold = c_.plockTarget();
    const Step* held = nullptr;
    if (hold && hold->track == t)
        held = &c_.editPattern().lanes[static_cast<std::size_t> (hold->track)].steps[static_cast<std::size_t> (hold->slot)];

    for (std::size_t i = 0; i < kKnobParams.size(); ++i)
    {
        const auto p = kKnobParams[i];
        float value = tr.params[static_cast<std::size_t> (p)];
        bool locked = false;
        if (held != nullptr)
            for (const auto& pl : held->plocks)
                if (pl.param == static_cast<uint8_t> (p))
                {
                    value = pl.value;
                    locked = true;
                }
        knobs[i]->setLocked (locked || held != nullptr);
        if (! knobs[i]->isMouseButtonDown())
            knobs[i]->setValue (value, juce::dontSendNotification);
        setMidiTarget (*knobs[i], MidiTarget::ofParam (trackParam (c_.selectedTrack(), p)));   // stage 5
    }
    setMidiTarget (masterGain, MidiTarget::ofParam (masterParam (MasterParam::Gain)));

    const auto s = c_.lastTouched();
    const auto& st = c_.editPattern().lanes[static_cast<std::size_t> (s.track)].steps[static_cast<std::size_t> (s.slot)];
    stepTitle.setText ("STEP  T" + juce::String (s.track + 1) + " S" + juce::String (s.slot + 1) + (st.active ? "" : "  (off)"),
                       juce::dontSendNotification);
    velocity.setValue (st.velocity, juce::dontSendNotification);
    ratchet.setValue (st.ratchet, juce::dontSendNotification);
    velRamp.setValue (st.ratchetVelRamp, juce::dontSendNotification);
    pitchRamp.setValue (st.ratchetPitchRamp, juce::dontSendNotification);
    pitch.setValue (st.pitch, juce::dontSendNotification);
    sliceField.setValue (st.slice, juce::dontSendNotification);
    probability.setValue (st.probability, juce::dontSendNotification);
    nudge.setValue (st.nudge, juce::dontSendNotification);
    reverse.setToggleState (st.reverse, juce::dontSendNotification);
    condition.setSelectedItemIndex (static_cast<int> (st.condition), juce::dontSendNotification);

    for (int i = 0; i < kMaxPlocksPerStep; ++i)
    {
        const auto& pl = st.plocks[static_cast<std::size_t> (i)];
        auto& b = plockButtons[static_cast<std::size_t> (i)];
        b.setVisible (shownTab_ == 3 && pl.param < kNumTrackParams);
        if (pl.param < kNumTrackParams)
            b.setButtonText (juce::String (std::string (kTrackParamSpecs[pl.param].label)) + " " + juce::String (pl.value, 2) + "  x");
    }

    wave.repaint();
}

void TrackPanel::paint (juce::Graphics& g)
{
    g.fillAll (palette::panel);
    g.setColour (palette::gridLine);
    g.drawVerticalLine (0, 0.f, static_cast<float> (getHeight()));
}

void TrackPanel::resized()
{
    auto r = getLocalBounds();
    auto tabRow = r.removeFromTop (24).reduced (6, 2);
    const int tw = tabRow.getWidth() / static_cast<int> (tabs.size());
    for (auto& b : tabs)
        b.setBounds (tabRow.removeFromLeft (tw).reduced (1, 0));

    fx.setBounds (r);
    gtr.setBounds (r);
    vst.setBounds (r);
    auto m = r;
    mst.setBounds (m.removeFromTop (m.getHeight() - 70));
    auto g = m.reduced (8, 4);
    masterGainLabel.setBounds (g.removeFromTop (14));
    masterGain.setBounds (g.removeFromLeft (120));

    layoutSrc (r.reduced (8, 6));
    layoutStep (r.reduced (8, 6));
}

void TrackPanel::layoutSrc (juce::Rectangle<int> r)
{
    name.setBounds (r.removeFromTop (24));
    r.removeFromTop (4);
    wave.setBounds (r.removeFromTop (80));
    r.removeFromTop (4);

    auto row = r.removeFromTop (22);
    load.setBounds (row.removeFromLeft (70));
    row.removeFromLeft (4);
    slice.setBounds (row.removeFromLeft (122));
    row.removeFromLeft (4);
    fit.setBounds (row);
    r.removeFromTop (4);
    row = r.removeFromTop (22);
    bars.setBounds (row.removeFromLeft (100));
    row.removeFromLeft (4);
    choke.setBounds (row.removeFromLeft (100));
    r.removeFromTop (6);

    const int kw = r.getWidth() / 4;
    for (int line = 0; line < 2; ++line)
    {
        auto labels = r.removeFromTop (14);
        auto knobRow = r.removeFromTop (56);
        for (int i = 0; i < 4; ++i)
        {
            const int k = line * 4 + i;
            if (k >= static_cast<int> (knobs.size()))
                break;
            knobLabels[static_cast<std::size_t> (k)].setBounds (labels.removeFromLeft (kw));
            knobs[static_cast<std::size_t> (k)]->setBounds (knobRow.removeFromLeft (kw));
        }
    }

}

void TrackPanel::layoutStep (juce::Rectangle<int> r)
{
    stepTitle.setBounds (r.removeFromTop (20));

    Field* fields[] { &velocity, &ratchet, &velRamp, &pitchRamp, &pitch, &sliceField, &probability, &nudge };
    const int colW = r.getWidth() / 2;
    for (int i = 0; i < 4; ++i)
    {
        auto line = r.removeFromTop (20);
        for (int k = 0; k < 2; ++k)
        {
            const int f = i * 2 + k;
            auto cell = line.removeFromLeft (colW).reduced (2, 1);
            fieldLabels[static_cast<std::size_t> (f)].setBounds (cell.removeFromLeft (44));
            fields[f]->setBounds (cell);
        }
        r.removeFromTop (2);
    }
    auto line = r.removeFromTop (22);
    reverse.setBounds (line.removeFromLeft (56));
    line.removeFromLeft (6);
    condition.setBounds (line.removeFromLeft (110));
    r.removeFromTop (4);
    for (auto& b : plockButtons)
    {
        b.setBounds (r.removeFromTop (18));
        r.removeFromTop (2);
    }
}

} // namespace jackal
