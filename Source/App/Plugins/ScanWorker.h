#pragma once

#include <juce_core/juce_core.h>

namespace jackal::scanworker
{

constexpr const char* kUid = "jackal-scan";

// Main.cpp calls this first. True when this process is the scan child: it serves the coordinator
// until the connection drops, and the app must not open a window, audio or a project.
bool tryRun (const juce::String& commandLine);
bool isWorkerCommandLine (const juce::String& commandLine);

} // namespace jackal::scanworker
