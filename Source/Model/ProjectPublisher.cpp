#include "ProjectPublisher.h"

namespace jackal
{

namespace
{
    template <typename T>
    constexpr std::size_t ix (T i) noexcept { return static_cast<std::size_t> (i); }
} // namespace

void writeFxParams (ParamTable& table, int chain, const FxSettings& fx)
{
    for (int p = 0; p < kNumFxParams; ++p)
        table.set (fxParam (chain, static_cast<FxParam> (p)), fx.params[ix (p)]);
    table.set (fxParam (chain, FxParam::Bypass), fx.bypass ? 1.f : 0.f);
    table.set (fxParam (chain, FxParam::Preset), static_cast<float> (fx.preset));
}

void publishProject (EngineCore& core, const Project& p, const std::array<Sample*, kNumSampleSlots>& samples,
                     const PublishedInserts& inserts)
{
    auto& q = core.commands();
    auto& params = core.params();
    auto& table = params.params;

    for (int s = 0; s < kNumSampleSlots; ++s)
        if (samples[ix (s)] != nullptr)
            q.push ({ CommandType::SetSlotSample, 0.f, s, samples[ix (s)] });

    for (int t = 0; t < kNumTracks; ++t)
    {
        const auto& tr = p.tracks[ix (t)];
        q.push ({ CommandType::SetTrackSlot, 0.f, t | ((tr.sampleSlot < 0 ? 0xFF : tr.sampleSlot) << 8) });
        q.push ({ CommandType::SetTrackChoke, 0.f, t | (tr.chokeGroup << 8) });
        q.push ({ CommandType::SetTrackFit, 0.f, t | ((tr.fitToTempo ? 1 : 0) << 8) });
        for (int k = 0; k < kNumTrackParams; ++k)
            table.set (trackParam (t, static_cast<TrackParam> (k)), tr.params[ix (k)]);
        writeFxParams (table, t, tr.fx);
    }

    table.set (masterParam (MasterParam::Gain), p.masterGainDb);
    table.set (masterParam (MasterParam::Shakal), p.masterShakal);
    writeFxParams (table, kMasterChain, p.masterFx);

    const auto& g = p.guitar;
    table.set (guitarParam (GuitarParam::Shakal), g.shakal);
    table.set (guitarParam (GuitarParam::GateDb), g.gateDb);
    table.set (guitarParam (GuitarParam::HpfHz), g.hpfHz);
    table.set (guitarParam (GuitarParam::LoopGainDb), g.loopGainDb);
    table.set (guitarParam (GuitarParam::LoopTap), g.loopTap == LoopTap::Amp ? 1.f : 0.f);
    writeFxParams (table, kGuitarChain, g.fx);

    for (int t = 0; t < kNumTracks; ++t)
    {
        params.trackMute[ix (t)].store (p.tracks[ix (t)].mute, std::memory_order_relaxed);
        params.trackSolo[ix (t)].store (p.tracks[ix (t)].solo, std::memory_order_relaxed);
    }

    q.push ({ CommandType::SetSeed, 0.f, static_cast<int32_t> (p.seed) });
    q.push ({ CommandType::SetBpm, static_cast<float> (p.bpm), 0 });

    for (int i = 0; i < kNumPatterns; ++i)
        q.push ({ CommandType::SetPattern, 0.f, i, new Pattern (p.patterns[ix (i)]) });

    for (int c = 0; c < kNumChains; ++c)
        for (int sl = 0; sl < kSlotsPerChain; ++sl)
        {
            const auto& spec = c == kMasterChain ? p.masterInserts[ix (sl)]
                             : c == kGuitarChain ? p.guitar.inserts[ix (sl)]
                                                 : p.tracks[ix (c)].inserts[ix (sl)];
            table.set (insertParam (c, sl, InsertParam::Bypass), spec.bypass ? 1.f : 0.f);
            if (InsertProcessor* proc = inserts.slots[ix (c * kSlotsPerChain + sl)])
                q.push ({ CommandType::SetInsert, 0.f, c | (sl << 8), proc });
        }

    q.push ({ CommandType::SetSong, 0.f, 0, new SongData (p.song) });
}

} // namespace jackal
