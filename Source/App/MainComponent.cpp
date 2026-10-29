#include "MainComponent.h"

#include "JackalLookAndFeel.h"
#include "KeyMap.h"
#include "ProjectFiles.h"

#include "Model/PatternOps.h"
#include "Model/SongOps.h"

namespace jackal
{

using namespace keymap;

MainComponent::MainComponent (AudioEngine& e, AppSettings& s, ProjectController& c)
    : engine (e),
      settings (s),
      controller (c),
      midiMapper (e, c, s),
      transportBar (c, e.core(), s),
      patternStrip (c),
      songStrip (c),
      trackHeaders (c),
      stepGrid (c),
      trackPanel (c),
      guitarStrip (c, e, s),
      padBar (c),
      statusBar (c, e),
      settingsOverlay (e, s, midiMapper),
      browser (c, s),
      exportOverlay (c, s)
{
    for (auto* comp : std::initializer_list<juce::Component*> { &transportBar, &patternStrip, &songStrip, &trackHeaders, &stepGrid,
                                                                 &trackPanel, &guitarStrip, &padBar, &statusBar })
        addAndMakeVisible (comp);
    addChildComponent (settingsOverlay);
    addChildComponent (browser);
    addChildComponent (exportOverlay);
    exportOverlay.onClosed = [this] { grabKeyboardFocus(); };

    guitarStrip.onBrowse = [this] (int ch, int sl) { openBrowser (ch, sl); };
    trackPanel.vst.onBrowse = [this] (int ch, int sl) { openBrowser (ch, sl); };
    browser.onClosed = [this] { grabKeyboardFocus(); };
    engine.updateLoopComp (settings);

    statusBar.onSettings = [this] { toggleSettings(); };
    statusBar.onExport = [this] { toggleExport(); };
    statusBar.midiLed = [this] { return midiMapper.ledOn(); };

    commands.registerAllCommandsForTarget (this);
    commands.setFirstCommandTarget (this);

    engine.deviceManager().addChangeListener (this);

    setWantsKeyboardFocus (true);
    setSize (1280, 900);

    midiMapper.attach (*this);   // the learn overlay goes on top of everything added above
    midiMapper.start();
}

MainComponent::~MainComponent()
{
    engine.deviceManager().removeChangeListener (this);
}

void MainComponent::changeListenerCallback (juce::ChangeBroadcaster*)
{
    const auto state = engine.deviceManager().createStateXml();
    settings.setAudioDeviceState (state.get());
    settingsOverlay.refreshChannels();
    engine.updateLoopComp (settings);
    settingsOverlay.refreshTiming();
}

void MainComponent::toggleSettings()
{
    if (! settingsOverlay.isVisible())
    {
        settingsOverlay.refreshMidiDevices();
        settingsOverlay.refreshTiming();
        browser.setVisible (false);   // one overlay at a time
        if (exportOverlay.isVisible())
            exportOverlay.close();
    }
    settingsOverlay.setVisible (! settingsOverlay.isVisible());
    grabKeyboardFocus();
}

void MainComponent::toggleExport()
{
    if (exportOverlay.isVisible())
    {
        exportOverlay.close();
        return;
    }
    settingsOverlay.setVisible (false);
    browser.setVisible (false);
    exportOverlay.open();
    grabKeyboardFocus();
}

void MainComponent::openBrowser (int chain, int slot)
{
    settingsOverlay.setVisible (false);
    if (exportOverlay.isVisible())
        exportOverlay.close();
    controller.setLastInsert (chain, slot);
    browser.openFor (chain, slot);
}

void MainComponent::browseFirstFree (int chain)
{
    const int slot = controller.insertSpec (chain, 0).empty() ? 0 : controller.insertSpec (chain, 1).empty() ? 1 : 0;
    openBrowser (chain, slot);
}

// ---------------------------------------------------------------------------- commands

void MainComponent::getAllCommands (juce::Array<juce::CommandID>& ids)
{
    ids.addArray ({ togglePlay, toggleRec });
    for (int i = 0; i < 16; ++i) ids.add (stepBase + i);
    for (int i = 0; i < 8; ++i) ids.addArray ({ hitBase + i, hitAccentBase + i, patternBase + i, appendBase + i });
    ids.addArray ({ rowUp, rowDown, shiftLeft, shiftRight, nudgeLeft, nudgeRight, pageUp, pageDown, doubleLen, mutateCmd,
                    undoCmd, redoCmd, redoAlt, saveCmd, saveAsCmd, openCmd, newCmd, closeOverlay, deletePLocks, nextTab,
                    loopEnter, loopRec, loopUndo, browseTrack, browseGuitar, toggleEditorCmd,
                    songModeCmd, followCmd, exportCmd, learnCmd, captureCmd, sampleUp, sampleDown });
}

void MainComponent::getCommandInfo (juce::CommandID id, juce::ApplicationCommandInfo& info)
{
    using KP = juce::KeyPress;
    const auto none = juce::ModifierKeys::noModifiers;
    const auto ctrl = juce::ModifierKeys::commandModifier;
    const auto shift = juce::ModifierKeys::shiftModifier;
    const auto alt = juce::ModifierKeys::altModifier;

    auto set = [&info] (const char* name, int key, juce::ModifierKeys mods) {
        info.setInfo (name, name, "Jackal", 0);
        info.addDefaultKeypress (key, mods);
    };

    if (id >= stepBase && id < stepBase + 16)
        return set ("Toggle step", kStepKeys[static_cast<std::size_t> (id - stepBase)], none);
    if (id >= hitBase && id < hitBase + 8)
        return set ("Hit track", kHitKeys[static_cast<std::size_t> (id - hitBase)], none);
    if (id >= hitAccentBase && id < hitAccentBase + 8)
        return set ("Hit track accent", kHitKeys[static_cast<std::size_t> (id - hitAccentBase)], shift);
    if (id >= patternBase && id < patternBase + 8)
        return set ("Queue pattern", KP::F1Key + (id - patternBase), none);
    if (id >= appendBase && id < appendBase + 8)
        return set ("Append pattern to the song", KP::F1Key + (id - appendBase), shift);

    switch (id)
    {
        case togglePlay:   return set ("Play / Stop", KP::spaceKey, none);
        case toggleRec:    return set ("Record", 'R', ctrl);
        case rowUp:        return set ("Row up", KP::upKey, none);
        case rowDown:      return set ("Row down", KP::downKey, none);
        case shiftLeft:    return set ("Shift row left", KP::leftKey, none);
        case shiftRight:   return set ("Shift row right", KP::rightKey, none);
        case nudgeLeft:    return set ("Nudge row left", KP::leftKey, alt);
        case nudgeRight:   return set ("Nudge row right", KP::rightKey, alt);
        case pageUp:       return set ("Previous page", KP::pageUpKey, none);
        case pageDown:     return set ("Next page", KP::pageDownKey, none);
        case doubleLen:    return set ("Double pattern", 'D', ctrl);
        case mutateCmd:    return set ("Mutation", 'M', ctrl);
        case undoCmd:      return set ("Undo", 'Z', ctrl);
        case redoCmd:      return set ("Redo", 'Y', ctrl);
        case redoAlt:      return set ("Redo", 'Z', ctrl | shift);
        case saveCmd:      return set ("Save", 'S', ctrl);
        case saveAsCmd:    return set ("Save as", 'S', ctrl | shift);
        case openCmd:      return set ("Open", 'O', ctrl);
        case newCmd:       return set ("New", 'N', ctrl);
        case closeOverlay: return set ("Close overlay", KP::escapeKey, none);
        case deletePLocks: return set ("Delete p-locks", KP::deleteKey, none);
        case nextTab:      return set ("Next panel tab", KP::tabKey, none);
        case loopEnter:    return set ("Loop REC / DUB / cancel", KP::returnKey, none);
        case loopRec:      return set ("Loop REC (replace)", KP::returnKey, shift);
        case loopUndo:     return set ("Loop UNDO", KP::backspaceKey, none);
        case browseTrack:  return set ("Plugin for the track", 'P', ctrl);
        case browseGuitar: return set ("Plugin for the guitar", 'P', ctrl | shift);
        case toggleEditorCmd: return set ("Plugin editor", 'E', ctrl);
        case songModeCmd:  return set ("PTN / SONG", 'P', none);
        case followCmd:    return set ("FOLLOW", 'O', none);
        case exportCmd:    return set ("Export", 'E', ctrl | shift);
        case learnCmd:     return set ("MIDI learn", 'L', ctrl);
        case captureCmd:   return set ("Capture the last 2 bars", 'R', ctrl | shift);
        case sampleUp:     return set ("Previous sample in the folder", KP::upKey, ctrl);
        case sampleDown:   return set ("Next sample in the folder", KP::downKey, ctrl);
        default: break;
    }
}

bool MainComponent::perform (const InvocationInfo& info)
{
    const int id = info.commandID;
    const int track = controller.selectedTrack();

    if (id >= stepBase && id < stepBase + 16)
    {
        const int slot = controller.gridPage() * 16 + (id - stepBase);
        if (slot < controller.editPattern().lanes[static_cast<std::size_t> (track)].length)
        {
            controller.beginGesture ("Step key");
            controller.editPattern ([=] (Pattern& p) { ops::toggleStep (p, track, slot); });
            controller.setLastTouched ({ track, slot });
        }
        return true;
    }
    if (id >= hitBase && id < hitBase + 8)          { controller.triggerTrack (id - hitBase, 100); return true; }
    if (id >= hitAccentBase && id < hitAccentBase + 8) { controller.triggerTrack (id - hitAccentBase, 127); return true; }
    if (id >= patternBase && id < patternBase + 8)  { controller.patternClicked (id - patternBase); return true; }
    if (id >= appendBase && id < appendBase + 8)    { controller.appendToSong (id - appendBase); return true; }

    switch (id)
    {
        case togglePlay: controller.togglePlay(); return true;
        case toggleRec:  controller.setRecording (! controller.recording()); return true;
        case rowUp:      controller.selectTrack (track - 1); return true;
        case rowDown:    controller.selectTrack (track + 1); return true;
        case shiftLeft:
        case shiftRight:
            controller.beginGesture ("Shift row");
            controller.editPattern ([=] (Pattern& p) { ops::shiftLane (p, track, id == shiftRight ? 1 : -1); });
            return true;
        case nudgeLeft:
        case nudgeRight:
            controller.beginGesture ("Nudge row");
            controller.editPattern ([=] (Pattern& p) { ops::nudgeLane (p, track, id == nudgeRight ? 5 : -5); });
            return true;
        case pageUp:     controller.setGridPage (controller.gridPage() - 1); return true;
        case pageDown:   controller.setGridPage (controller.gridPage() + 1); return true;
        case doubleLen:  controller.doublePattern(); return true;
        case mutateCmd:  controller.mutate(); return true;
        case undoCmd:    controller.undo(); return true;
        case redoCmd:
        case redoAlt:    controller.redo(); return true;
        case saveCmd:    controller.save(); return true;
        case saveAsCmd:  saveAsDialog(); return true;
        case openCmd:    openDialog(); return true;
        case newCmd:     controller.newProject(); return true;
        case closeOverlay:
            if (midiMapper.learning())   // stage 5: learn first, then the plugin window, then the overlay
            {
                midiMapper.cancelLearn();
                return true;
            }
            if (controller.inserts().closeTopEditor())
                return true;
            if (settingsOverlay.calibrating())
                settingsOverlay.cancelCalibration();
            else if (browser.isVisible())
                browser.close();
            else if (exportOverlay.isVisible())
                exportOverlay.close();
            else if (settingsOverlay.isVisible())
                toggleSettings();
            return true;
        case songModeCmd: controller.setSongMode (! controller.songMode()); return true;
        case followCmd:   controller.setFollow (! controller.follow()); return true;
        case exportCmd:   toggleExport(); return true;
        case learnCmd:    midiMapper.learning() ? midiMapper.cancelLearn() : midiMapper.beginLearn(); return true;
        case captureCmd:  controller.capture (2); return true;
        case sampleUp:
        case sampleDown:  controller.stepSampleFile (track, id == sampleDown ? 1 : -1); return true;
        case loopEnter:  controller.loops().enter(); return true;
        case loopRec:    controller.loops().busy() ? controller.loops().cancel() : controller.loops().rec (controller.loops().bars()); return true;
        case loopUndo:   controller.loops().undo(); return true;
        case browseTrack:  browseFirstFree (track); return true;
        case browseGuitar: browseFirstFree (kGuitarChain); return true;
        case toggleEditorCmd:
        {
            const auto [ch, sl] = controller.lastInsert();
            if (ch >= 0)
                controller.toggleEditor (ch, sl);
            return true;
        }
        case nextTab:
            controller.setTrackPanelTab (controller.trackPanelTab() + 1);
            return true;
        case deletePLocks:
            if (auto entry = songStrip.hoverEntry())   // stage 5: over a song cell it removes the entry
            {
                const int at = *entry;
                controller.editSong ("Song remove", [at] (SongData& song) { songops::remove (song, at); });
                return true;
            }
            if (auto s = stepGrid.hoverStep())
            {
                const auto st = *s;
                controller.beginGesture ("Delete p-locks");
                controller.editPattern ([st] (Pattern& p) { ops::clearPLocks (p, st.track, st.slot); });
            }
            return true;
        default: break;
    }
    return false;
}

bool MainComponent::keyPressed (const juce::KeyPress& key)
{
    // pattern copy / paste (not in the command table: C and V are also hit keys without Ctrl)
    if (key == juce::KeyPress ('C', juce::ModifierKeys::commandModifier, 0)) { patternStrip.copySelected(); return true; }
    if (key == juce::KeyPress ('V', juce::ModifierKeys::commandModifier, 0)) { patternStrip.pasteSelected(); return true; }

    // held keys are handled in keyStateChanged; swallow them here so nothing else reacts
    const auto code = key.getKeyCode();
    if (code == kFillKey || (code >= '1' && code <= '8'))
        return true;
    return false;
}

bool MainComponent::keyStateChanged (bool)
{
    bool handled = false;

    const bool fill = juce::KeyPress::isKeyCurrentlyDown (kFillKey);
    if (fill != fillKeyDown)
    {
        fillKeyDown = fill;
        controller.setFill (fill);
        handled = true;
    }

    for (int p = 0; p < kNumPads; ++p)
    {
        const bool down = juce::KeyPress::isKeyCurrentlyDown (kPadKeys[static_cast<std::size_t> (p)]);
        auto& was = padKeyDown[static_cast<std::size_t> (p)];
        if (down != was)
        {
            was = down;
            if (down)
                controller.padDown (p);
            else
                controller.padUp (p);
            handled = true;
        }
    }
    return handled;
}

// ---------------------------------------------------------------------------- files

void MainComponent::openDialog()
{
    chooser = std::make_unique<juce::FileChooser> ("Open a Jackal project folder", files::projectsRoot(), "*.jackal");
    chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                          [this] (const juce::FileChooser& fc) {
                              const auto f = fc.getResult();
                              if (f == juce::File())
                                  return;
                              const auto folder = files::looksLikeProject (f) ? f : f.getParentDirectory();
                              if (! controller.openProject (folder))
                                  controller.setStatus (controller.lastError());
                          });
}

void MainComponent::saveAsDialog()
{
    chooser = std::make_unique<juce::FileChooser> ("Save the project as", files::projectsRoot().getChildFile ("MyTrack.jackal"), "*.jackal");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles,
                          [this] (const juce::FileChooser& fc) {
                              const auto f = fc.getResult();
                              if (f != juce::File())
                                  controller.saveAs (f);
                          });
}

bool MainComponent::isInterestedInFileDrag (const juce::StringArray& paths)
{
    for (const auto& p : paths)
        if (files::isAudioFile (juce::File (p)))
            return true;
    return false;
}

void MainComponent::filesDropped (const juce::StringArray& paths, int x, int y)
{
    juce::File file;
    for (const auto& p : paths)
        if (files::isAudioFile (juce::File (p)))
        {
            file = juce::File (p);
            break;
        }
    if (file == juce::File())
    {
        controller.setStatus ("drop a .wav or .aiff file");
        return;
    }

    const juce::Point<int> pt (x, y);
    int track = trackHeaders.rowAt (trackHeaders.getLocalPoint (this, pt));
    if (track < 0)
        track = stepGrid.rowAt (stepGrid.getLocalPoint (this, pt));
    if (track < 0)
        track = controller.selectedTrack();

    controller.selectTrack (track);
    controller.importFile (track, file);
}

// ---------------------------------------------------------------------------- layout

void MainComponent::paint (juce::Graphics& g)
{
    g.fillAll (palette::background);
}

void MainComponent::resized()
{
    auto r = getLocalBounds();
    transportBar.setBounds (r.removeFromTop (64));
    patternStrip.setBounds (r.removeFromTop (40));
    songStrip.setBounds (r.removeFromTop (36));
    statusBar.setBounds (r.removeFromBottom (28));
    padBar.setBounds (r.removeFromBottom (72));
    const auto overlayArea = r;
    guitarStrip.setBounds (r.removeFromBottom (96));

    trackHeaders.setBounds (r.removeFromLeft (180));
    trackPanel.setBounds (r.removeFromRight (300));
    stepGrid.setBounds (r.reduced (6, 0));

    auto over = overlayArea;
    settingsOverlay.setBounds (over.removeFromRight (520));
    browser.setBounds (settingsOverlay.getBounds());
    exportOverlay.setBounds (settingsOverlay.getBounds());
    midiMapper.layout();
}

} // namespace jackal
