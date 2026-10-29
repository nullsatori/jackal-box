#include "AppSettings.h"
#include "AudioEngine.h"
#include "JackalLookAndFeel.h"
#include "MainComponent.h"

#include <juce_gui_extra/juce_gui_extra.h>

namespace jackal
{

class MainWindow : public juce::DocumentWindow
{
public:
    MainWindow (const juce::String& name, MainComponent* content)
        : DocumentWindow (name, palette::background, DocumentWindow::allButtons)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (content, true);
        setResizable (true, true);
        setResizeLimits (800, 500, 4096, 4096);
        centreWithSize (getWidth(), getHeight());

        // space works regardless of focus (except inside a text editor)
        addKeyListener (content->commandManager().getKeyMappings());
        setVisible (true);
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }
};

class JackalBoxApplication : public juce::JUCEApplication
{
public:
    const juce::String getApplicationName() override { return JUCE_APPLICATION_NAME_STRING; }
    const juce::String getApplicationVersion() override { return JUCE_APPLICATION_VERSION_STRING; }
    bool moreThanOneInstanceAllowed() override { return false; }

    void initialise (const juce::String&) override
    {
        juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);

        settings = std::make_unique<AppSettings>();
        engine = std::make_unique<AudioEngine>();

        const auto savedState = settings->audioDeviceState();
        engine->initialise (savedState.get());

        window = std::make_unique<MainWindow> (getApplicationName(), new MainComponent (*engine, *settings));
    }

    void shutdown() override
    {
        window = nullptr;

        if (settings != nullptr && engine != nullptr)
        {
            const auto state = engine->deviceManager().createStateXml();
            settings->setAudioDeviceState (state.get());
            settings->saveNow();
        }

        engine = nullptr;
        settings = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override { quit(); }

private:
    JackalLookAndFeel lookAndFeel;
    std::unique_ptr<AppSettings> settings;
    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<MainWindow> window;
};

} // namespace jackal

START_JUCE_APPLICATION (jackal::JackalBoxApplication)
