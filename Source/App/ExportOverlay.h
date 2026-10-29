#pragma once

#include "AppSettings.h"
#include "ProjectController.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>

namespace jackal
{

// Export parameters, progress and the result (contract stage 5, §9.4). 520 px on the right like the other overlays.
class ExportOverlay : public juce::Component, private juce::Timer
{
public:
    ExportOverlay (ProjectController& controller, AppSettings& settings);

    std::function<void()> onClosed;
    void open();
    void close();

    void paint (juce::Graphics&) override;
    void resized() override;

private:
    void timerCallback() override;
    void refresh();
    void write (const std::function<void (ExportSettings&)>& fn);
    juce::String songSummary() const;
    juce::String folderText() const;

    ProjectController& c_;
    AppSettings& settings_;
    RenderScope scope_ = RenderScope::Song;

    juce::Label title { {}, "EXPORT" }, scopeLabel { {}, "WHAT" }, rateLabel { {}, "RATE" }, bitsLabel { {}, "BITS" },
                tailLabel { {}, "TAIL" }, cyclesLabel { {}, "CYCLES" }, folderLabel { {}, "FOLDER" }, folderPath, stemsNote,
                songInfo, timeLine, speedLine, resultLine;
    juce::TextButton patternButton { "PATTERN" }, songButton { "SONG" }, stems { "STEMS" }, chooseFolder { juce::String (juce::CharPointer_UTF8 ("\xe2\x80\xa6")) },
                     resetFolder { "PROJECT" }, exportButton { "EXPORT" }, openFolder { "OPEN FOLDER" }, closeButton { "x" };
    juce::ComboBox rate, bits;
    juce::Slider tail { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Slider cycles { juce::Slider::LinearHorizontal, juce::Slider::TextBoxRight };
    juce::Rectangle<int> progressArea_;
    double shownFraction_ = -1.0;
    uint32_t shownVersion_ = 0;
    std::unique_ptr<juce::FileChooser> chooser_;
};

} // namespace jackal
