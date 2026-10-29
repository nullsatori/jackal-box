#include "PluginScanner.h"

#include "ScanWorker.h"

namespace jackal
{

namespace
{
    constexpr double kTimeoutMs = 60000.0;
}

struct PluginScanner::Coordinator : juce::ChildProcessCoordinator
{
    explicit Coordinator (PluginScanner& s) : owner (s) {}

    void handleMessageFromWorker (const juce::MemoryBlock& mb) override
    {
        juce::MessageManager::callAsync ([safe = juce::WeakReference<Coordinator> (this), mb] {
            if (safe != nullptr)
                safe->owner.onReply (mb);
        });
    }

    void handleConnectionLost() override
    {
        juce::MessageManager::callAsync ([safe = juce::WeakReference<Coordinator> (this)] {
            if (safe != nullptr)
                safe->owner.onLost();
        });
    }

    PluginScanner& owner;
    JUCE_DECLARE_WEAK_REFERENCEABLE (Coordinator)
};

PluginScanner::PluginScanner (PluginHost& host) : host_ (host) {}

PluginScanner::~PluginScanner()
{
    cancel();
}

juce::String PluginScanner::currentFile() const
{
    return index_ < files_.size() ? juce::File (files_[index_]).getFileName() : juce::String();
}

void PluginScanner::start (bool rescan)
{
    if (running_)
        return;

    auto& list = host_.list();
    if (rescan)
        list.clearBlacklistedFiles();

    juce::StringArray knownFiles;
    for (const auto& d : list.getTypes())
        knownFiles.addIfNotAlreadyThere (d.fileOrIdentifier);

    files_.clear();
    for (const auto& dir : host_.searchPaths())
    {
        const juce::File folder (dir);
        if (! folder.isDirectory())
            continue;
        // .vst3 is a bundle folder on Windows (and a plain file for old plugins): take both, do not descend into bundles
        juce::Array<juce::File> found;
        folder.findChildFiles (found, juce::File::findFilesAndDirectories | juce::File::ignoreHiddenFiles, true, "*.vst3");
        for (const auto& f : found)
        {
            bool insideBundle = false;
            for (auto p = f.getParentDirectory(); p != folder && p != juce::File(); p = p.getParentDirectory())
                insideBundle = insideBundle || p.hasFileExtension ("vst3");
            if (insideBundle)
                continue;
            const auto path = f.getFullPathName();
            const bool known = knownFiles.contains (path);
            if (! rescan && (known || list.getBlacklistedFiles().contains (path)))
                continue;
            files_.addIfNotAlreadyThere (path);
        }
    }

    index_ = 0;
    running_ = true;
    if (files_.isEmpty())
    {
        finish();
        return;
    }
    if (! launch())
    {
        finish();
        return;
    }
    startTimer (500);
    next();
}

bool PluginScanner::launch()
{
    child_ = std::make_unique<Coordinator> (*this);
    return child_->launchWorkerProcess (juce::File::getSpecialLocation (juce::File::currentExecutableFile), scanworker::kUid, 10000);
}

void PluginScanner::next()
{
    if (onProgress)
        onProgress();
    if (index_ >= files_.size())
    {
        finish();
        return;
    }
    const auto path = files_[index_];
    waiting_ = true;
    sentAt_ = juce::Time::getMillisecondCounterHiRes();
    if (! child_->sendMessageToWorker (juce::MemoryBlock (path.toRawUTF8(), path.getNumBytesAsUTF8())))
        onLost();
}

void PluginScanner::onReply (const juce::MemoryBlock& mb)
{
    if (! running_ || ! waiting_)
        return;
    waiting_ = false;
    if (auto xml = juce::XmlDocument::parse (mb.toString()))
        for (auto* e : xml->getChildIterator())
        {
            juce::PluginDescription d;
            if (d.loadFromXml (*e))
                host_.list().addType (d);
        }
    host_.saveCache();
    ++index_;
    next();
}

void PluginScanner::onLost()
{
    if (! running_ || ! waiting_)
        return;
    waiting_ = false;
    host_.list().addToBlacklist (files_[index_]);
    host_.saveCache();
    ++index_;
    if (index_ < files_.size() && ! launch())
    {
        finish();
        return;
    }
    next();
}

void PluginScanner::timerCallback()
{
    if (waiting_ && juce::Time::getMillisecondCounterHiRes() - sentAt_ > kTimeoutMs)
    {
        child_->killWorkerProcess();
        onLost();
    }
}

void PluginScanner::cancel()
{
    if (running_)
        finish();
}

void PluginScanner::finish()
{
    stopTimer();
    running_ = waiting_ = false;
    if (child_ != nullptr)
        child_->killWorkerProcess();
    child_ = nullptr;
    host_.saveCache();
    if (onProgress)
        onProgress();
}

} // namespace jackal
