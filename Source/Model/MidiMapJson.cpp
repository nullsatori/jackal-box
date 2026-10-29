#include "MidiMapJson.h"

#include <nlohmann/json.hpp>

#include <algorithm>

namespace jackal
{

using Json = nlohmann::ordered_json;

std::string toJson (const MidiMap& m)
{
    Json j;
    j["format"] = "jackal-midimap";
    j["version"] = kMidiMapFormatVersion;
    j["channel"] = m.channel;
    j["defaultNotes"] = m.defaultNotes;
    Json bindings = Json::array();
    for (const auto& b : m.bindings)
    {
        Json src = { { "type", b.source.type == MidiSourceType::Cc ? "cc" : "note" }, { "ch", b.source.channel }, { "num", b.source.number } };
        Json dst;
        if (b.target.kind == MidiTarget::Kind::Param)
            dst = { { "param", address (b.target.param) } };
        else
        {
            dst = { { "action", std::string (actionName (b.target.action)) } };
            if (actionHasIndex (b.target.action))
                dst["index"] = b.target.index;
        }
        bindings.push_back ({ { "src", std::move (src) }, { "dst", std::move (dst) } });
    }
    j["bindings"] = std::move (bindings);
    return j.dump (1);
}

MidiMapParseResult midiMapFromJson (std::string_view text)
{
    MidiMapParseResult r;
    Json j = Json::parse (text.begin(), text.end(), nullptr, false);
    if (j.is_discarded() || ! j.is_object())
    {
        r.error = "midimap.json is not valid JSON";
        return r;
    }
    const auto f = j.find ("format");
    if (f == j.end() || ! f->is_string() || f->get<std::string>() != "jackal-midimap")
    {
        r.error = "not a Jackal Box MIDI map (format field)";
        return r;
    }
    const auto v = j.find ("version");
    if (v == j.end() || ! v->is_number_integer() || v->get<int>() > kMidiMapFormatVersion || v->get<int>() < 1)
    {
        r.error = "unsupported MIDI map version";
        return r;
    }

    auto intOf = [] (const Json& o, const char* key, int def, int lo, int hi) {
        const auto it = o.find (key);
        if (it == o.end() || ! it->is_number())
            return def;
        return std::clamp (static_cast<int> (it->get<double>()), lo, hi);
    };

    r.map.channel = static_cast<uint8_t> (intOf (j, "channel", 0, 0, 16));
    if (const auto d = j.find ("defaultNotes"); d != j.end() && d->is_boolean())
        r.map.defaultNotes = d->get<bool>();

    if (const auto b = j.find ("bindings"); b != j.end() && b->is_array())
        for (const auto& o : *b)
        {
            if (! o.is_object())
            {
                ++r.skipped;
                continue;
            }
            const auto src = o.find ("src");
            const auto dst = o.find ("dst");
            if (src == o.end() || dst == o.end() || ! src->is_object() || ! dst->is_object())
            {
                ++r.skipped;
                continue;
            }
            MidiBinding mb;
            const auto type = src->find ("type");
            const std::string ts = (type != src->end() && type->is_string()) ? type->get<std::string>() : "";
            if (ts != "cc" && ts != "note")
            {
                ++r.skipped;
                continue;
            }
            mb.source.type = ts == "cc" ? MidiSourceType::Cc : MidiSourceType::Note;
            mb.source.channel = static_cast<uint8_t> (intOf (*src, "ch", 0, 0, 16));
            mb.source.number = static_cast<uint8_t> (intOf (*src, "num", 0, 0, 127));

            bool ok = false;
            if (const auto p = dst->find ("param"); p != dst->end() && p->is_string())
            {
                if (const auto id = fromAddress (p->get<std::string>()))
                {
                    mb.target = MidiTarget::ofParam (*id);
                    ok = true;
                }
            }
            else if (const auto a = dst->find ("action"); a != dst->end() && a->is_string())
            {
                const ActionId act = actionFromName (a->get<std::string>(), ok);
                if (ok)
                    mb.target = MidiTarget::ofAction (act, intOf (*dst, "index", 0, 0, 7));
            }
            if (! ok)
            {
                ++r.skipped;
                continue;
            }
            bind (r.map, mb);
        }

    r.ok = true;
    return r;
}

} // namespace jackal
