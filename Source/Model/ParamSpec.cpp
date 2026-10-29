#include "ParamSpec.h"

#include <map>

namespace jackal
{

namespace
{
    constexpr std::array<std::string_view, 3> kPresetChoices { "iron", "net", "mem" };
    constexpr std::array<std::string_view, 2> kTapChoices { "di", "amp" };

    struct Table
    {
        std::array<ParamInfo, kNumParams> infos {};
        std::array<bool, kNumParams> defined {};
        std::map<std::string, ParamId, std::less<>> byAddress;
        std::array<std::string, kNumParams> addresses;
    };

    std::string chainName (int chain)
    {
        if (chain == kMasterChain) return "master";
        if (chain == kGuitarChain) return "guitar";
        return std::to_string (chain);
    }

    ParamScale trackScale (TrackParam p)
    {
        return (p == TrackParam::Decay || p == TrackParam::Tone) ? ParamScale::Log : ParamScale::Linear;
    }

    ParamScale fxScale (FxParam p)
    {
        switch (p)
        {
            case FxParam::Sp12FilterHz: case FxParam::DecimRateHz: case FxParam::GlitchLenMs:
                return ParamScale::Log;
            case FxParam::Preset:
                return ParamScale::Choice;
            case FxParam::Bypass: case FxParam::DriveOn: case FxParam::DriveMode: case FxParam::Sp12On: case FxParam::DecimOn:
            case FxParam::CrushOn: case FxParam::CrushMulaw: case FxParam::FlipOn: case FxParam::FlipMode: case FxParam::GlitchOn:
                return ParamScale::Toggle;
            default:
                return ParamScale::Linear;
        }
    }

    const Table& table()
    {
        static const Table* t = [] {
            auto* tab = new Table();
            auto add = [tab] (ParamId id, std::string addr, ParamInfo in) {
                tab->infos[id] = in;
                tab->defined[id] = true;
                tab->byAddress.emplace (addr, id);
                tab->addresses[id] = std::move (addr);
            };

            add (masterParam (MasterParam::Gain), "master.gain",
                 { "gain", kMasterGainSpec.label, kMasterGainSpec.unit, kMasterGainSpec.minValue, kMasterGainSpec.maxValue,
                   kMasterGainSpec.defaultValue, ParamScale::Linear, {} });
            add (masterParam (MasterParam::Shakal), "master.shakal",
                 { "shakal", kMasterShakalSpec.label, kMasterShakalSpec.unit, 0.f, 1.f, 0.f, ParamScale::Linear, {} });

            for (int tr = 0; tr < kNumTracks; ++tr)
                for (int k = 0; k < kNumTrackParams; ++k)
                {
                    const auto& sp = kTrackParamSpecs[static_cast<std::size_t> (k)];
                    add (trackParam (tr, static_cast<TrackParam> (k)), "track." + std::to_string (tr) + "." + std::string (sp.name),
                         { sp.name, sp.label, sp.unit, sp.minValue, sp.maxValue, sp.defaultValue, trackScale (static_cast<TrackParam> (k)), {} });
                }

            for (int c = 0; c < kNumChains; ++c)
                for (int k = 0; k < kNumFxParams; ++k)
                {
                    const auto fp = static_cast<FxParam> (k);
                    const auto& sp = kFxParamSpecs[static_cast<std::size_t> (k)];
                    ParamInfo in { sp.name, sp.label, sp.unit, sp.minValue, sp.maxValue, sp.defaultValue, fxScale (fp), {} };
                    if (fp == FxParam::Preset)
                        in.choices = kPresetChoices;
                    add (fxParam (c, fp), "fx." + chainName (c) + "." + std::string (sp.name), in);
                }

            for (int k = 0; k < kNumGuitarParams; ++k)
            {
                const auto gp = static_cast<GuitarParam> (k);
                const auto& sp = kGuitarParamSpecs[static_cast<std::size_t> (k)];
                ParamInfo in { sp.name, sp.label, sp.unit, sp.minValue, sp.maxValue, sp.defaultValue,
                               gp == GuitarParam::HpfHz ? ParamScale::Log : ParamScale::Linear, {} };
                if (gp == GuitarParam::LoopTap)
                {
                    in.scale = ParamScale::Choice;
                    in.choices = kTapChoices;
                }
                add (guitarParam (gp), "guitar." + std::string (sp.name), in);
            }

            for (int c = 0; c < kNumChains; ++c)
                for (int sl = 0; sl < kSlotsPerChain; ++sl)
                    add (insertParam (c, sl, InsertParam::Bypass), "insert." + chainName (c) + "." + std::to_string (sl) + ".bypass",
                         { kInsertBypassNames[static_cast<std::size_t> (sl)], "BYP", "", 0.f, 1.f, 0.f, ParamScale::Toggle, {} });
            return tab;
        }();
        return *t;
    }
} // namespace

bool isDefined (ParamId id) noexcept
{
    return id < kNumParams && table().defined[id];
}

const ParamInfo& info (ParamId id) noexcept
{
    return table().infos[id < kNumParams ? id : 0];
}

std::string address (ParamId id)
{
    return isDefined (id) ? table().addresses[id] : std::string {};
}

std::optional<ParamId> fromAddress (std::string_view a)
{
    const auto& m = table().byAddress;
    const auto it = m.find (a);
    if (it == m.end())
        return std::nullopt;
    return it->second;
}

} // namespace jackal
