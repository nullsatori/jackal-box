#pragma once

#include "Model/OfflineRender.h"
#include "Model/ProjectPublisher.h"
#include "Model/WavFile.h"

#include <juce_core/juce_core.h>

#include <array>
#include <atomic>
#include <memory>

namespace jackal
{

// Everything a render needs, collected on the message thread before the thread starts (contract stage 5, §9.4).
// The job owns the copies: the project, deep copies of the live samples and second plugin instances.
struct ExportSources
{
    std::unique_ptr<Project> project;
    std::array<Sample*, kNumSampleSlots> samples {};   // ownership -> the job's EngineCore
    PublishedInserts inserts;                          // ownership -> the job's EngineCore
    RenderOptions options;
    double sampleRate = 48000.0;
    WavDepth depth = WavDepth::Pcm24;
    juce::File folder;                                 // <export folder>/<project>_<YYYYMMDD-HHMMSS>
    juce::StringArray warnings;                        // "skipped: T3·1 Limiter (missing)"
};

struct ExportOutcome
{
    RenderStatus status = RenderStatus::Error;
    juce::String error;
    juce::File folder;
    double seconds = 0.0;        // rendered length
    int stemsWritten = 0;
    float peakDb = -100.f;
    int64_t clipped = 0;         // clipped sample frames over all written files (PCM only)
    juce::StringArray warnings;
    std::vector<InsertProcessor*> retiredInserts;   // deleted on the message thread
};

struct ExportState
{
    bool running = false;
    double fraction = 0.0;
    double renderedSeconds = 0.0, totalSeconds = 0.0;
    double speed = 0.0;          // x realtime
    bool hasResult = false;
    const ExportOutcome* result = nullptr;
    juce::String resultText;     // the line the status bar showed when it finished
    bool resultAlarm = false;    // clipped or failed: red
};

// juce::Thread "Jackal Export" with its own EngineCore. Nothing here touches the live engine.
// The controller polls isDone() on its timer and then takes the outcome (instead of callAsync: see DEVIATIONS).
class ExportJob : private juce::Thread
{
public:
    explicit ExportJob (ExportSources sources);
    ~ExportJob() override;   // cancels and waits

    void begin();
    void cancel() noexcept { progress_.cancel = true; }
    bool stop (int timeoutMs);   // cancel + stopThread
    bool isDone() const noexcept { return done_.load (std::memory_order_acquire); }
    ExportOutcome takeOutcome();   // after isDone()

    double fraction() const noexcept { return progress_.fraction.load (std::memory_order_relaxed); }
    double totalSeconds() const noexcept { return totalSeconds_; }
    double elapsedSeconds() const;

private:
    void run() override;
    void write (const RenderBuffers& buffers);

    ExportSources src_;
    RenderProgress progress_;
    ExportOutcome outcome_;
    std::atomic<bool> done_ { false };
    double totalSeconds_ = 0.0;
    double startMs_ = 0.0;
};

} // namespace jackal
