#include "PluginBrowserOverlay.h"

#include "InsertHost.h"

#include "../JackalLookAndFeel.h"
#include "../Widgets.h"

namespace jackal
{

PluginBrowserOverlay::PluginBrowserOverlay (ProjectController& c, AppSettings& settings) : c_ (c), settings_ (settings)
{
    title.setFont (monoFont (13.f, true));
    title.setColour (juce::Label::textColourId, palette::neonCyan);
    addAndMakeVisible (title);
    progress.setFont (monoFont (11.f));
    progress.setColour (juce::Label::textColourId, palette::dim);
    addAndMakeVisible (progress);

    search.setFont (monoFont (13.f));
    search.setTextToShowWhenEmpty ("search name or vendor", palette::dim);
    search.onTextChange = [this] { rebuild(); };
    search.onReturnKey = [this] { choose (list.getSelectedRow()); };
    search.onEscapeKey = [this] { close(); };
    addAndMakeVisible (search);

    list.setRowHeight (22);
    list.setColour (juce::ListBox::backgroundColourId, palette::background);
    addAndMakeVisible (list);

    for (auto* b : { &scan, &rescan, &addFolder, &closeButton })
    {
        styleButton (*b);
        addAndMakeVisible (*b);
    }
    scan.onClick = [this] { c_.scanner().start (false); updateProgress(); };
    rescan.onClick = [this] { c_.scanner().start (true); updateProgress(); };
    closeButton.onClick = [this] { close(); };
    addFolder.onClick = [this] {
        chooser_ = std::make_unique<juce::FileChooser> ("Add a VST3 folder", juce::File());
        chooser_->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
                               [this] (const juce::FileChooser& fc) {
                                   const auto f = fc.getResult();
                                   if (! f.isDirectory())
                                       return;
                                   auto paths = settings_.vst3Paths();
                                   paths.addIfNotAlreadyThere (f.getFullPathName());
                                   settings_.setVst3Paths (paths);
                                   c_.scanner().start (false);
                                   updateProgress();
                               });
    };

    c_.scanner().onProgress = [this] { updateProgress(); rebuild(); };
}

void PluginBrowserOverlay::openFor (int chain, int slot)
{
    chain_ = chain;
    slot_ = slot;
    const auto target = InsertHost::slotName (chain, slot).replace (juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")), " " + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + " SLOT ");
    title.setText ("LOAD INTO  " + target, juce::dontSendNotification);
    search.clear();
    rebuild();
    updateProgress();
    setVisible (true);
    toFront (true);
    search.grabKeyboardFocus();
}

void PluginBrowserOverlay::close()
{
    setVisible (false);
    if (onClosed)
        onClosed();
}

void PluginBrowserOverlay::rebuild()
{
    rows_.clear();
    const auto q = search.getText().trim();
    auto types = c_.plugins().list().getTypes();
    std::sort (types.begin(), types.end(), [] (const auto& a, const auto& b) { return a.name.compareIgnoreCase (b.name) < 0; });
    for (const auto& d : types)
        if (q.isEmpty() || d.name.containsIgnoreCase (q) || d.manufacturerName.containsIgnoreCase (q))
            rows_.add ({ d, false, {} });
    for (const auto& path : c_.plugins().list().getBlacklistedFiles())
        if (q.isEmpty() || path.containsIgnoreCase (q))
        {
            Row r;
            r.crashed = true;
            r.crashedPath = path;
            rows_.add (r);
        }
    list.updateContent();
    if (list.getSelectedRow() < 0 && ! rows_.isEmpty())
        list.selectRow (0);
    list.repaint();
}

void PluginBrowserOverlay::updateProgress()
{
    auto& s = c_.scanner();
    if (s.running())
        progress.setText (juce::String (s.done()) + "/" + juce::String (s.total()) + "  " + s.currentFile(), juce::dontSendNotification);
    else
        progress.setText (juce::String (c_.plugins().list().getNumTypes()) + " plugins known", juce::dontSendNotification);
}

void PluginBrowserOverlay::paintListBoxItem (int row, juce::Graphics& g, int w, int h, bool selected)
{
    if (row < 0 || row >= rows_.size())
        return;
    const auto& r = rows_.getReference (row);
    const bool usable = ! r.crashed && ! r.desc.isInstrument;
    if (selected && usable)
    {
        g.setColour (palette::neonCyan.withAlpha (0.25f));
        g.fillRect (0, 0, w, h);
    }
    g.setFont (monoFont (12.f, ! r.crashed));
    if (r.crashed)
    {
        g.setColour (palette::dim);
        g.drawText ("CRASHED  " + juce::File (r.crashedPath).getFileName(), 6, 0, w - 12, h, juce::Justification::centredLeft, true);
        return;
    }
    g.setColour (usable ? palette::text : palette::dim);
    g.drawText ((r.desc.isInstrument ? "INST  " : "") + r.desc.name, 6, 0, w / 2, h, juce::Justification::centredLeft, true);
    g.setFont (monoFont (11.f));
    g.setColour (palette::dim);
    g.drawText (r.desc.manufacturerName + "  " + r.desc.version, w / 2, 0, w / 2 - 6, h, juce::Justification::centredRight, true);
}

void PluginBrowserOverlay::choose (int row)
{
    if (row < 0 || row >= rows_.size())
        return;
    const auto& r = rows_.getReference (row);
    if (r.crashed || r.desc.isInstrument)
        return;
    if (c_.loadPlugin (chain_, slot_, r.desc).isEmpty())
        close();
}

bool PluginBrowserOverlay::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey) { close(); return true; }
    if (key == juce::KeyPress::returnKey) { choose (list.getSelectedRow()); return true; }
    if (key == juce::KeyPress::downKey)   { list.selectRow (juce::jmin (rows_.size() - 1, list.getSelectedRow() + 1)); return true; }
    if (key == juce::KeyPress::upKey)     { list.selectRow (juce::jmax (0, list.getSelectedRow() - 1)); return true; }
    return false;
}

void PluginBrowserOverlay::paint (juce::Graphics& g)
{
    g.fillAll (palette::panel);
    g.setColour (palette::neonCyan.withAlpha (0.6f));
    g.drawRect (getLocalBounds(), 1);
}

void PluginBrowserOverlay::resized()
{
    auto r = getLocalBounds().reduced (8);
    auto head = r.removeFromTop (24);
    closeButton.setBounds (head.removeFromRight (24));
    title.setBounds (head);
    r.removeFromTop (4);
    search.setBounds (r.removeFromTop (26));
    r.removeFromTop (6);
    auto buttons = r.removeFromBottom (26);
    scan.setBounds (buttons.removeFromLeft (70));
    buttons.removeFromLeft (4);
    rescan.setBounds (buttons.removeFromLeft (80));
    buttons.removeFromLeft (4);
    addFolder.setBounds (buttons.removeFromLeft (120));
    buttons.removeFromLeft (8);
    progress.setBounds (buttons);
    r.removeFromBottom (6);
    list.setBounds (r);
}

} // namespace jackal
