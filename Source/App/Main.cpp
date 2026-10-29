#include "AppSettings.h"
#include "AudioEngine.h"
#include "JackalLookAndFeel.h"
#include "MainComponent.h"
#include "ProjectController.h"

#include <juce_gui_extra/juce_gui_extra.h>

namespace jackal
{

class MainWindow : public juce::DocumentWindow
{
public:
    MainWindow (const juce::String& name, MainComponent* content, AppSettings& s)
        : DocumentWindow (name, palette::background, DocumentWindow::allButtons), settings (s)
    {
        setUsingNativeTitleBar (true);
        setContentOwned (content, true);
        setResizable (true, true);
        setResizeLimits (1100, 680, 4096, 4096);

        const auto saved = settings.windowBounds();
        if (saved.isNotEmpty())
            restoreWindowStateFromString (saved);
        else
            centreWithSize (getWidth(), getHeight());

        // keys work regardless of focus (except inside a text editor)
        addKeyListener (content->commandManager().getKeyMappings());
        setVisible (true);
        content->grabKeyboardFocus();
    }

    ~MainWindow() override
    {
        settings.setWindowBounds (getWindowStateAsString());
    }

    void closeButtonPressed() override
    {
        juce::JUCEApplication::getInstance()->systemRequestedQuit();
    }

private:
    AppSettings& settings;
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

        // monitor settings live in AppSettings (stage 1)
        engine->core().params().monitorEnabled.store (settings->monitorEnabled());
        engine->core().params().monitorGainDb.store (settings->monitorGainDb());
        engine->core().commands().push ({ CommandType::SetInputChannel, 0.f, settings->inputChannel() });

        // MIDI: the saved device, or the first one on the very first launch
        auto midiId = settings->midiInputId();
        if (midiId.isEmpty() && settings->lastProjectPath().isEmpty())
        {
            const auto devices = juce::MidiInput::getAvailableDevices();
            if (! devices.isEmpty())
                midiId = devices[0].identifier;
        }
        engine->openMidiInput (midiId);

        controller = std::make_unique<ProjectController> (*engine, *settings);
        controller->start();

        window = std::make_unique<MainWindow> (getApplicationName(), new MainComponent (*engine, *settings, *controller), *settings);
    }

    void shutdown() override
    {
        window = nullptr;

        if (settings != nullptr && engine != nullptr)
        {
            const auto state = engine->deviceManager().createStateXml();
            settings->setAudioDeviceState (state.get());
        }

        if (controller != nullptr)
            controller->shutdown();   // saves, removes the audio callback, frees every engine pointer
        controller = nullptr;

        if (settings != nullptr)
            settings->saveNow();

        engine = nullptr;
        settings = nullptr;
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    }

    void systemRequestedQuit() override { quit(); }

private:
    JackalLookAndFeel lookAndFeel;
    std::unique_ptr<AppSettings> settings;
    std::unique_ptr<AudioEngine> engine;
    std::unique_ptr<ProjectController> controller;
    std::unique_ptr<MainWindow> window;
};

} // namespace jackal

START_JUCE_APPLICATION (jackal::JackalBoxApplication)
