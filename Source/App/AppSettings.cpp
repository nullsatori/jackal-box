#include "AppSettings.h"

#include "Engine/EngineTypes.h"

namespace jackal
{

namespace keys
{
    const juce::String audioDeviceState = "audioDeviceState";
    const juce::String bpm = "bpm";
    const juce::String metronomeEnabled = "metronomeEnabled";
    const juce::String metronomeGain = "metronomeGain";
    const juce::String monitorEnabled = "monitorEnabled";
    const juce::String monitorGainDb = "monitorGainDb";
    const juce::String inputChannel = "inputChannel";
    const juce::String lastProjectPath = "lastProjectPath";
    const juce::String midiInputId = "midiInputId";
    const juce::String windowBounds = "windowBounds";
    const juce::String recentProjects = "recentProjects";
} // namespace keys

AppSettings::AppSettings()
{
    juce::PropertiesFile::Options options;
    options.applicationName = "JackalBox";
    options.filenameSuffix = "xml";
    options.folderName = "JackalBox";
    options.osxLibrarySubFolder = "Application Support";
    options.storageFormat = juce::PropertiesFile::storeAsXML;
    options.millisecondsBeforeSaving = -1; // we debounce ourselves
    properties.setStorageParameters (options);
}

AppSettings::~AppSettings()
{
    saveNow();
}

juce::PropertiesFile& AppSettings::file() const
{
    return *const_cast<juce::ApplicationProperties&> (properties).getUserSettings();
}

void AppSettings::set (const juce::String& key, const juce::var& value)
{
    file().setValue (key, value);
    startTimer (500); // debounce: every change restarts the 500 ms window
}

void AppSettings::timerCallback()
{
    stopTimer();
    file().saveIfNeeded();
}

void AppSettings::saveNow()
{
    stopTimer();
    file().saveIfNeeded();
}

std::unique_ptr<juce::XmlElement> AppSettings::audioDeviceState() const
{
    return file().getXmlValue (keys::audioDeviceState);
}

void AppSettings::setAudioDeviceState (const juce::XmlElement* state)
{
    file().setValue (keys::audioDeviceState, state);
    startTimer (500);
}

double AppSettings::bpm() const             { return file().getDoubleValue (keys::bpm, kDefaultBpm); }
bool AppSettings::metronomeEnabled() const  { return file().getBoolValue (keys::metronomeEnabled, true); }
float AppSettings::metronomeGain() const    { return static_cast<float> (file().getDoubleValue (keys::metronomeGain, 0.5)); }
bool AppSettings::monitorEnabled() const    { return file().getBoolValue (keys::monitorEnabled, true); }
float AppSettings::monitorGainDb() const    { return static_cast<float> (file().getDoubleValue (keys::monitorGainDb, 0.0)); }
int AppSettings::inputChannel() const       { return file().getIntValue (keys::inputChannel, 0); }

void AppSettings::setBpm (double value)             { set (keys::bpm, value); }
void AppSettings::setMetronomeEnabled (bool value)  { set (keys::metronomeEnabled, value); }
void AppSettings::setMetronomeGain (float value)    { set (keys::metronomeGain, value); }
void AppSettings::setMonitorEnabled (bool value)    { set (keys::monitorEnabled, value); }
void AppSettings::setMonitorGainDb (float value)    { set (keys::monitorGainDb, value); }
void AppSettings::setInputChannel (int value)       { set (keys::inputChannel, value); }

juce::String AppSettings::lastProjectPath() const { return file().getValue (keys::lastProjectPath); }
juce::String AppSettings::midiInputId() const     { return file().getValue (keys::midiInputId); }
juce::String AppSettings::windowBounds() const    { return file().getValue (keys::windowBounds); }

juce::StringArray AppSettings::recentProjects() const
{
    return juce::StringArray::fromTokens (file().getValue (keys::recentProjects), ";", "");
}

void AppSettings::setLastProjectPath (const juce::String& path)
{
    set (keys::lastProjectPath, path);
    auto recent = recentProjects();
    recent.removeString (path);
    recent.removeEmptyStrings();
    recent.insert (0, path);
    while (recent.size() > 8)
        recent.remove (recent.size() - 1);
    set (keys::recentProjects, recent.joinIntoString (";"));
}

void AppSettings::setMidiInputId (const juce::String& id)      { set (keys::midiInputId, id); }
void AppSettings::setWindowBounds (const juce::String& bounds) { set (keys::windowBounds, bounds); }

} // namespace jackal
