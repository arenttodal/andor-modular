#pragma once

#include "Canvas.h"
#include "Frame.h"
#include "PlayView.h"
#include "PluginProcessor.h"
#include "QuickAdd.h"
#include "Rack.h"

namespace andor::modular
{

/**
    ANDOR Modular's window: the ANDOR instrument (andor-design-system/guide), laid out once on
    the family's 1448 x 1086 canvas and scaled to the window as one piece. The housing, the
    identity and the preset bar, VOLUME; the band where OSP and Analog keep their sources is
    the board: PATCH (ANDOR Analog's routing board, grown: every module of the patch across its
    whole width, one module's rack opening along its foot) or PLAY (ANDOR Analog's front panel: one card for each sound source,
    over the family's macro panel: eight macros with their ivory knobs, the scenes); then the
    keyboard row with PITCH, the voices card, Evolve, and the status line.
*/
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
    ui::Rack& rackView() noexcept { return rack; }
    void openQuickAdd (const ui::QuickAddContext& ctx, juce::Point<int> screen);
    /** Opens a module's rack along the board's foot (another one open: swapped). "" closes. */
    void openRack (const std::string& id);
    void showEvolve (bool show);
    bool isEvolveShown() const noexcept { return evolvePanel != nullptr; }
    /** Escape, decided in one place: closes exactly one thing, the topmost, in this order:
        a drag under way (cable, selection box, move), Quick Add's submenu, Quick Add, EVOLVE,
        the rack, the group being edited, the selection. Never an undo step. False: nothing
        was open. (A parameter's popup menu is modal and takes its own Escape first.) */
    bool dismissTopmost();

private:
    /** The instrument, in reference pixels. */
    struct Surface final : public juce::Component
    {
        explicit Surface (Editor& e) : owner (e) {}
        void paint (juce::Graphics& g) override { owner.paintSurface (g); }
        Editor& owner;
    };
    void paintSurface (juce::Graphics&);
    void layoutSurface();
    juce::Rectangle<float> boardArea() const;

    void sessionChanged (bool structural) override;
    void sessionMessage (const juce::String& text, bool canUndo, bool isError) override;
    void timerCallback() override;
    void choose (const std::string& type);
    void showPatchesMenu();
    void showMainMenu();
    void showVoicesMenu();
    void stepFactory (int delta);
    void save();
    void saveAs();
    void addModuleFromHeader();
    void loadPreferences();
    void savePreferences();
    void updateHeader();
    void updateStatus();

    ModularProcessor& processor;
    Session& session;
    ui::LookAndFeel lookAndFeel;   // (also keeps the rendered ceramic pieces alive)
    juce::TooltipWindow tooltips { this, 600 };
    Surface surface { *this };

    // header
    ui::PresetBar presetBar;
    ui::KeyButton saveKey { {}, ui::skin::KeyRole::generic };
    ui::KeyButton patchKey { {}, ui::skin::KeyRole::modifier }, playKey { {}, ui::skin::KeyRole::modifier };
    ui::VolumeFader volume;
    ui::InkButton menuButton { juce::String() }, clearSolo { "SOLO" };

    // the board
    ui::InkButton addModule { "ADD MODULE" }, arrange { "ARRANGE" }, fit { "FIT" }, undo { juce::String() }, redo { juce::String() };
    ui::Canvas canvas;
    ui::Rack rack;
    ui::PlayView play;

    // the macro panel (PLAY)
    std::vector<std::unique_ptr<ui::MacroKnob>> macros;
    ui::ScenePanel scenes;

    // the keyboard row
    ui::PitchWheel pitchWheel;
    ui::Keyboard keyboard;
    ui::VoicesCard voices;
    ui::KeyButton evolveKey { "Evolve", ui::skin::KeyRole::advanced };
    ui::StatusLine status;

    // over everything
    ui::QuickAdd quickAdd;
    std::unique_ptr<ui::EvolvePanel> evolvePanel;

    ui::QuickAddContext pending;
    bool playShown = false;
};

} // namespace andor::modular
