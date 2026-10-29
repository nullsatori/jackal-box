#include "MidiDispatcher.h"

#include <algorithm>

namespace jackal
{

MidiOutcome dispatch (const MidiMap& map, const MidiEvent& e)
{
    MidiOutcome out;
    const int ch = (e.status & 0x0F) + 1;
    if (map.channel != 0 && ch != map.channel)
        return out;

    const int type = e.status & 0xF0;
    MidiSourceType st;
    bool pressed = false;
    uint8_t velocity = 0;
    if (type == 0x90 || type == 0x80)
    {
        st = MidiSourceType::Note;
        pressed = type == 0x90 && e.data2 > 0;
        velocity = pressed ? e.data2 : 0;
    }
    else if (type == 0xB0)
    {
        st = MidiSourceType::Cc;
        pressed = e.data2 >= 64;
        velocity = static_cast<uint8_t> (std::max<int> (1, e.data2));
    }
    else
    {
        return out;
    }

    const MidiBinding* hit = nullptr;
    for (const auto& b : map.bindings)
        if (b.source.type == st && b.source.number == e.data1 && (b.source.channel == 0 || b.source.channel == ch))
        {
            hit = &b;
            break;
        }
    if (hit == nullptr)
        return out;

    const auto& t = hit->target;
    if (t.kind == MidiTarget::Kind::Param)
    {
        if (st != MidiSourceType::Cc || ! isDefined (t.param))
            return out;
        out.kind = MidiOutcome::Kind::Param;
        out.param = t.param;
        out.value = ccToValue (info (t.param), e.data2);
        return out;
    }

    if (actionInEngine (t.action) && st == MidiSourceType::Note)
        return out;   // the engine already did it
    if (! actionIsMomentary (t.action) && ! pressed)
        return out;
    out.kind = MidiOutcome::Kind::Action;
    out.action = t.action;
    out.index = t.index;
    out.pressed = pressed;
    out.velocity = velocity;
    return out;
}

} // namespace jackal
