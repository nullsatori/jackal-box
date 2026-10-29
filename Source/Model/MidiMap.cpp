#include "MidiMap.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <string>

namespace jackal
{

namespace
{
    constexpr std::array<std::string_view, static_cast<std::size_t> (ActionId::Count)> kActionNames {
        "play", "stop", "togglePlay", "rec", "fill", "pattern", "songMode", "songNext", "songPrev", "loopRec", "loopDub",
        "loopUndo", "pad", "trackHit", "mute", "solo", "selectTrack", "mutate", "undo", "redo", "reroll", "capture"
    };
} // namespace

std::string_view actionName (ActionId a)
{
    const auto i = static_cast<std::size_t> (a);
    return i < kActionNames.size() ? kActionNames[i] : std::string_view {};
}

ActionId actionFromName (std::string_view n, bool& ok)
{
    for (std::size_t i = 0; i < kActionNames.size(); ++i)
        if (kActionNames[i] == n)
        {
            ok = true;
            return static_cast<ActionId> (i);
        }
    ok = false;
    return ActionId::Play;
}

int bind (MidiMap& m, const MidiBinding& b)
{
    const auto before = m.bindings.size();
    std::erase_if (m.bindings, [&b] (const MidiBinding& x) { return x.source == b.source || x.target == b.target; });
    const int removed = static_cast<int> (before - m.bindings.size());
    m.bindings.push_back (b);
    return removed;
}

void unbindTarget (MidiMap& m, const MidiTarget& t)
{
    std::erase_if (m.bindings, [&t] (const MidiBinding& x) { return x.target == t; });
}

void unbindSource (MidiMap& m, const MidiSource& s)
{
    std::erase_if (m.bindings, [&s] (const MidiBinding& x) { return x.source == s; });
}

const MidiBinding* findByTarget (const MidiMap& m, const MidiTarget& t)
{
    for (const auto& b : m.bindings)
        if (b.target == t)
            return &b;
    return nullptr;
}

float ccToValue (const ParamInfo& in, int cc)
{
    cc = std::clamp (cc, 0, 127);
    const double u = static_cast<double> (cc) / 127.0;
    const double lo = in.minValue, hi = in.maxValue;
    switch (in.scale)
    {
        case ParamScale::Linear: return static_cast<float> (lo + u * (hi - lo));
        case ParamScale::Log:
            if (lo > 0.0 && hi > 0.0)
                return static_cast<float> (lo * std::pow (hi / lo, u));
            return static_cast<float> (lo + u * (hi - lo));
        case ParamScale::Toggle: return cc >= 64 ? 1.f : 0.f;
        case ParamScale::Choice:
        {
            const int n = in.choices.empty() ? static_cast<int> (std::lround (hi - lo)) + 1 : static_cast<int> (in.choices.size());
            return static_cast<float> (lo + std::min (n - 1, cc * n / 128));
        }
    }
    return static_cast<float> (lo);
}

int valueToCc (const ParamInfo& in, float v)
{
    const double lo = in.minValue, hi = in.maxValue;
    double cc = 0.0;
    switch (in.scale)
    {
        case ParamScale::Linear:
            cc = hi > lo ? (static_cast<double> (v) - lo) / (hi - lo) * 127.0 : 0.0;
            break;
        case ParamScale::Log:
            if (lo > 0.0 && hi > lo && v > 0.f)
                cc = std::log (static_cast<double> (v) / lo) / std::log (hi / lo) * 127.0;
            else
                cc = hi > lo ? (static_cast<double> (v) - lo) / (hi - lo) * 127.0 : 0.0;
            break;
        case ParamScale::Toggle:
            return v >= 0.5f ? 127 : 0;
        case ParamScale::Choice:
        {
            const int n = in.choices.empty() ? static_cast<int> (std::lround (hi - lo)) + 1 : static_cast<int> (in.choices.size());
            const int i = std::clamp (static_cast<int> (std::lround (static_cast<double> (v) - lo)), 0, n - 1);
            return std::min (127, static_cast<int> (std::lround ((i + 0.5) * 128.0 / n)));
        }
    }
    return std::clamp (static_cast<int> (std::lround (cc)), 0, 127);
}

MidiNoteMap buildNoteMap (const MidiMap& m)
{
    MidiNoteMap out = m.defaultNotes ? kDefaultNoteMap : MidiNoteMap {};
    out.channel = m.channel;
    for (const auto& b : m.bindings)
    {
        if (b.source.type != MidiSourceType::Note || b.source.number > 127)
            continue;
        auto& slot = out.notes[b.source.number];
        if (b.target.kind == MidiTarget::Kind::Action && actionInEngine (b.target.action))
        {
            const NoteAction a = b.target.action == ActionId::TrackHit ? NoteAction::TrackHit
                               : b.target.action == ActionId::Pad      ? NoteAction::Pad
                                                                       : NoteAction::Fill;
            slot = { a, static_cast<uint8_t> (std::min<int> (b.target.index, 7)) };
        }
        else
        {
            slot = {};   // the note belongs to a button or a parameter now
        }
    }
    return out;
}

std::string targetToString (const MidiTarget& t)
{
    if (t.kind == MidiTarget::Kind::Param)
        return address (t.param);
    std::string s = "action:" + std::string (actionName (t.action));
    if (actionHasIndex (t.action))
        s += ":" + std::to_string (t.index);
    return s;
}

bool targetFromString (std::string_view s, MidiTarget& out)
{
    if (s.rfind ("action:", 0) == 0)
    {
        std::string_view rest = s.substr (7);
        int index = 0;
        if (const auto colon = rest.find (':'); colon != std::string_view::npos)
        {
            const std::string num (rest.substr (colon + 1));
            if (num.empty() || num.size() > 2 || ! std::all_of (num.begin(), num.end(), [] (char c) { return c >= '0' && c <= '9'; }))
                return false;
            index = std::stoi (num);
            rest = rest.substr (0, colon);
        }
        bool ok = false;
        const ActionId a = actionFromName (rest, ok);
        if (! ok || index > 7)
            return false;
        out = MidiTarget::ofAction (a, index);
        return true;
    }
    if (const auto id = fromAddress (s))
    {
        out = MidiTarget::ofParam (*id);
        return true;
    }
    return false;
}

std::string sourceToString (const MidiSource& s)
{
    std::string r = (s.type == MidiSourceType::Cc ? "CC " : "NOTE ") + std::to_string (s.number);
    if (s.channel != 0)
        r += " (ch " + std::to_string (s.channel) + ")";
    return r;
}

} // namespace jackal
