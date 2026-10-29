#include "ShakalSection.h"

#include "Model/ParamSpec.h"

namespace jackal
{

namespace
{
    juce::String formatFx (FxParam p, double v)
    {
        switch (p)
        {
            case FxParam::DriveDb:      return juce::String (v, 1) + "dB";
            case FxParam::Sp12FilterHz: return v >= 19999.0 ? juce::String ("off") : juce::String (v / 1000.0, 1) + "k";
            case FxParam::DecimRateHz:  return juce::String (v / 1000.0, v < 10000.0 ? 2 : 1) + "k";
            case FxParam::CrushBits:    return juce::String (v, 1);
            case FxParam::FlipProb:
            case FxParam::GlitchProb:   return juce::String (juce::roundToInt (v * 100.0)) + "%";
            case FxParam::FlipBit:      return juce::String (juce::roundToInt (v));
            case FxParam::GlitchLenMs:  return juce::String (juce::roundToInt (v)) + "ms";
            default:                    return juce::String (v, 2);
        }
    }
} // namespace

void ShakalSection::MacroKnob::mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails& w)
{
    if (ctl != nullptr)
        ctl->beginGesture ("SHAKAL");
    const double d = (w.deltaY != 0.f ? w.deltaY : w.deltaX) > 0.f ? 0.02 : -0.02;
    setValue (juce::jlimit (0.0, 1.0, getValue() + d), juce::sendNotificationSync);
}

ShakalSection::ShakalSection (ProjectController& controller, int fixedChain)
    : c_ (controller), fixed_ (fixedChain), macro (controller, "SHAKAL")
{
    title.setText (fixedChain == kMasterChain ? "MASTER SHAKAL" : fixedChain == kGuitarChain ? "GUITAR SHAKAL" : "SHAKAL", juce::dontSendNotification);
    title.setFont (monoFont (13.f, true));
    title.setColour (juce::Label::textColourId, palette::neonCyan);
    addAndMakeVisible (title);

    for (int i = 0; i < 3; ++i)
    {
        auto& b = presets[static_cast<std::size_t> (i)];
        styleButton (b);
        b.setColour (juce::TextButton::buttonOnColourId, palette::neonCyan);
        b.onClick = [this, i] {
            c_.beginGesture ("Preset");
            c_.setFx (chain(), FxParam::Preset, static_cast<float> (i));
        };
        addAndMakeVisible (b);
    }

    styleButton (bypass);
    bypass.setColour (juce::TextButton::buttonOnColourId, palette::rec);
    bypass.onClick = [this] {
        c_.beginGesture ("Bypass");
        c_.setFx (chain(), FxParam::Bypass, c_.fxSettings (chain()).bypass ? 0.f : 1.f);
    };
    addAndMakeVisible (bypass);

    styleButton (reroll);
    reroll.setTooltip ("REROLL: new project seed (glitch, bit flips, step probability)");
    reroll.onClick = [this] { c_.reroll(); };
    addAndMakeVisible (reroll);

    macro.ctl = &c_;
    macro.setRange (0.0, 1.0, 0.0);
    macro.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    macro.setDoubleClickReturnValue (true, 0.0);
    macro.onValueChange = [this] {
        if (! refreshing_)
            c_.setShakal (chain(), static_cast<float> (macro.getValue()));
    };
    addAndMakeVisible (macro);

    macroValue.setFont (monoFont (13.f, true));
    macroValue.setJustificationType (juce::Justification::centred);
    macroValue.setColour (juce::Label::textColourId, palette::text);
    addAndMakeVisible (macroValue);

    auto make = [] (FxSlot slot, const char* name, FxParam on, std::vector<FxParam> knobs,
                    FxParam sw = FxParam::Count, const char* off = "", const char* onText = "") {
        auto r = std::make_unique<Row>();
        r->slot = slot;
        r->title = name;
        r->onParam = on;
        r->knobParams = std::move (knobs);
        r->switchParam = sw;
        r->switchOff = off;
        r->switchOn = onText;
        return r;
    };
    rows[0] = make (FxSlot::Drive,  "DRIVE",  FxParam::DriveOn,  { FxParam::DriveDb }, FxParam::DriveMode, "CLIP", "WRAP");
    rows[1] = make (FxSlot::Sp12,   "SP12",   FxParam::Sp12On,   { FxParam::Sp12FilterHz });
    rows[2] = make (FxSlot::Decim,  "DECIM",  FxParam::DecimOn,  { FxParam::DecimRateHz });
    rows[3] = make (FxSlot::Crush,  "CRUSH",  FxParam::CrushOn,  { FxParam::CrushBits }, FxParam::CrushMulaw, "LIN", juce::CharPointer_UTF8 ("\xce\xbc"));
    rows[4] = make (FxSlot::Flip,   "FLIP",   FxParam::FlipOn,   { FxParam::FlipProb, FxParam::FlipBit }, FxParam::FlipMode, "SMP", "BRST");
    rows[5] = make (FxSlot::Glitch, "GLITCH", FxParam::GlitchOn, { FxParam::GlitchProb, FxParam::GlitchLenMs });
    for (auto& r : rows)
        setupRow (*r);

    startTimerHz (30);
    refresh();
}

void ShakalSection::setupRow (Row& row)
{
    styleButton (row.on);
    row.on.setColour (juce::TextButton::buttonOnColourId, palette::neonCyan);
    row.on.onClick = [this, &row] {
        c_.beginGesture (row.title + " on");
        const bool now = c_.fxSettings (chain()).params[static_cast<std::size_t> (row.onParam)] >= 0.5f;
        c_.setFx (chain(), row.onParam, now ? 0.f : 1.f);
    };
    addAndMakeVisible (row.on);

    row.name.setText (row.title, juce::dontSendNotification);
    row.name.setFont (monoFont (12.f, true));
    row.name.setColour (juce::Label::textColourId, palette::text);
    addAndMakeVisible (row.name);

    for (auto p : row.knobParams)
    {
        const auto& spec = fxParamSpec (p);
        auto k = std::make_unique<Knob> (c_, row.title + " " + juce::String (std::string (spec.label)));
        k->textFromValueFunction = [p] (double v) { return formatFx (p, v); };
        k->setRange (spec.minValue, spec.maxValue, p == FxParam::FlipBit ? 1.0 : 0.0);
        if (spec.scale == ParamScale::Log)
            k->setSkewFactorFromMidPoint (std::sqrt (static_cast<double> (spec.minValue) * spec.maxValue));
        if (p == FxParam::FlipProb || p == FxParam::GlitchProb)
            k->setSkewFactor (0.3);
        k->setDoubleClickReturnValue (true, spec.defaultValue);
        k->setTextBoxStyle (juce::Slider::TextBoxRight, false, 44, 14);
        k->updateText();
        k->onValueChange = [this, p, raw = k.get()] {
            if (! refreshing_)
                c_.setFx (chain(), p, static_cast<float> (raw->getValue()));
        };
        addAndMakeVisible (*k);
        row.knobs.push_back (std::move (k));

        auto l = std::make_unique<juce::Label>();
        l->setText (juce::String (std::string (spec.label)), juce::dontSendNotification);
        l->setFont (monoFont (10.f));
        l->setColour (juce::Label::textColourId, palette::dim);
        addAndMakeVisible (*l);
        row.knobLabels.push_back (std::move (l));
    }

    if (row.switchParam != FxParam::Count)
    {
        styleButton (row.sw);
        row.sw.setColour (juce::TextButton::buttonOnColourId, palette::neonCyan);
        row.sw.onClick = [this, &row] {
            c_.beginGesture (row.title + " mode");
            const bool now = c_.fxSettings (chain()).params[static_cast<std::size_t> (row.switchParam)] >= 0.5f;
            c_.setFx (chain(), row.switchParam, now ? 0.f : 1.f);
        };
        addAndMakeVisible (row.sw);
    }
}

int ShakalSection::chain() const
{
    return fixed() ? fixed_ : c_.selectedTrack();
}

void ShakalSection::timerCallback()
{
    if (c_.version() != shownVersion_)
    {
        shownVersion_ = c_.version();
        refresh();
    }

    if (! isShowing())
        return;

    const auto snap = c_.shakalSnapshot();
    const auto ci = static_cast<std::size_t> (chain());
    const float actual = snap.macro[ci];
    const bool ring = (snap.overrideMask >> chain()) & 1u;
    auto& props = macro.getProperties();
    if (static_cast<float> (props[JackalLookAndFeel::actualProperty]) != actual
        || static_cast<bool> (props[JackalLookAndFeel::ringProperty]) != ring)
    {
        props.set (JackalLookAndFeel::actualProperty, actual);
        props.set (JackalLookAndFeel::ringProperty, ring);
        macro.repaint();
    }
    if (snap.engaged[ci] != shownEngaged_)
    {
        shownEngaged_ = snap.engaged[ci];
        repaint();
    }
}

void ShakalSection::refresh()
{
    const juce::ScopedValueSetter<bool> guard (refreshing_, true);
    const int ch = chain();
    const auto& fx = c_.fxSettings (ch);

    for (int i = 0; i < 3; ++i)
        presets[static_cast<std::size_t> (i)].setToggleState (fx.preset == i, juce::dontSendNotification);
    bypass.setToggleState (fx.bypass, juce::dontSendNotification);

    // a held step shows (and edits) its SHAKAL p-lock
    float value = c_.shakalValue (ch);
    bool held = false;
    if (! fixed())
        if (const auto hold = c_.plockTarget(); hold && hold->track == ch)
        {
            held = true;
            const auto& st = c_.editPattern().lanes[static_cast<std::size_t> (hold->track)].steps[static_cast<std::size_t> (hold->slot)];
            for (const auto& pl : st.plocks)
                if (pl.param == static_cast<uint8_t> (TrackParam::Shakal))
                    value = pl.value;
        }
    macro.setLocked (held);
    if (! macro.isMouseButtonDown())
        macro.setValue (value, juce::dontSendNotification);
    macroValue.setText (juce::String (juce::roundToInt (value * 100.f)), juce::dontSendNotification);

    for (auto& r : rows)
    {
        r->on.setToggleState (fx.params[static_cast<std::size_t> (r->onParam)] >= 0.5f, juce::dontSendNotification);
        for (std::size_t k = 0; k < r->knobs.size(); ++k)
            if (! r->knobs[k]->isMouseButtonDown())
                r->knobs[k]->setValue (fx.params[static_cast<std::size_t> (r->knobParams[k])], juce::dontSendNotification);
        if (r->switchParam != FxParam::Count)
        {
            const bool s = fx.params[static_cast<std::size_t> (r->switchParam)] >= 0.5f;
            r->sw.setButtonText (s ? r->switchOn : r->switchOff);
            r->sw.setToggleState (s, juce::dontSendNotification);
        }
    }
    repaint();
}

void ShakalSection::paint (juce::Graphics& g)
{
    g.fillAll (palette::panel);
    const auto& fx = c_.fxSettings (chain());
    for (auto& r : rows)
    {
        g.setColour (palette::gridLine);
        g.drawHorizontalLine (r->on.getY() - 6, 0.f, static_cast<float> (getWidth()));

        const bool manual = fx.params[static_cast<std::size_t> (r->onParam)] >= 0.5f;
        const bool engaged = (shownEngaged_ != 0xFF) && ((shownEngaged_ >> static_cast<int> (r->slot)) & 1u);
        const auto led = r->led.toFloat();
        g.setColour (engaged ? (manual ? palette::neonCyan : palette::nixie) : palette::ledOff);
        g.fillEllipse (led);
        g.setColour (palette::gridLine);
        g.drawEllipse (led, 1.f);
    }
}

void ShakalSection::resized()
{
    auto r = getLocalBounds().reduced (6, 4);
    auto head = r.removeFromTop (24);
    title.setBounds (head.removeFromLeft (fixed() ? 104 : 64));
    for (auto& b : presets)
    {
        b.setBounds (head.removeFromLeft (40).reduced (1));
    }
    head.removeFromLeft (4);
    reroll.setBounds (head.removeFromRight (30).reduced (1));
    bypass.setBounds (head.removeFromRight (38).reduced (1));
    r.removeFromTop (4);

    auto m = r.removeFromTop (72);
    macro.setBounds (m.withSizeKeepingCentre (72, 72));
    macroValue.setBounds (r.removeFromTop (16));
    r.removeFromTop (8);

    const int rowH = juce::jmin (60, r.getHeight() / 6);
    for (auto& row : rows)
    {
        auto line = r.removeFromTop (rowH).reduced (0, 6);
        auto left = line.removeFromLeft (92);
        auto top = left.removeFromTop (20);
        row->on.setBounds (top.removeFromLeft (30));
        top.removeFromLeft (4);
        row->led = top.removeFromLeft (10).withSizeKeepingCentre (8, 8);
        top.removeFromLeft (4);
        row->name.setBounds (top);
        if (row->switchParam != FxParam::Count)
            row->sw.setBounds (left.removeFromTop (20).withWidth (52));

        const int n = static_cast<int> (row->knobs.size());
        const int w = n > 0 ? line.getWidth() / n : 0;
        for (int k = 0; k < n; ++k)
        {
            auto cell = line.removeFromLeft (w);
            row->knobLabels[static_cast<std::size_t> (k)]->setBounds (cell.removeFromTop (12));
            row->knobs[static_cast<std::size_t> (k)]->setBounds (cell.removeFromLeft (36 + 46));
        }
    }
}

} // namespace jackal
