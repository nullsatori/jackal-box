#include "InsertHost.h"

namespace jackal
{

namespace
{
    std::size_t idx (int i) { return static_cast<std::size_t> (i); }
}

InsertHost::InsertHost (AudioEngine& engine, PluginHost& plugins) : engine_ (engine), plugins_ (plugins) {}

InsertHost::~InsertHost()
{
    for (auto& chain : windows_)
        for (auto& w : chain)
            w = nullptr;
    // whatever the engine still holds was handed back by releaseAll() before this point
    for (auto* p : owned_)
        delete p;
}

juce::String InsertHost::slotName (int chain, int slot)
{
    const auto n = juce::String (slot + 1);
    if (chain == kMasterChain) return "MASTER" + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + n;
    if (chain == kGuitarChain) return "GTR" + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + n;
    return "T" + juce::String (chain + 1) + juce::String (juce::CharPointer_UTF8 ("\xc2\xb7")) + n;
}

Vst3Insert* InsertHost::live (int chain, int slot) const noexcept
{
    if (chain < 0 || chain >= kNumChains || slot < 0 || slot >= kSlotsPerChain)
        return nullptr;
    return live_[idx (chain)][idx (slot)];
}

juce::String InsertHost::load (int chain, int slot, const juce::PluginDescription& d, const juce::String& stateBase64)
{
    const auto info = engine_.latency();
    const double sr = info.sampleRate > 0.0 ? info.sampleRate : 48000.0;
    const int block = info.bufferSamples > 0 ? info.bufferSamples : 512;

    juce::String error;
    auto inst = plugins_.create (d, sr, block, error);
    if (inst == nullptr)
        return error.isNotEmpty() ? error : d.name + ": could not load";
    auto insert = Vst3Insert::make (std::move (inst), error);
    if (insert == nullptr)
        return error;

    if (stateBase64.isNotEmpty())
    {
        juce::MemoryBlock mb;
        if (mb.fromBase64Encoding (stateBase64))
            insert->setState (mb);
    }
    insert->prepare (sr, block);

    remove (chain, slot);   // the old one (window first, then the engine hands it back)
    auto* raw = insert.release();
    if (! engine_.core().commands().push ({ CommandType::SetInsert, 0.f, chain | (slot << 8), raw }))
    {
        delete raw;
        return "engine busy, plugin not loaded";
    }
    owned_.insert (raw);
    live_[idx (chain)][idx (slot)] = raw;
    return {};
}

void InsertHost::remove (int chain, int slot)
{
    auto*& p = live_[idx (chain)][idx (slot)];
    if (p == nullptr)
        return;
    if (auto& w = window (chain, slot); w != nullptr)
    {
        std::erase (shown_, w.get());
        w = nullptr;   // the editor goes before the plugin
    }
    engine_.core().commands().push ({ CommandType::SetInsert, 0.f, chain | (slot << 8), nullptr });
    p = nullptr;
}

void InsertHost::removeAll()
{
    for (int c = 0; c < kNumChains; ++c)
        for (int s = 0; s < kSlotsPerChain; ++s)
            remove (c, s);
}

juce::String InsertHost::stateBase64 (int chain, int slot) const
{
    auto* p = live (chain, slot);
    return p != nullptr ? p->state().toBase64Encoding() : juce::String();
}

bool InsertHost::pollChanged()
{
    bool any = false;
    for (auto& chain : live_)
        for (auto* p : chain)
            if (p != nullptr && p->consumeChanged())
                any = true;
    return any;
}

void InsertHost::toggleEditor (int chain, int slot)
{
    auto* p = live (chain, slot);
    if (p == nullptr)
        return;
    auto& w = window (chain, slot);
    if (w == nullptr)
        w = std::make_unique<PluginWindow> (p->instance(), p->name() + "  " + slotName (chain, slot));
    else
        w->setVisible (! w->isVisible());

    std::erase (shown_, w.get());
    if (w->isVisible())
    {
        shown_.push_back (w.get());
        w->toFront (true);
    }
}

bool InsertHost::editorOpen (int chain, int slot) const
{
    const auto& w = windows_[idx (chain)][idx (slot)];
    return w != nullptr && w->isVisible();
}

bool InsertHost::closeTopEditor()
{
    while (! shown_.empty())
    {
        auto* w = shown_.back();
        shown_.pop_back();
        if (w->isVisible())
        {
            w->setVisible (false);
            return true;
        }
    }
    return false;
}

void InsertHost::closeAllEditors()
{
    shown_.clear();
    for (auto& chain : windows_)
        for (auto& w : chain)
            w = nullptr;
}

bool InsertHost::onRetired (const void* ptr)
{
    auto* p = static_cast<Vst3Insert*> (const_cast<void*> (ptr));
    const auto it = owned_.find (p);
    if (it == owned_.end())
        return false;
    owned_.erase (it);
    delete p;
    return true;
}

} // namespace jackal
