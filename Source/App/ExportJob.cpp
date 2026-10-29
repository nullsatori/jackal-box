#include "ExportJob.h"

#include <cmath>

namespace jackal
{

ExportJob::ExportJob (ExportSources sources) : juce::Thread ("Jackal Export"), src_ (std::move (sources))
{
    totalSeconds_ = static_cast<double> (renderLength (*src_.project, src_.options, src_.sampleRate)) / src_.sampleRate;
}

ExportJob::~ExportJob()
{
    stop (5000);
    // a job that never ran still owns its copies
    if (! isDone())
    {
        for (auto*& s : src_.samples)
        {
            delete s;
            s = nullptr;
        }
        for (auto*& p : src_.inserts.slots)
        {
            delete p;
            p = nullptr;
        }
    }
    for (auto* p : outcome_.retiredInserts)   // outcome never taken (shutdown)
        delete p;
}

void ExportJob::begin()
{
    startMs_ = juce::Time::getMillisecondCounterHiRes();
    startThread();
}

bool ExportJob::stop (int timeoutMs)
{
    cancel();
    return stopThread (timeoutMs);
}

double ExportJob::elapsedSeconds() const
{
    return startMs_ > 0.0 ? (juce::Time::getMillisecondCounterHiRes() - startMs_) / 1000.0 : 0.0;
}

ExportOutcome ExportJob::takeOutcome()
{
    return std::move (outcome_);
}

void ExportJob::run()
{
    outcome_.folder = src_.folder;
    outcome_.warnings = src_.warnings;

    RenderBuffers buffers;
    {
        EngineCore core;
        core.prepare (src_.sampleRate, src_.options.blockSize, 0, 2);
        publishProject (core, *src_.project, src_.samples, src_.inserts);
        src_.samples = {};   // the core owns them now (renderOffline hands them back and deletes them)
        src_.inserts = {};
        const auto r = renderOffline (core, *src_.project, src_.options, buffers, &progress_);
        outcome_.status = r.status;
        outcome_.error = juce::String (r.error);
        outcome_.retiredInserts = r.retiredInserts;
    }

    if (outcome_.status == RenderStatus::Done)
        write (buffers);

    done_.store (true, std::memory_order_release);
}

void ExportJob::write (const RenderBuffers& b)
{
    // files are written only after a complete render: a cancel leaves nothing behind
    const auto stemsDir = src_.folder.getChildFile ("stems");
    if (! src_.folder.createDirectory() || (src_.options.stems && ! stemsDir.createDirectory()))
    {
        outcome_.status = RenderStatus::Error;
        outcome_.error = "cannot create " + src_.folder.getFullPathName();
        return;
    }

    const auto length = static_cast<int64_t> (b.master[0].size());
    outcome_.seconds = static_cast<double> (length) / src_.sampleRate;
    outcome_.peakDb = b.peakMaster > 0.f ? 20.f * std::log10 (b.peakMaster) : -100.f;

    auto writeOne = [&] (const juce::File& f, const std::array<std::vector<float>, 2>& ch) {
        const float* ptrs[2] = { ch[0].data(), ch[1].data() };
        const auto res = writeWav (f.getFullPathName().toStdString(), ptrs, 2, static_cast<int64_t> (ch[0].size()), src_.sampleRate, src_.depth);
        if (! res.ok)
        {
            outcome_.status = RenderStatus::Error;
            outcome_.error = juce::String (res.error);
            return false;
        }
        outcome_.clipped += res.clippedSamples;
        return true;
    };

    if (! writeOne (src_.folder.getChildFile ("master.wav"), b.master))
        return;

    if (! src_.options.stems)
        return;

    for (int s = 0; s < kNumStems; ++s)
    {
        if (b.stemSilent[static_cast<std::size_t> (s)] || b.stems[static_cast<std::size_t> (s)][0].empty())
            continue;
        const std::string name = s < kNumTracks ? src_.project->tracks[static_cast<std::size_t> (s)].name : std::string ("GUITAR");
        if (! writeOne (stemsDir.getChildFile (juce::String (stemFileName (s, name))), b.stems[static_cast<std::size_t> (s)]))
            return;
        ++outcome_.stemsWritten;
    }
}

} // namespace jackal
