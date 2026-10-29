#pragma once

#include "PluginHost.h"
#include "PluginWindow.h"
#include "Vst3Insert.h"

#include "../AudioEngine.h"

#include "Engine/ParamIds.h"

#include <array>
#include <memory>
#include <set>

namespace jackal
{

// Live plugin instances of the ten insert chains: creation, publishing (SetInsert), editor windows,
// deletion when the engine hands them back. Message thread only.
class InsertHost
{
public:
    InsertHost (AudioEngine& engine, PluginHost& plugins);
    ~InsertHost();

    // Replaces whatever is in the slot. Empty string on success, else the error text.
    juce::String load (int chain, int slot, const juce::PluginDescription&, const juce::String& stateBase64);
    void remove (int chain, int slot);
    void removeAll();

    Vst3Insert* live (int chain, int slot) const noexcept;
    juce::String stateBase64 (int chain, int slot) const;
    bool pollChanged();   // true when any plugin reported a change since the last call

    void toggleEditor (int chain, int slot);
    bool editorOpen (int chain, int slot) const;
    bool closeTopEditor();   // Esc: hides the most recently shown editor
    void closeAllEditors();  // before the engine hands every plugin back (shutdown)

    // Retire queue (Insert kind): deletes the instance. True when it was ours.
    bool onRetired (const void* ptr);

    static juce::String slotName (int chain, int slot);   // T3·1, MASTER·2, GTR·1

private:
    using Window = std::unique_ptr<PluginWindow>;
    Window& window (int chain, int slot) { return windows_[static_cast<std::size_t> (chain)][static_cast<std::size_t> (slot)]; }

    AudioEngine& engine_;
    PluginHost& plugins_;
    std::array<std::array<Vst3Insert*, kSlotsPerChain>, kNumChains> live_ {};
    std::array<std::array<Window, kSlotsPerChain>, kNumChains> windows_ {};
    std::set<Vst3Insert*> owned_;
    std::vector<PluginWindow*> shown_;
};

} // namespace jackal
