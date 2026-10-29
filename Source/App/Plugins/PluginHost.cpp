#include "PluginHost.h"

namespace jackal
{

PluginHost::PluginHost (AppSettings& settings) : settings_ (settings)
{
#if JUCE_PLUGINHOST_VST3
    formats_.addFormat (std::make_unique<juce::VST3PluginFormat>());
#endif
    if (auto xml = juce::XmlDocument::parse (cacheFile()))
        list_.recreateFromXml (*xml);
}

juce::File PluginHost::cacheFile()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
        .getChildFile ("JackalBox").getChildFile ("plugins.xml");
}

juce::AudioPluginFormat* PluginHost::vst3() const
{
    return formats_.getNumFormats() > 0 ? formats_.getFormat (0) : nullptr;
}

void PluginHost::saveCache()
{
    if (auto xml = list_.createXml())
    {
        cacheFile().getParentDirectory().createDirectory();
        xml->writeTo (cacheFile());
    }
}

juce::StringArray PluginHost::searchPaths() const
{
    juce::StringArray paths;
    if (auto* f = vst3())
    {
        const auto def = f->getDefaultLocationsToSearch();
        for (int i = 0; i < def.getNumPaths(); ++i)
            paths.addIfNotAlreadyThere (def.getRawString (i));
    }
    for (const auto& p : settings_.vst3Paths())
        paths.addIfNotAlreadyThere (p);
    return paths;
}

std::unique_ptr<juce::PluginDescription> PluginHost::find (const juce::String& identifier) const
{
    return list_.getTypeForIdentifierString (identifier);
}

std::unique_ptr<juce::AudioPluginInstance> PluginHost::create (const juce::PluginDescription& d, double sampleRate, int blockSize, juce::String& error)
{
    if (d.isInstrument)
    {
        error = d.name + " is an instrument (instruments come after the MVP)";
        return nullptr;
    }
    return formats_.createPluginInstance (d, sampleRate, blockSize, error);
}

} // namespace jackal
