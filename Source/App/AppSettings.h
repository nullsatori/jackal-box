#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace jackal
{

// %APPDATA%\JackalBox\JackalBox.xml on Windows.
class AppSettings : private juce::Timer
{
public:
    AppSettings();
    ~AppSettings() override;

    std::unique_ptr<juce::XmlElement> audioDeviceState() const;
    void setAudioDeviceState (const juce::XmlElement* state);

    double bpm() const;
    bool metronomeEnabled() const;
    float metronomeGain() const;
    bool monitorEnabled() const;
    float monitorGainDb() const;
    int inputChannel() const;

    void setBpm (double value);
    void setMetronomeEnabled (bool value);
    void setMetronomeGain (float value);
    void setMonitorEnabled (bool value);
    void setMonitorGainDb (float value);
    void setInputChannel (int value);

    // stage 2
    juce::String lastProjectPath() const;
    juce::String midiInputId() const;
    juce::String windowBounds() const;
    juce::StringArray recentProjects() const;
    void setLastProjectPath (const juce::String& path);   // also pushes it to the recent list
    void setMidiInputId (const juce::String& id);
    void setWindowBounds (const juce::String& bounds);

    void saveNow();

private:
    void set (const juce::String& key, const juce::var& value);
    void timerCallback() override;
    juce::PropertiesFile& file() const;

    juce::ApplicationProperties properties;
};

} // namespace jackal
