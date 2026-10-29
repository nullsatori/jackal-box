#pragma once

#include "PluginHost.h"

#include <juce_events/juce_events.h>

#include <functional>
#include <memory>

namespace jackal
{

// Scans .vst3 files in a child process (one file at a time, 60 s timeout). A crash or a timeout
// blacklists that file and the scan goes on with a fresh child. Message thread only.
class PluginScanner : private juce::Timer
{
public:
    explicit PluginScanner (PluginHost& host);
    ~PluginScanner() override;

    void start (bool rescan);   // rescan: everything, the blacklist included
    void cancel();
    bool running() const noexcept { return running_; }
    int done() const noexcept { return index_; }
    int total() const noexcept { return files_.size(); }
    juce::String currentFile() const;

    std::function<void()> onProgress;   // also called once when the scan ends

private:
    struct Coordinator;

    void timerCallback() override;
    void next();
    void finish();
    void onReply (const juce::MemoryBlock&);
    void onLost();
    bool launch();

    PluginHost& host_;
    std::unique_ptr<Coordinator> child_;
    juce::StringArray files_;
    int index_ = 0;
    bool running_ = false, waiting_ = false;
    double sentAt_ = 0.0;
};

} // namespace jackal
