#pragma once

#include <juce_core/juce_core.h>

namespace jackal::files
{

// A project is a folder "<name>.jackal/" with project.json and samples/.
juce::File projectsRoot();                          // Documents/JackalBox/Projects
juce::File newProjectFolder();                      // Untitled-YYYYMMDD-HHMM.jackal, unique
juce::File projectJson (const juce::File& folder);
bool looksLikeProject (const juce::File& folder);

// Writes to a temp file and renames, so a crash never leaves half a project.json.
juce::Result writeProject (const juce::File& folder, const juce::String& json);
juce::Result readProject (const juce::File& folder, juce::String& json);

// Copies a sample into samples/ (adds _2, _3 ... on name clashes). Returns the relative path or empty.
juce::String importSample (const juce::File& folder, const juce::File& source, juce::String& error);

bool isAudioFile (const juce::File&);               // .wav .aif .aiff

} // namespace jackal::files
