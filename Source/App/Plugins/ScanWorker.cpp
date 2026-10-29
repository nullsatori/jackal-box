#include "ScanWorker.h"

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_events/juce_events.h>

#include <cstdlib>
#include <memory>

namespace jackal::scanworker
{

namespace
{
    class Worker : public juce::ChildProcessWorker
    {
    public:
        Worker()
        {
#if JUCE_PLUGINHOST_VST3
            format_ = std::make_unique<juce::VST3PluginFormat>();
#endif
        }

        void handleMessageFromCoordinator (const juce::MemoryBlock& mb) override
        {
            const auto path = mb.toString();
            const auto crashOn = juce::SystemStats::getEnvironmentVariable ("JACKAL_SCAN_CRASH_ON", {});
            if (crashOn.isNotEmpty() && path.containsIgnoreCase (crashOn))
                std::abort();   // manual blacklist check without a real crashing plugin

            juce::XmlElement result ("plugins");
            if (format_ != nullptr)
            {
                juce::OwnedArray<juce::PluginDescription> found;
                format_->findAllTypesForFile (found, path);
                for (auto* d : found)
                    result.addChildElement (d->createXml().release());
            }
            const auto text = result.toString();
            sendMessageToCoordinator (juce::MemoryBlock (text.toRawUTF8(), text.getNumBytesAsUTF8()));
        }

        void handleConnectionLost() override
        {
            juce::JUCEApplicationBase::quit();
        }

    private:
        std::unique_ptr<juce::AudioPluginFormat> format_;
    };

    std::unique_ptr<Worker> worker;
} // namespace

bool isWorkerCommandLine (const juce::String& commandLine)
{
    return commandLine.contains (juce::String ("--") + kUid);
}

bool tryRun (const juce::String& commandLine)
{
    if (! isWorkerCommandLine (commandLine))
        return false;
    worker = std::make_unique<Worker>();
    if (! worker->initialiseFromCommandLine (commandLine, kUid, 10000))
    {
        worker = nullptr;
        juce::JUCEApplicationBase::quit();
    }
    return true;
}

} // namespace jackal::scanworker
