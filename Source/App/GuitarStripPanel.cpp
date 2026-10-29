#include "GuitarStripPanel.h"

#include "MidiTargets.h"

#include "JackalLookAndFeel.h"

#include "Model/ParamSpec.h"

#include <cmath>

namespace jackal
{

namespace
{
    const char* kPresetLetters[] { "I", "N", "M" };
}

GuitarStripPanel::GuitarStripPanel (ProjectController& c, AudioEngine& engine, AppSettings& settings)
    : c_ (c), engine_ (engine), settings_ (settings),
      gain (c, "Monitor gain"), gate (c, "Gate"), hpf (c, "Low cut"), shakal (c, "Guitar SHAKAL"), loopGain (c, "Loop level"),
      slot1 (c, [] { return kGuitarChain; }, 0), slot2 (c, [] { return kGuitarChain; }, 1)
{
    setupKnob (gain, gainL, "GAIN", -60.0, 12.0, 0.0);
    setupKnob (gate, gateL, "GATE", -80.0, 0.0, -80.0);
    setupKnob (hpf, hpfL, "HPF", 20.0, 300.0, 20.0);
    setupKnob (shakal, shakalL, "SHAKAL", 0.0, 1.0, 0.0);
    setupKnob (loopGain, loopL, "LOOP", -60.0, 12.0, 0.0);
    hpf.setSkewFactorFromMidPoint (80.0);
    shakal.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);   // the preset letter sits under it
    gain.textFromValueFunction = [] (double v) { return v <= -60.0 ? juce::String ("-inf") : juce::String (v, 1); };
    gate.textFromValueFunction = [] (double v) { return v <= -80.0 ? juce::String ("off") : juce::String (v, 0); };
    hpf.textFromValueFunction = [] (double v) { return v <= 20.0 ? juce::String ("off") : juce::String (juce::roundToInt (v)); };
    shakal.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)); };
    loopGain.textFromValueFunction = [] (double v) { return v <= -60.0 ? juce::String ("-inf") : juce::String (v, 1); };
    for (auto* k : { &gain, &gate, &hpf, &shakal, &loopGain })
        k->updateText();

    gain.onValueChange = [this] {
        if (refreshing_) return;
        const auto db = static_cast<float> (gain.getValue());
        engine_.core().params().monitorGainDb.store (db);
        settings_.setMonitorGainDb (db);
    };
    gate.onValueChange = [this] { if (! refreshing_) c_.setGuitarParam (GuitarParam::GateDb, static_cast<float> (gate.getValue())); };
    hpf.onValueChange = [this] { if (! refreshing_) c_.setGuitarParam (GuitarParam::HpfHz, static_cast<float> (hpf.getValue())); };
    shakal.onValueChange = [this] { if (! refreshing_) c_.setShakal (kGuitarChain, static_cast<float> (shakal.getValue())); };
    loopGain.onValueChange = [this] { if (! refreshing_) c_.setGuitarParam (GuitarParam::LoopGainDb, static_cast<float> (loopGain.getValue())); };

    presetLetter.setFont (monoFont (11.f, true));
    presetLetter.setColour (juce::Label::textColourId, palette::nixie);
    presetLetter.setJustificationType (juce::Justification::centred);
    presetLetter.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (presetLetter);

    for (auto* s : { &slot1, &slot2 })
    {
        s->onBrowse = [this] (int ch, int sl) { if (onBrowse) onBrowse (ch, sl); };
        addAndMakeVisible (*s);
    }
    addAndMakeVisible (loopView);

    for (auto* b : { &rec, &dub, &undo, &toTrack, &tap })
    {
        styleButton (*b);
        addAndMakeVisible (*b);
    }
    rec.onClick = [this] { c_.loops().busy() ? c_.loops().cancel() : c_.loops().rec (c_.loops().bars()); };
    dub.onClick = [this] { c_.loops().busy() ? c_.loops().cancel() : c_.loops().dub(); };
    undo.onClick = [this] { c_.loops().undo(); };
    toTrack.onClick = [this] { c_.loops().toTrack(); };
    // stage 5: MIDI learn targets
    setMidiTarget (rec, MidiTarget::ofAction (ActionId::LoopRec));
    setMidiTarget (dub, MidiTarget::ofAction (ActionId::LoopDub));
    setMidiTarget (undo, MidiTarget::ofAction (ActionId::LoopUndo));
    setMidiTarget (tap, MidiTarget::ofParam (guitarParam (GuitarParam::LoopTap)));
    setMidiTarget (gate, MidiTarget::ofParam (guitarParam (GuitarParam::GateDb)));
    setMidiTarget (hpf, MidiTarget::ofParam (guitarParam (GuitarParam::HpfHz)));
    setMidiTarget (shakal, MidiTarget::ofParam (guitarParam (GuitarParam::Shakal)));
    setMidiTarget (loopGain, MidiTarget::ofParam (guitarParam (GuitarParam::LoopGainDb)));
    tap.setClickingTogglesState (false);
    tap.onClick = [this] {
        c_.beginGesture ("Loop tap");
        c_.setGuitarParam (GuitarParam::LoopTap, c_.guitar().loopTap == LoopTap::Amp ? 0.f : 1.f);
    };

    bars.addItemList ({ "1", "2", "4" }, 1);
    bars.setSelectedItemIndex (0, juce::dontSendNotification);
    bars.onChange = [this] { const int v[] { 1, 2, 4 }; c_.loops().setBars (v[juce::jlimit (0, 2, bars.getSelectedItemIndex())]); };
    bars.setTooltip ("BARS: loop length in pattern cycles");
    addAndMakeVisible (bars);

    startTimerHz (30);
    refresh();
}

void GuitarStripPanel::setupKnob (Knob& k, juce::Label& l, const char* label, double lo, double hi, double def)
{
    k.setRange (lo, hi, 0.0);
    k.setDoubleClickReturnValue (true, def);
    k.setTextBoxStyle (juce::Slider::TextBoxBelow, false, 46, 12);
    addAndMakeVisible (k);
    l.setText (label, juce::dontSendNotification);
    l.setFont (monoFont (10.f));
    l.setColour (juce::Label::textColourId, palette::dim);
    l.setJustificationType (juce::Justification::centred);
    l.setInterceptsMouseClicks (false, false);
    addAndMakeVisible (l);
}

void GuitarStripPanel::refresh()
{
    const juce::ScopedValueSetter<bool> guard (refreshing_, true);
    const auto& g = c_.guitar();
    auto set = [] (Knob& k, double v) { if (! k.isMouseButtonDown()) k.setValue (v, juce::dontSendNotification); };
    set (gain, engine_.core().params().monitorGainDb.load());
    set (gate, g.gateDb);
    set (hpf, g.hpfHz);
    set (shakal, g.shakal);
    set (loopGain, g.loopGainDb);
    presetLetter.setText (kPresetLetters[juce::jlimit (0, 2, static_cast<int> (g.fx.preset))], juce::dontSendNotification);
    tap.setButtonText (g.loopTap == LoopTap::Amp ? "AMP" : "DI");
    undo.setEnabled (c_.loops().canUndo());
    toTrack.setEnabled (c_.loops().slotOfEditPattern() >= 0);
}

void GuitarStripPanel::timerCallback()
{
    if (c_.version() != shownVersion_)
    {
        shownVersion_ = c_.version();
        refresh();
    }
    snap_ = c_.loopSnapshot();
    const float peak = engine_.core().inputMeter().peak();
    level_ = juce::jmax (peak, level_ * 0.85f);

    const bool blink = (juce::Time::getMillisecondCounter() / 250) % 2 == 0;   // 2 Hz
    auto style = [&] (juce::TextButton& b, bool mine, juce::Colour col) {
        const bool armed = mine && snap_.state == LoopState::Armed;
        const bool recording = mine && snap_.state == LoopState::Recording;
        b.setColour (juce::TextButton::buttonColourId, recording ? col.withAlpha (0.25f) : palette::panel);
        b.setColour (juce::TextButton::textColourOffId, armed ? (blink ? col : palette::dim) : recording ? col : palette::text);
    };
    style (rec, ! snap_.overdub, palette::rec);
    style (dub, snap_.overdub, palette::nixie);
    loopView.repaint();
    repaint (meter_);
}

void GuitarStripPanel::LoopView::paint (juce::Graphics& g)
{
    auto& c = owner.c_;
    const auto& snap = owner.snap_;
    const auto r = getLocalBounds().toFloat();
    g.setColour (palette::background);
    g.fillRect (r);
    g.setColour (palette::gridLine);
    g.drawRect (r, 1.f);

    const int slot = c.loops().slotOfEditPattern();
    if (snap.state == LoopState::Recording && snap.lengthSamples > 0)
    {
        const float f = static_cast<float> (juce::jlimit<int64_t> (0, snap.lengthSamples, snap.recIndex)) / static_cast<float> (snap.lengthSamples);
        g.setColour (palette::rec.withAlpha (0.6f));
        g.fillRect (r.reduced (2.f).withWidth ((r.getWidth() - 4.f) * f));
        g.setColour (palette::text);
        g.setFont (monoFont (12.f, true));
        g.drawText (snap.overdub ? "DUB" : "REC", getLocalBounds(), juce::Justification::centred, false);
        return;
    }
    if (snap.state == LoopState::Armed)
    {
        const auto t = c.transport();
        const double spb = t.bpm > 0.0 ? c.sampleRate() * 60.0 / t.bpm : 1.0;
        const int beats = static_cast<int> (std::ceil (static_cast<double> (snap.startSample - t.positionSamples) / spb));
        g.setColour (palette::rec);
        g.setFont (monoFont (22.f, true));
        g.drawText (juce::String (-juce::jmax (1, beats)), getLocalBounds(), juce::Justification::centred, false);
        return;
    }

    const auto* peaks = slot >= 0 ? c.peaksForSlot (slot) : nullptr;
    if (peaks == nullptr || peaks->mins.empty())
    {
        g.setColour (palette::dim);
        g.setFont (monoFont (11.f));
        g.drawText ("no loop: Enter records one", getLocalBounds(), juce::Justification::centred, false);
        return;
    }
    const float mid = r.getCentreY(), half = r.getHeight() * 0.45f;
    const int n = static_cast<int> (peaks->mins.size());
    g.setColour (palette::nixie.withAlpha (0.8f));
    for (int x = 0; x < getWidth(); ++x)
    {
        const int b = juce::jlimit (0, n - 1, x * n / juce::jmax (1, getWidth()));
        g.drawVerticalLine (x, mid - peaks->maxs[static_cast<std::size_t> (b)] * half, mid - peaks->mins[static_cast<std::size_t> (b)] * half + 1.f);
    }
    if (snap.playIndex >= 0 && peaks->length > 0)
    {
        const float x = static_cast<float> (snap.playIndex) / static_cast<float> (peaks->length) * r.getWidth();
        g.setColour (palette::neonCyan);
        g.drawVerticalLine (static_cast<int> (x), r.getY(), r.getBottom());
    }
}

void GuitarStripPanel::paint (juce::Graphics& g)
{
    g.fillAll (palette::panel);
    g.setColour (palette::gridLine);
    g.drawHorizontalLine (0, 0.f, static_cast<float> (getWidth()));

    // raw input level, as in the monitor panel
    const auto m = meter_.toFloat();
    g.setColour (palette::background);
    g.fillRect (m);
    const float db = level_ > 1e-6f ? 20.f * std::log10 (level_) : -60.f;
    const float f = juce::jlimit (0.f, 1.f, (db + 60.f) / 60.f);
    g.setColour (db > -1.f ? palette::alarm : palette::neonCyan);
    g.fillRect (m.withTop (m.getBottom() - m.getHeight() * f));
    g.setColour (palette::gridLine);
    g.drawRect (m, 1.f);
}

void GuitarStripPanel::mouseUp (const juce::MouseEvent& e)
{
    if (shakalL.getBounds().contains (e.getPosition()) || presetLetter.getBounds().contains (e.getPosition()))
        c_.setTrackPanelTab (5);   // GTR
}

void GuitarStripPanel::resized()
{
    auto r = getLocalBounds().reduced (8, 6);
    constexpr int gap = 8;
    meter_ = r.removeFromLeft (24);
    r.removeFromLeft (gap);

    auto knob = [&] (Knob& k, juce::Label& l) {
        auto col = r.removeFromLeft (48);
        r.removeFromLeft (gap);
        l.setBounds (col.removeFromTop (12));
        k.setBounds (col.removeFromTop (66));
        return col;
    };
    knob (gain, gainL);
    knob (gate, gateL);
    knob (hpf, hpfL);
    for (auto* s : { &slot1, &slot2 })
    {
        s->setBounds (r.removeFromLeft (110).withSizeKeepingCentre (110, 32));
        r.removeFromLeft (gap);
    }
    {
        auto col = r.removeFromLeft (48);
        r.removeFromLeft (gap);
        shakalL.setBounds (col.removeFromTop (12));
        shakal.setBounds (col.removeFromTop (52));
        presetLetter.setBounds (col.removeFromTop (14));
    }
    loopView.setBounds (r.removeFromLeft (300).withSizeKeepingCentre (300, 80));
    r.removeFromLeft (gap);
    for (auto* b : { &rec, &dub, &undo })
    {
        b->setBounds (r.removeFromLeft (56).withSizeKeepingCentre (56, 32));
        r.removeFromLeft (4);
    }
    r.removeFromLeft (gap - 4);
    bars.setBounds (r.removeFromLeft (60).withSizeKeepingCentre (60, 24));
    r.removeFromLeft (gap);
    tap.setBounds (r.removeFromLeft (48).withSizeKeepingCentre (48, 28));
    r.removeFromLeft (gap);
    knob (loopGain, loopL);
    toTrack.setBounds (r.removeFromLeft (56).withSizeKeepingCentre (56, 32));
}

} // namespace jackal
