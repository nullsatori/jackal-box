#pragma once

#include "../ProjectController.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace jackal
{

// Plugin list for one target slot: search, SCAN / RESCAN / ADD FOLDER, Enter or double click loads.
class PluginBrowserOverlay : public juce::Component, private juce::ListBoxModel
{
public:
    PluginBrowserOverlay (ProjectController& c, AppSettings& settings);

    void openFor (int chain, int slot);
    void close();
    std::function<void()> onClosed;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

private:
    struct Row
    {
        juce::PluginDescription desc;
        bool crashed = false;
        juce::String crashedPath;
    };

    int getNumRows() override { return rows_.size(); }
    void paintListBoxItem (int row, juce::Graphics&, int w, int h, bool selected) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override { choose (row); }
    void returnKeyPressed (int row) override { choose (row); }

    void rebuild();
    void choose (int row);
    void updateProgress();

    ProjectController& c_;
    AppSettings& settings_;
    int chain_ = 0, slot_ = 0;
    juce::Label title, progress;
    juce::TextEditor search;
    juce::ListBox list { "plugins", this };
    juce::TextButton scan { "SCAN" }, rescan { "RESCAN" }, addFolder { "ADD FOLDER..." }, closeButton { "x" };
    juce::Array<Row> rows_;
    std::unique_ptr<juce::FileChooser> chooser_;
};

} // namespace jackal
