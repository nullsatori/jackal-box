#include "ProjectFiles.h"

namespace jackal::files
{

juce::File projectsRoot()
{
    return juce::File::getSpecialLocation (juce::File::userDocumentsDirectory).getChildFile ("JackalBox").getChildFile ("Projects");
}

juce::File newProjectFolder()
{
    const auto root = projectsRoot();
    root.createDirectory();
    const auto stamp = juce::Time::getCurrentTime().formatted ("%Y%m%d-%H%M");
    auto folder = root.getChildFile ("Untitled-" + stamp + ".jackal");
    for (int i = 2; folder.exists(); ++i)
        folder = root.getChildFile ("Untitled-" + stamp + "_" + juce::String (i) + ".jackal");
    return folder;
}

juce::File projectJson (const juce::File& folder)
{
    return folder.getChildFile ("project.json");
}

bool looksLikeProject (const juce::File& folder)
{
    return folder.isDirectory() && projectJson (folder).existsAsFile();
}

juce::Result writeProject (const juce::File& folder, const juce::String& json)
{
    if (! folder.createDirectory())
        return juce::Result::fail ("cannot create " + folder.getFullPathName());
    folder.getChildFile ("samples").createDirectory();

    const auto target = projectJson (folder);
    const auto temp = folder.getChildFile ("project.json.tmp");
    if (! temp.replaceWithText (json, false, false, "\n"))
        return juce::Result::fail ("cannot write " + temp.getFullPathName());
    if (! temp.moveFileTo (target))
        return juce::Result::fail ("cannot replace " + target.getFullPathName());
    return juce::Result::ok();
}

juce::Result readProject (const juce::File& folder, juce::String& json)
{
    const auto f = projectJson (folder);
    if (! f.existsAsFile())
        return juce::Result::fail ("no project.json in " + folder.getFullPathName());
    json = f.loadFileAsString();
    return juce::Result::ok();
}

juce::String importSample (const juce::File& folder, const juce::File& source, juce::String& error)
{
    const auto samples = folder.getChildFile ("samples");
    if (! samples.createDirectory())
    {
        error = "cannot create " + samples.getFullPathName();
        return {};
    }

    // already inside this project: reuse it
    if (source.isAChildOf (samples))
        return "samples/" + source.getFileName();

    auto target = samples.getChildFile (source.getFileName());
    for (int i = 2; target.exists(); ++i)
        target = samples.getChildFile (source.getFileNameWithoutExtension() + "_" + juce::String (i) + source.getFileExtension());

    if (! source.copyFileTo (target))
    {
        error = "cannot copy " + source.getFileName();
        return {};
    }
    return "samples/" + target.getFileName();
}

bool isAudioFile (const juce::File& f)
{
    return f.hasFileExtension ("wav;aif;aiff");
}

} // namespace jackal::files
