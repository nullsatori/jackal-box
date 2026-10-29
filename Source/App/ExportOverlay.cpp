#include "ExportOverlay.h"

#include "JackalLookAndFeel.h"
#include "Widgets.h"

#include "Model/SongOps.h"

namespace jackal
{

namespace
{
    juce::String clock (double seconds)
    {
        const int m = static_cast<int> (seconds / 60.0);
        const double s = seconds - m * 60.0;
        return juce::String (m).paddedLeft ('0', 2) + ":" + juce::String (s, 1).paddedLeft ('0', 4);
    }

    const int kRates[] { 0, 44100, 48000, 96000 };
    const int kBits[] { 16, 24, 32 };
} // namespace

ExportOverlay::ExportOverlay (ProjectController& controller, AppSettings& settings) : c_ (controller), settings_ (settings)
{
    title.setFont (monoFont (14.f, true));
    title.setColour (juce::Label::textColourId, palette::neonCyan);
    for (auto* l : { &title, &scopeLabel, &rateLabel, &bitsLabel, &tailLabel, &cyclesLabel, &folderLabel, &folderPath, &stemsNote,
                     &songInfo, &timeLine, &speedLine, &resultLine })
    {
        if (l != &title)
            l->setFont (monoFont (12.f));
        addAndMakeVisible (*l);
    }
    for (auto* l : { &scopeLabel, &rateLabel, &bitsLabel, &tailLabel, &cyclesLabel, &folderLabel, &stemsNote, &speedLine })
        l->setColour (juce::Label::textColourId, palette::dim);
    stemsNote.setText ("stems are pre-master (no master SHAKAL, glitch, inserts, gain)", juce::dontSendNotification);
    stemsNote.setFont (monoFont (11.f));
    timeLine.setColour (juce::Label::textColourId, palette::nixie);
    resultLine.setJustificationType (juce::Justification::topLeft);
    resultLine.setMinimumHorizontalScale (1.f);

    for (auto* b : { &patternButton, &songButton, &stems, &chooseFolder, &resetFolder, &exportButton, &openFolder, &closeButton })
    {
        styleButton (*b);
        addAndMakeVisible (*b);
    }
    patternButton.setColour (juce::TextButton::buttonOnColourId, palette::neonCyan);
    songButton.setColour (juce::TextButton::buttonOnColourId, palette::nixie);
    stems.setColour (juce::TextButton::buttonOnColourId, palette::neonCyan);
    exportButton.setColour (juce::TextButton::buttonOnColourId, palette::nixie);
    exportButton.setColour (juce::TextButton::textColourOffId, palette::nixie);

    patternButton.onClick = [this] { scope_ = RenderScope::Pattern; refresh(); };
    songButton.onClick = [this] { scope_ = RenderScope::Song; refresh(); };
    stems.onClick = [this] { write ([] (ExportSettings& e) { e.stems = ! e.stems; }); };
    closeButton.onClick = [this] { close(); };

    rate.addItem ("DEVICE", 1);
    rate.addItem ("44100", 2);
    rate.addItem ("48000", 3);
    rate.addItem ("96000", 4);
    rate.onChange = [this] {
        const int i = juce::jlimit (0, 3, rate.getSelectedItemIndex());
        write ([i] (ExportSettings& e) { e.sampleRate = kRates[i]; });
    };
    bits.addItem ("16", 1);
    bits.addItem ("24", 2);
    bits.addItem ("32f", 3);
    bits.onChange = [this] {
        const int i = juce::jlimit (0, 2, bits.getSelectedItemIndex());
        write ([i] (ExportSettings& e) { e.bitDepth = kBits[i]; });
    };
    addAndMakeVisible (rate);
    addAndMakeVisible (bits);

    tail.setRange (0.0, 8.0, 0.5);
    tail.setTextValueSuffix (" s");
    tail.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 20);
    tail.onValueChange = [this] { write ([v = tail.getValue()] (ExportSettings& e) { e.tailSeconds = static_cast<float> (v); }); };
    cycles.setRange (1.0, 16.0, 1.0);
    cycles.setTextBoxStyle (juce::Slider::TextBoxRight, false, 52, 20);
    cycles.onValueChange = [this] { write ([v = cycles.getValue()] (ExportSettings& e) { e.cycles = juce::roundToInt (v); }); };
    for (auto* s : { &tail, &cycles })
    {
        s->setMouseClickGrabsKeyboardFocus (false);
        s->setWantsKeyboardFocus (false);
        s->setColour (juce::Slider::trackColourId, palette::neonCyan.withAlpha (0.4f));
        addAndMakeVisible (*s);
    }

    chooseFolder.setTooltip ("choose the export folder");
    chooseFolder.onClick = [this] {
        chooser_ = std::make_unique<juce::FileChooser> ("Export folder", c_.exportFolder());
        chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                               [this] (const juce::FileChooser& fc) {
                                   const auto f = fc.getResult();
                                   if (f.isDirectory())
                                       settings_.setExportFolder (f.getFullPathName());
                                   refresh();
                               });
    };
    resetFolder.setTooltip ("export into the project folder (export/)");
    resetFolder.onClick = [this] { settings_.setExportFolder ({}); refresh(); };

    exportButton.onClick = [this] {
        if (c_.exportState().running)
        {
            c_.cancelExport();
            return;
        }
        if (const auto error = c_.startExport (scope_); error.isNotEmpty())
            c_.setStatus (error, true);
        refresh();
    };
    openFolder.onClick = [this] {
        const auto s = c_.exportState();
        if (s.result != nullptr && s.result->folder.isDirectory())
            s.result->folder.revealToUser();
        else
            c_.exportFolder().revealToUser();
    };

    setWantsKeyboardFocus (false);
}

void ExportOverlay::open()
{
    scope_ = songops::playable (c_.song()) ? RenderScope::Song : RenderScope::Pattern;
    refresh();
    setVisible (true);
    startTimerHz (15);
}

void ExportOverlay::close()
{
    stopTimer();   // closing does not cancel a running export
    setVisible (false);
    if (onClosed)
        onClosed();
}

void ExportOverlay::write (const std::function<void (ExportSettings&)>& fn)
{
    auto e = c_.exportSettings();
    fn (e);
    if (! (e == c_.exportSettings()))
        c_.setExportSettings (e);
    refresh();
}

juce::String ExportOverlay::songSummary() const
{
    const auto& song = c_.song();
    if (song.length == 0)
        return "song is empty";
    const double beats = songops::totalBeats (song, c_.project().patterns);
    const int bars = juce::roundToInt (beats / kBeatsPerBar);
    return "(" + juce::String (song.length) + " entries, " + juce::String (bars) + " bars, " + clock (beats * 60.0 / c_.project().bpm) + ")";
}

juce::String ExportOverlay::folderText() const
{
    const auto f = c_.exportFolder();
    if (settings_.exportFolder().isEmpty())
        return "export/";
    return f.getFullPathName();
}

void ExportOverlay::timerCallback()
{
    const auto s = c_.exportState();
    if (s.fraction != shownFraction_ || c_.version() != shownVersion_)
        refresh();
}

void ExportOverlay::refresh()
{
    const auto& e = c_.exportSettings();
    const auto s = c_.exportState();
    shownFraction_ = s.fraction;
    shownVersion_ = c_.version();

    const bool songOk = songops::playable (c_.song());
    if (! songOk && scope_ == RenderScope::Song)
        scope_ = RenderScope::Pattern;
    patternButton.setButtonText ("PATTERN " + juce::String::charToString (static_cast<juce::juce_wchar> ('A' + c_.editPatternIndex())));
    patternButton.setToggleState (scope_ == RenderScope::Pattern, juce::dontSendNotification);
    songButton.setToggleState (scope_ == RenderScope::Song, juce::dontSendNotification);
    songButton.setEnabled (songOk);
    songInfo.setText (songSummary(), juce::dontSendNotification);
    songInfo.setColour (juce::Label::textColourId, songOk ? palette::text : palette::dim);
    cycles.setEnabled (scope_ == RenderScope::Pattern);
    cyclesLabel.setEnabled (scope_ == RenderScope::Pattern);

    const auto info = juce::String (c_.sampleRate(), 0);
    rate.changeItemText (1, "DEVICE (" + info + ")");
    int ri = 0;
    for (int i = 0; i < 4; ++i)
        if (kRates[i] == e.sampleRate)
            ri = i;
    rate.setSelectedItemIndex (ri, juce::dontSendNotification);
    bits.setSelectedItemIndex (e.bitDepth == 16 ? 0 : e.bitDepth == 32 ? 2 : 1, juce::dontSendNotification);
    stems.setToggleState (e.stems, juce::dontSendNotification);
    if (! tail.isMouseButtonDown())
        tail.setValue (e.tailSeconds, juce::dontSendNotification);
    if (! cycles.isMouseButtonDown())
        cycles.setValue (e.cycles, juce::dontSendNotification);
    folderPath.setText (folderText(), juce::dontSendNotification);
    resetFolder.setEnabled (settings_.exportFolder().isNotEmpty());

    exportButton.setButtonText (s.running ? "CANCEL" : "EXPORT");
    exportButton.setEnabled (s.running || c_.exportAllowed());
    exportButton.setTooltip (c_.exportAllowed() || s.running ? juce::String() : juce::String ("not while a loop is armed or recording"));

    if (s.running)
    {
        timeLine.setText (clock (s.renderedSeconds) + " / " + clock (s.totalSeconds), juce::dontSendNotification);
        speedLine.setText (s.speed > 0.0 ? juce::String (juce::CharPointer_UTF8 ("\xc3\x97")) + juce::String (juce::roundToInt (s.speed)) + " realtime"
                                         : juce::String(),
                           juce::dontSendNotification);
    }
    else if (! s.hasResult)
    {
        timeLine.setText ({}, juce::dontSendNotification);
        speedLine.setText ({}, juce::dontSendNotification);
    }

    if (s.result != nullptr)
    {
        resultLine.setText (s.resultText, juce::dontSendNotification);
        resultLine.setColour (juce::Label::textColourId, s.resultAlarm ? palette::alarm : palette::text);
        openFolder.setEnabled (s.result->status == RenderStatus::Done);
    }
    else
    {
        resultLine.setText ({}, juce::dontSendNotification);
        openFolder.setEnabled (c_.exportFolder().isDirectory());
    }
    repaint();
}

void ExportOverlay::paint (juce::Graphics& g)
{
    g.fillAll (palette::panel);
    g.setColour (palette::neonCyan.withAlpha (0.6f));
    g.drawRect (getLocalBounds(), 1);

    const auto s = c_.exportState();
    const auto bar = progressArea_.toFloat();
    g.setColour (palette::ledOff);
    g.fillRect (bar);
    g.setColour (palette::nixie);
    const double f = s.running ? s.fraction : (s.result != nullptr && s.result->status == RenderStatus::Done ? 1.0 : 0.0);
    g.fillRect (bar.withWidth (bar.getWidth() * static_cast<float> (juce::jlimit (0.0, 1.0, f))));
    g.setColour (palette::gridLine);
    g.drawRect (bar, 1.f);
}

void ExportOverlay::resized()
{
    auto r = getLocalBounds().reduced (12);
    auto head = r.removeFromTop (24);
    closeButton.setBounds (head.removeFromRight (24));
    title.setBounds (head);
    r.removeFromTop (8);

    auto row = [&r] (int h = 26) {
        auto x = r.removeFromTop (h);
        r.removeFromTop (6);
        return x;
    };

    auto a = row();
    scopeLabel.setBounds (a.removeFromLeft (64));
    patternButton.setBounds (a.removeFromLeft (110));
    a.removeFromLeft (6);
    songButton.setBounds (a.removeFromLeft (64));
    a.removeFromLeft (8);
    songInfo.setBounds (a);

    auto c = row();
    cyclesLabel.setBounds (c.removeFromLeft (64));
    cycles.setBounds (c.removeFromLeft (240));
    r.removeFromTop (6);

    auto b = row();
    rateLabel.setBounds (b.removeFromLeft (64));
    rate.setBounds (b.removeFromLeft (150));
    b.removeFromLeft (16);
    bitsLabel.setBounds (b.removeFromLeft (44));
    bits.setBounds (b.removeFromLeft (80));

    auto t = row();
    tailLabel.setBounds (t.removeFromLeft (64));
    tail.setBounds (t.removeFromLeft (240));
    t.removeFromLeft (16);
    stems.setBounds (t.removeFromLeft (80));
    stemsNote.setBounds (row (18));
    r.removeFromTop (6);

    auto f = row();
    folderLabel.setBounds (f.removeFromLeft (64));
    resetFolder.setBounds (f.removeFromRight (72));
    f.removeFromRight (4);
    chooseFolder.setBounds (f.removeFromRight (32));
    f.removeFromRight (6);
    folderPath.setBounds (f);
    r.removeFromTop (12);

    auto e = row (34);
    exportButton.setBounds (e.removeFromLeft (120));
    e.removeFromLeft (12);
    progressArea_ = e.withSizeKeepingCentre (e.getWidth(), 10);
    auto tl = row (20);
    timeLine.setBounds (tl.removeFromLeft (220));
    speedLine.setBounds (tl);
    r.removeFromTop (8);

    resultLine.setBounds (row (60));
    openFolder.setBounds (row().removeFromLeft (120));
}

} // namespace jackal
