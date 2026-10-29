#include "PluginWindow.h"

#include "../JackalLookAndFeel.h"

namespace jackal
{

PluginWindow::PluginWindow (juce::AudioProcessor& processor, const juce::String& title)
    : juce::DocumentWindow (title, palette::background, juce::DocumentWindow::closeButton), processor_ (processor)
{
    setUsingNativeTitleBar (true);
    juce::AudioProcessorEditor* editor = processor.hasEditor() ? processor.createEditorAndMakeActive() : nullptr;
    if (editor == nullptr)
        editor = new juce::GenericAudioProcessorEditor (processor);
    setContentOwned (editor, true);
    setResizable (editor->isResizable(), false);
    centreWithSize (getWidth(), getHeight());
    setVisible (true);
}

PluginWindow::~PluginWindow()
{
    if (auto* editor = dynamic_cast<juce::AudioProcessorEditor*> (getContentComponent()))
        processor_.editorBeingDeleted (editor);
    clearContentComponent();
}

bool PluginWindow::keyPressed (const juce::KeyPress& key)
{
    if (key == juce::KeyPress::escapeKey)
    {
        setVisible (false);
        return true;
    }
    return juce::DocumentWindow::keyPressed (key);
}

} // namespace jackal
