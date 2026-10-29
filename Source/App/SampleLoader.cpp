#include "SampleLoader.h"

#include "Model/Slicer.h"

namespace jackal
{

WavePeaks computePeaks (const Sample& s, int buckets)
{
    WavePeaks p;
    p.length = s.length;
    if (s.length <= 0 || buckets <= 0)
        return p;

    p.mins.assign (static_cast<std::size_t> (buckets), 0.f);
    p.maxs.assign (static_cast<std::size_t> (buckets), 0.f);
    const float* l = s.channel (0);
    const float* r = s.channel (1);

    for (int b = 0; b < buckets; ++b)
    {
        const int64_t from = s.length * b / buckets;
        const int64_t to = std::max (from + 1, s.length * (b + 1) / buckets);
        float lo = 0.f, hi = 0.f;
        for (int64_t i = from; i < to && i < s.length; ++i)
        {
            const float v = s.numChannels > 1 ? 0.5f * (l[i] + r[i]) : l[i];
            lo = std::min (lo, v);
            hi = std::max (hi, v);
        }
        p.mins[static_cast<std::size_t> (b)] = lo;
        p.maxs[static_cast<std::size_t> (b)] = hi;
    }
    return p;
}

SampleLoader::SampleLoader (Callback onLoaded)
    : juce::Thread ("JackalSampleLoader"), onLoaded_ (std::move (onLoaded))
{
    formats_.registerBasicFormats();
    startThread();
}

SampleLoader::~SampleLoader()
{
    *alive_ = false;
    signalThreadShouldExit();
    wake_.signal();
    stopThread (4000);
}

void SampleLoader::load (int slot, int generation, const juce::File& file, const SampleSlotSpec& spec, bool guessBars)
{
    {
        const juce::ScopedLock sl (lock_);
        jobs_.push_back ({ slot, generation, file, spec, guessBars });
    }
    wake_.signal();
}

LoadedSample SampleLoader::decode (const Job& job)
{
    LoadedSample out;
    out.slot = job.slot;
    out.generation = job.generation;
    out.spec = job.spec;

    std::unique_ptr<juce::AudioFormatReader> reader (formats_.createReaderFor (job.file));
    if (reader == nullptr)
    {
        out.error = "cannot read " + job.file.getFileName();
        return out;
    }

    const auto length = static_cast<int64_t> (reader->lengthInSamples);
    if (length <= 0 || length > static_cast<int64_t> (reader->sampleRate * 600.0))
    {
        out.error = job.file.getFileName() + ": empty or longer than 10 minutes";
        return out;
    }

    const int channels = juce::jlimit (1, 2, static_cast<int> (reader->numChannels));
    juce::AudioBuffer<float> buffer (channels, static_cast<int> (length));
    reader->read (&buffer, 0, static_cast<int> (length), 0, true, channels > 1);

    auto s = std::make_unique<Sample>();
    s->numChannels = channels;
    s->sampleRate = reader->sampleRate;
    s->length = length;
    for (int c = 0; c < channels; ++c)
        s->data[static_cast<std::size_t> (c)].assign (buffer.getReadPointer (c), buffer.getReadPointer (c) + length);

    if (job.guessBars && out.spec.sourceBars <= 0.0)
        out.spec.sourceBars = guessSourceBars (static_cast<double> (length) / reader->sampleRate);

    applySlicing (*s, out.spec);
    out.peaks = computePeaks (*s);
    out.sample = std::move (s);
    return out;
}

void SampleLoader::run()
{
    while (! threadShouldExit())
    {
        Job job;
        bool have = false;
        {
            const juce::ScopedLock sl (lock_);
            if (! jobs_.empty())
            {
                job = jobs_.front();
                jobs_.pop_front();
                have = true;
            }
        }

        if (! have)
        {
            wake_.wait (500);
            continue;
        }

        auto result = std::make_shared<LoadedSample> (decode (job));
        std::weak_ptr<bool> alive = alive_;
        auto callback = onLoaded_;
        juce::MessageManager::callAsync ([alive, callback, result] {
            if (auto a = alive.lock(); a != nullptr && *a)
                callback (std::move (*result));
        });
    }
}

} // namespace jackal
