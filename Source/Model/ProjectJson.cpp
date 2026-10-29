#include "ProjectJson.h"

#include "ParamSpec.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cmath>

namespace jackal
{

using Json = nlohmann::ordered_json;

namespace
{
    const char* speedName (LaneSpeed s)
    {
        switch (s)
        {
            case LaneSpeed::Half:    return "x05";
            case LaneSpeed::Normal:  return "x1";
            case LaneSpeed::Double:  return "x2";
            case LaneSpeed::Triplet: return "t";
        }
        return "x1";
    }

    LaneSpeed speedFromName (const std::string& s)
    {
        if (s == "x05") return LaneSpeed::Half;
        if (s == "x2")  return LaneSpeed::Double;
        if (s == "t")   return LaneSpeed::Triplet;
        return LaneSpeed::Normal;
    }

    const char* condName (StepCondition c)
    {
        switch (c)
        {
            case StepCondition::Always:     return "always";
            case StepCondition::Fill:       return "fill";
            case StepCondition::NotFill:    return "notfill";
            case StepCondition::FirstCycle: return "first";
            case StepCondition::Cycle1of2:  return "1:2";
            case StepCondition::Cycle2of2:  return "2:2";
        }
        return "always";
    }

    StepCondition condFromName (const std::string& s)
    {
        if (s == "fill")    return StepCondition::Fill;
        if (s == "notfill") return StepCondition::NotFill;
        if (s == "first")   return StepCondition::FirstCycle;
        if (s == "1:2")     return StepCondition::Cycle1of2;
        if (s == "2:2")     return StepCondition::Cycle2of2;
        return StepCondition::Always;
    }

    const char* sliceName (SliceMode m)
    {
        switch (m)
        {
            case SliceMode::Off:       return "off";
            case SliceMode::Grid16:    return "grid16";
            case SliceMode::Grid32:    return "grid32";
            case SliceMode::Transient: return "transient";
        }
        return "off";
    }

    SliceMode sliceFromName (const std::string& s)
    {
        if (s == "grid16")    return SliceMode::Grid16;
        if (s == "grid32")    return SliceMode::Grid32;
        if (s == "transient") return SliceMode::Transient;
        return SliceMode::Off;
    }

    // ---- reading helpers: missing or wrong type = default, numbers clamped

    template <typename T>
    T num (const Json& j, const char* key, T def, T lo, T hi)
    {
        const auto it = j.find (key);
        if (it == j.end() || ! it->is_number())
            return def;
        const double v = it->get<double>();
        if (! std::isfinite (v))
            return def;
        const double c = std::clamp (v, static_cast<double> (lo), static_cast<double> (hi));
        if constexpr (std::is_integral_v<T>)
            return static_cast<T> (std::llround (c));
        else
            return static_cast<T> (c);
    }

    bool flag (const Json& j, const char* key, bool def)
    {
        const auto it = j.find (key);
        return (it != j.end() && it->is_boolean()) ? it->get<bool>() : def;
    }

    std::string str (const Json& j, const char* key, const std::string& def)
    {
        const auto it = j.find (key);
        return (it != j.end() && it->is_string()) ? it->get<std::string>() : def;
    }

    const Json* arr (const Json& j, const char* key)
    {
        const auto it = j.find (key);
        return (it != j.end() && it->is_array()) ? &*it : nullptr;
    }

    const Json* obj (const Json& j, const char* key)
    {
        const auto it = j.find (key);
        return (it != j.end() && it->is_object()) ? &*it : nullptr;
    }

    // ---- SHAKAL settings (v3)

    Json fxToJson (const FxSettings& f)
    {
        Json params = Json::object();
        for (int i = 0; i < kNumFxParams; ++i)
        {
            const auto p = static_cast<FxParam> (i);
            if (p == FxParam::Bypass || p == FxParam::Preset)
                continue;
            const auto& spec = fxParamSpec (p);
            const float v = f.params[static_cast<std::size_t> (i)];
            const std::string key (spec.name);
            if (spec.scale == ParamScale::Toggle)
                params[key] = v >= 0.5f;
            else if (spec.scale == ParamScale::Choice)
                params[key] = std::string (spec.choices[static_cast<std::size_t> (std::clamp<long> (std::lround (v), 0, 2))]);
            else
                params[key] = v;
        }
        const auto& presets = fxParamSpec (FxParam::Preset).choices;
        return { { "preset", std::string (presets[static_cast<std::size_t> (std::clamp<int> (f.preset, 0, 2))]) },
                 { "bypass", f.bypass }, { "params", std::move (params) } };
    }

    int choiceIndex (const FxParamSpecEntry& spec, const std::string& s, int def)
    {
        for (std::size_t i = 0; i < spec.choices.size(); ++i)
            if (! spec.choices[i].empty() && spec.choices[i] == s)
                return static_cast<int> (i);
        return def;
    }

    FxSettings fxFromJson (const Json& o)
    {
        FxSettings f;
        f.preset = static_cast<int8_t> (choiceIndex (fxParamSpec (FxParam::Preset), str (o, "preset", "iron"), 0));
        f.bypass = flag (o, "bypass", false);
        if (const Json* params = obj (o, "params"))
            for (int i = 0; i < kNumFxParams; ++i)
            {
                const auto p = static_cast<FxParam> (i);
                if (p == FxParam::Bypass || p == FxParam::Preset)
                    continue;
                const auto& spec = fxParamSpec (p);
                const std::string key (spec.name);
                float& v = f.params[static_cast<std::size_t> (i)];
                if (spec.scale == ParamScale::Toggle)
                    v = flag (*params, key.c_str(), spec.defaultValue >= 0.5f) ? 1.f : 0.f;
                else if (spec.scale == ParamScale::Choice)
                    v = static_cast<float> (choiceIndex (spec, str (*params, key.c_str(), ""), static_cast<int> (spec.defaultValue)));
                else
                    v = num<float> (*params, key.c_str(), spec.defaultValue, spec.minValue, spec.maxValue);
            }
        return f;
    }

    // ---- insert slots and the guitar strip (v4)

    Json insertsToJson (const InsertSlots& slots)
    {
        Json a = Json::array();
        for (const auto& sl : slots)
        {
            Json o = Json::object();   // empty slot is {}
            if (! sl.empty())
            {
                o["id"] = sl.identifier;
                o["name"] = sl.name;
                o["state"] = sl.stateBase64;
                o["bypass"] = sl.bypass;
            }
            a.push_back (std::move (o));
        }
        return a;
    }

    InsertSlots insertsFromJson (const Json& o, const char* key)
    {
        InsertSlots slots {};
        if (const Json* a = arr (o, key))
        {
            std::size_t i = 0;
            for (const auto& e : *a)
            {
                if (i >= slots.size())
                    break;
                auto& sl = slots[i++];
                if (! e.is_object())
                    continue;
                sl.identifier = str (e, "id", "");
                if (sl.identifier.empty())
                    continue;
                sl.name = str (e, "name", "");
                sl.stateBase64 = str (e, "state", "");
                sl.bypass = flag (e, "bypass", false);
            }
        }
        return slots;
    }

    float guitarNum (const Json& o, GuitarParam p)
    {
        const auto& spec = kGuitarParamSpecs[static_cast<std::size_t> (p)];
        return num<float> (o, std::string (spec.name).c_str(), spec.defaultValue, spec.minValue, spec.maxValue);
    }

    Json guitarToJson (const GuitarSettings& g)
    {
        return { { "shakal", g.shakal }, { "fx", fxToJson (g.fx) }, { "gateDb", g.gateDb }, { "hpfHz", g.hpfHz },
                 { "loopGainDb", g.loopGainDb }, { "loopTap", g.loopTap == LoopTap::Di ? "di" : "amp" },
                 { "inserts", insertsToJson (g.inserts) } };
    }

    GuitarSettings guitarFromJson (const Json& o)
    {
        GuitarSettings g;
        g.shakal = guitarNum (o, GuitarParam::Shakal);
        if (const Json* fx = obj (o, "fx"))
            g.fx = fxFromJson (*fx);
        g.gateDb = guitarNum (o, GuitarParam::GateDb);
        g.hpfHz = guitarNum (o, GuitarParam::HpfHz);
        g.loopGainDb = guitarNum (o, GuitarParam::LoopGainDb);
        g.loopTap = str (o, "loopTap", "amp") == "di" ? LoopTap::Di : LoopTap::Amp;
        g.inserts = insertsFromJson (o, "inserts");
        return g;
    }

    // ---- writing

    Json stepToJson (const Step& s, int index)
    {
        Json j;
        j["i"] = index;
        if (! s.active)
            j["a"] = false;   // erased step that still carries its labels (see DEVIATIONS.md)
        j["v"] = s.velocity;
        j["r"] = s.ratchet;
        j["rvr"] = s.ratchetVelRamp;
        j["rpr"] = s.ratchetPitchRamp;
        j["p"] = s.pitch;
        j["s"] = s.slice;
        j["rev"] = s.reverse;
        j["prob"] = s.probability;
        j["cond"] = condName (s.condition);
        j["nudge"] = s.nudge;

        Json pl = Json::array();
        for (const auto& l : s.plocks)
            if (l.param < kNumTrackParams)
                pl.push_back ({ { "id", std::string (kTrackParamSpecs[l.param].name) }, { "v", l.value } });
        j["pl"] = std::move (pl);
        return j;
    }

    Step stepFromJson (const Json& j)
    {
        Step s;
        s.active = flag (j, "a", true);
        s.velocity = num<uint8_t> (j, "v", 100, 1, 127);
        s.ratchet = num<uint8_t> (j, "r", 1, 1, kMaxRatchet);
        s.ratchetVelRamp = num<int8_t> (j, "rvr", 0, -100, 100);
        s.ratchetPitchRamp = num<int8_t> (j, "rpr", 0, -24, 24);
        s.pitch = num<int8_t> (j, "p", 0, -24, 24);
        s.slice = num<int8_t> (j, "s", -1, -1, 63);
        s.reverse = flag (j, "rev", false);
        s.probability = num<uint8_t> (j, "prob", 100, 0, 100);
        s.condition = condFromName (str (j, "cond", "always"));
        s.nudge = num<int8_t> (j, "nudge", 0, -kMaxNudgePercent, kMaxNudgePercent);

        if (const Json* pl = arr (j, "pl"))
        {
            std::size_t n = 0;
            for (const auto& e : *pl)
            {
                if (n >= s.plocks.size() || ! e.is_object())
                    break;
                const int id = trackParamFromName (str (e, "id", ""));
                if (id < 0)
                    continue;
                const auto p = static_cast<TrackParam> (id);
                s.plocks[n].param = static_cast<uint8_t> (id);
                s.plocks[n].value = clampTrackParam (p, num<float> (e, "v", trackParamSpec (p).defaultValue, -1e9f, 1e9f));
                ++n;
            }
        }
        return s;
    }
} // namespace

std::string toJson (const Project& p)
{
    Json j;
    j["format"] = "jackal-project";
    j["version"] = kProjectFormatVersion;
    j["name"] = p.name;
    j["bpm"] = p.bpm;
    j["seed"] = p.seed;
    j["masterGainDb"] = p.masterGainDb;
    j["masterShakal"] = p.masterShakal;
    j["masterFx"] = fxToJson (p.masterFx);
    j["masterInserts"] = insertsToJson (p.masterInserts);
    j["guitar"] = guitarToJson (p.guitar);

    Json slots = Json::array();
    for (const auto& s : p.slots)
    {
        Json o = Json::object();
        if (s.used)
        {
            o["source"] = s.source;
            if (! s.path.empty())
                o["path"] = s.path;
            o["sliceMode"] = sliceName (s.sliceMode);
            o["sourceBars"] = s.sourceBars;
            if (s.sliceMode == SliceMode::Transient)
                o["boundaries"] = s.transientBoundaries;
            if (! s.origin.empty())
                o["origin"] = s.origin;
        }
        slots.push_back (std::move (o));
    }
    j["slots"] = std::move (slots);

    Json tracks = Json::array();
    for (const auto& t : p.tracks)
    {
        Json params = Json::object();
        for (std::size_t i = 0; i < kTrackParamSpecs.size(); ++i)
            params[std::string (kTrackParamSpecs[i].name)] = t.params[i];

        tracks.push_back ({ { "name", t.name }, { "slot", t.sampleSlot }, { "choke", t.chokeGroup },
                            { "fit", t.fitToTempo }, { "mute", t.mute }, { "solo", t.solo }, { "params", std::move (params) },
                            { "fx", fxToJson (t.fx) }, { "inserts", insertsToJson (t.inserts) } });
    }
    j["tracks"] = std::move (tracks);

    Json patterns = Json::array();
    for (const auto& pat : p.patterns)
    {
        Json lanes = Json::array();
        for (const auto& lane : pat.lanes)
        {
            Json steps = Json::array();
            for (int i = 0; i < kMaxSteps; ++i)
            {
                const Step& s = lane.steps[static_cast<std::size_t> (i)];
                if (s.active || ! (s == Step {}))
                    steps.push_back (stepToJson (s, i));
            }
            lanes.push_back ({ { "length", lane.length }, { "speed", speedName (lane.speed) }, { "steps", std::move (steps) } });
        }

        const int cells = std::clamp<int> (pat.bars, 1, kMaxBars) * 16;
        Json glitch = Json::array();
        for (int i = 0; i < cells; ++i)
            glitch.push_back (pat.glitch.pad[static_cast<std::size_t> (i)]);

        patterns.push_back ({ { "bars", pat.bars }, { "swing", pat.swing }, { "lanes", std::move (lanes) }, { "glitch", std::move (glitch) },
                              { "loopSlot", pat.loopSlot } });
    }
    j["patterns"] = std::move (patterns);

    j["ui"] = { { "selectedTrack", p.ui.selectedTrack }, { "currentPattern", p.ui.currentPattern },
                { "gridPage", p.ui.gridPage }, { "mutationStrength", p.ui.mutationStrength },
                { "trackPanelTab", p.ui.trackPanelTab }, { "songMode", p.ui.songMode }, { "follow", p.ui.follow } };

    Json entries = Json::array();
    for (int i = 0; i < std::min<int> (p.song.length, kMaxSongEntries); ++i)
    {
        const auto& e = p.song.entries[static_cast<std::size_t> (i)];
        entries.push_back ({ { "p", e.pattern }, { "n", e.repeats } });
    }
    j["song"] = { { "loop", p.song.loop }, { "entries", std::move (entries) } };
    const auto& x = p.exportSettings;
    j["export"] = { { "sampleRate", x.sampleRate }, { "bitDepth", x.bitDepth }, { "stems", x.stems },
                    { "tailSeconds", x.tailSeconds }, { "cycles", x.cycles } };

    return j.dump (1);
}

ParseResult fromJson (std::string_view text)
{
    ParseResult result;

    Json j = Json::parse (text.begin(), text.end(), nullptr, false);
    if (j.is_discarded() || ! j.is_object())
    {
        result.error = "project.json is not valid JSON";
        return result;
    }

    if (str (j, "format", "") != "jackal-project")
    {
        result.error = "not a Jackal Box project (format field)";
        return result;
    }

    const auto vit = j.find ("version");
    if (vit == j.end() || ! vit->is_number_integer())
    {
        result.error = "project has no version";
        return result;
    }
    const int version = vit->get<int>();
    if (version > kProjectFormatVersion)
    {
        result.error = "project version " + std::to_string (version) + " is newer than this build (" + std::to_string (kProjectFormatVersion) + ")";
        return result;
    }
    if (version < 2)
    {
        result.error = "project version " + std::to_string (version) + " is not supported";
        return result;
    }

    auto p = std::make_unique<Project>();
    p->name = str (j, "name", "Untitled");
    p->bpm = num<double> (j, "bpm", 172.0, 60.0, 300.0);
    p->seed = num<uint32_t> (j, "seed", 0x5A4C, 0u, 0xFFFFFFFFu);
    p->masterGainDb = num<float> (j, "masterGainDb", kMasterGainDefaultDb, kMasterGainSpec.minValue, kMasterGainSpec.maxValue);
    if (version >= 3)
    {
        p->masterShakal = num<float> (j, "masterShakal", 0.f, 0.f, 1.f);
        if (const Json* mf = obj (j, "masterFx"))
            p->masterFx = fxFromJson (*mf);
    }
    if (version >= 4)
    {
        p->masterInserts = insertsFromJson (j, "masterInserts");
        if (const Json* g = obj (j, "guitar"))
            p->guitar = guitarFromJson (*g);
    }

    if (const Json* slots = arr (j, "slots"))
    {
        std::size_t i = 0;
        for (const auto& o : *slots)
        {
            if (i >= p->slots.size())
                break;
            auto& s = p->slots[i++];
            if (! o.is_object() || o.find ("source") == o.end())
                continue;
            s.used = true;
            s.source = str (o, "source", "");
            s.path = str (o, "path", "");
            s.sliceMode = sliceFromName (str (o, "sliceMode", "off"));
            s.sourceBars = num<double> (o, "sourceBars", 0.0, 0.0, 64.0);
            if (const Json* b = arr (o, "boundaries"))
                for (const auto& v : *b)
                    if (v.is_number_integer() && s.transientBoundaries.size() < 65)
                        s.transientBoundaries.push_back (std::max<int64_t> (0, v.get<int64_t>()));
            if (version >= 5)
                s.origin = str (o, "origin", "");
        }
    }

    if (const Json* tracks = arr (j, "tracks"))
    {
        std::size_t i = 0;
        for (const auto& o : *tracks)
        {
            if (i >= p->tracks.size())
                break;
            auto& t = p->tracks[i++];
            if (! o.is_object())
                continue;
            t.name = str (o, "name", "").substr (0, 16);
            t.sampleSlot = num<int8_t> (o, "slot", -1, -1, kNumSampleSlots - 1);
            t.chokeGroup = num<uint8_t> (o, "choke", 0, 0, 4);
            t.fitToTempo = flag (o, "fit", false);
            t.mute = flag (o, "mute", false);
            t.solo = flag (o, "solo", false);
            if (const Json* params = obj (o, "params"))
                for (std::size_t k = 0; k < kTrackParamSpecs.size(); ++k)
                {
                    const auto& spec = kTrackParamSpecs[k];
                    t.params[k] = num<float> (*params, std::string (spec.name).c_str(), spec.defaultValue, spec.minValue, spec.maxValue);
                }
            if (version < 3)
                t.params[static_cast<std::size_t> (TrackParam::Shakal)] = 0.f;   // v2 had no SHAKAL: clean sound
            else if (const Json* fx = obj (o, "fx"))
                t.fx = fxFromJson (*fx);
            if (version >= 4)
                t.inserts = insertsFromJson (o, "inserts");
        }
    }

    if (const Json* patterns = arr (j, "patterns"))
    {
        std::size_t pi = 0;
        for (const auto& o : *patterns)
        {
            if (pi >= p->patterns.size())
                break;
            auto& pat = p->patterns[pi++];
            if (! o.is_object())
                continue;
            pat.bars = num<uint8_t> (o, "bars", 1, 1, kMaxBars);
            pat.swing = num<uint8_t> (o, "swing", 50, kMinSwing, kMaxSwing);
            if (version >= 4)
            {
                const int ls = num<int> (o, "loopSlot", -1, -1000, 1000);
                pat.loopSlot = static_cast<int8_t> ((ls >= 8 && ls < kNumSampleSlots) ? ls : -1);
            }

            if (const Json* lanes = arr (o, "lanes"))
            {
                std::size_t li = 0;
                for (const auto& lo : *lanes)
                {
                    if (li >= pat.lanes.size())
                        break;
                    auto& lane = pat.lanes[li++];
                    if (! lo.is_object())
                        continue;
                    lane.length = num<uint8_t> (lo, "length", 16, 1, kMaxSteps);
                    lane.speed = speedFromName (str (lo, "speed", "x1"));
                    if (const Json* steps = arr (lo, "steps"))
                        for (const auto& so : *steps)
                        {
                            if (! so.is_object())
                                continue;
                            const int idx = num<int> (so, "i", -1, -1, kMaxSteps - 1);
                            if (idx >= 0)
                                lane.steps[static_cast<std::size_t> (idx)] = stepFromJson (so);
                        }
                }
            }

            if (const Json* g = arr (o, "glitch"))
            {
                std::size_t gi = 0;
                for (const auto& v : *g)
                {
                    if (gi >= pat.glitch.pad.size())
                        break;
                    pat.glitch.pad[gi++] = v.is_number() ? static_cast<uint8_t> (std::clamp<int64_t> (v.get<int64_t>(), 0, kNumPads)) : uint8_t (0);
                }
            }
        }
    }

    if (const Json* ui = obj (j, "ui"))
    {
        p->ui.selectedTrack = num<int> (*ui, "selectedTrack", 0, 0, kNumTracks - 1);
        p->ui.currentPattern = num<int> (*ui, "currentPattern", 0, 0, kNumPatterns - 1);
        p->ui.gridPage = num<int> (*ui, "gridPage", 0, 0, 3);
        p->ui.mutationStrength = num<float> (*ui, "mutationStrength", 0.5f, 0.f, 1.f);
        p->ui.trackPanelTab = num<int> (*ui, "trackPanelTab", 0, 0, 5);
        if (version < 4)   // v3 tabs SRC FX STEP MST; v4 inserts VST after FX and adds GTR
            p->ui.trackPanelTab = std::array<int, 6> { 0, 1, 3, 4, 4, 4 }[static_cast<std::size_t> (std::clamp (p->ui.trackPanelTab, 0, 3))];
        if (version >= 5)
        {
            p->ui.songMode = flag (*ui, "songMode", false);
            p->ui.follow = flag (*ui, "follow", true);
        }
    }

    if (version >= 5)
    {
        if (const Json* song = obj (j, "song"))
        {
            p->song.loop = flag (*song, "loop", true);
            if (const Json* entries = arr (*song, "entries"))
                for (const auto& e : *entries)
                {
                    if (p->song.length >= kMaxSongEntries)
                        break;
                    if (! e.is_object())
                        continue;
                    const auto pit = e.find ("p");
                    if (pit == e.end() || ! pit->is_number_integer())
                        continue;
                    const int64_t pat = pit->get<int64_t>();
                    if (pat < 0 || pat >= kNumPatterns)
                        continue;   // skipped, not clamped: a foreign pattern is worse than a shorter song
                    p->song.entries[p->song.length++] = { static_cast<int8_t> (pat), num<uint8_t> (e, "n", 1, 1, kMaxRepeats) };
                }
        }
        if (const Json* x = obj (j, "export"))
        {
            auto& es = p->exportSettings;
            const int sr = num<int> (*x, "sampleRate", 0, 0, 1000000);
            es.sampleRate = (sr == 44100 || sr == 48000 || sr == 96000) ? sr : 0;
            const int bits = num<int> (*x, "bitDepth", 24, 0, 1000);
            es.bitDepth = (bits == 16 || bits == 24 || bits == 32) ? bits : 24;
            es.stems = flag (*x, "stems", true);
            es.tailSeconds = num<float> (*x, "tailSeconds", 2.f, 0.f, 8.f);
            es.cycles = num<int> (*x, "cycles", 4, 1, 16);
        }
    }

    result.ok = true;
    result.migratedFrom = version < kProjectFormatVersion ? version : 0;
    result.project = std::move (p);
    return result;
}

} // namespace jackal
