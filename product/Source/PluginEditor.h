#pragma once

#include "Canvas.h"
#include "DetailPanel.h"
#include "PlayView.h"
#include "PluginProcessor.h"
#include "QuickAdd.h"

namespace andor::modular
{

/** ANDOR Modular's window: PATCH (the canvas, the detail panel and the macro strip) and PLAY
    (the instrument's front panel), one top bar, Quick Add, and messages with their undo. */
class Editor final : public juce::AudioProcessorEditor, private Session::Listener, private juce::Timer
{
public:
    explicit Editor (ModularProcessor&);
    ~Editor() override;

    void paint (juce::Graphics&) override;
    void resized() override;
    bool keyPressed (const juce::KeyPress&) override;

    // for the product tests
    void showPlay (bool play);
    bool isPlayShown() const noexcept { return playShown; }
    ui::Canvas& canvasView() noexcept { return canvas; }
    ui::QuickAdd& quickAddView() noexcept { return quickAdd; }
    ui::DetailPanel& detailView() noexcept { return detail; }
    void openQuickAdd (const ui::QuickAddContext& ctx, juce::Point<int> screen);
    void openDetail (const std::string& id);

private:
    void sessionChanged (bool structural) override;
    void sessionMessage (const juce::String& text, bool canUndo, bool isError) override;
    void timerCallback() override;
    void choose (const std::string& type);
    void showPatchesMenu();
    void saveAs();
    void updateBar();

    ModularProcessor& processor;
    Session& session;
    ui::LookAndFeel lookAndFeel;
    juce::TooltipWindow tooltips { this, 600 };
    ui::Canvas canvas;
    ui::DetailPanel detail;
    ui::PlayView play;
    ui::QuickAdd quickAdd;
    ui::Toast toast;
    std::vector<std::unique_ptr<ui::MacroKnob>> strip;

    juce::Label name;
    ui::BarButton patches { "PATCHES", "New, open, save, the factory patches, modules (Cmd/Ctrl + O / S)" };
    ui::BarButton undo { "UNDO", "Cmd/Ctrl + Z" }, redo { "REDO", "Shift + Cmd/Ctrl + Z" };
    ui::BarButton arrange { "ARRANGE", "Lay the patch out left to right (signal flow)" }, fit { "FIT", "Show everything (F)" };
    ui::BarButton patchTab { "PATCH", "The modules and their cables (Tab)" }, playTab { "PLAY", "The instrument's front panel (Tab)" };
    ui::BarButton evolve { "EVOLVE", "A seeded variation (Cmd/Ctrl + E)" };
    juce::ComboBox voices;
    juce::Rectangle<int> bar, status, stripArea;
    ui::QuickAddContext pending;
    bool playShown = false, detailShown = false;
};

} // namespace andor::modular
