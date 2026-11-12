#pragma once

#include "Canvas.h"
#include "Racks.h"

namespace andor::modular::ui
{

/**
    The rack: one module opened as a full-width drawer along the foot of the PATCH board (above
    the keyboard), so the board keeps its whole width and the patch stays in sight while a
    module is edited. One at a time: opening another swaps it; X or Escape closes it.

    Left: what it is (its face in miniature, name, type, where it runs, one line of help, its
    recording or hosted plugin). Centre: every control it has in its current mode (mode keys,
    then the knobs with their scales; the step editor; its incoming modulation with depths):
    the same parameters as its tile, so macros, automation and modulation are assigned here
    exactly as there (right-click). Right: its live display, read from the running graph.
    If a module has more controls than fit, the centre scrolls sideways.
*/
class Rack final : public juce::Component, private juce::Timer
{
public:
    /** Reference px. A native rack is the family's 214 px device (rail 32, body 172, foot 10)
        with the drawer's margin; the plain rack (modules without a native one yet) is taller. */
    static constexpr int nativeHeight = 224, plainHeight = 260;
    int wantedHeight() const noexcept { return family != nullptr ? nativeHeight : plainHeight; }
    /** The family's rack component for the module shown (null: the plain rack), and its host. */
    bool isNative() const noexcept { return family != nullptr; }
    osp::plugin::rack::Module* module() const;
    RackHost* binding() const noexcept { return native != nullptr ? native->host.get() : nullptr; }

    explicit Rack (Session& s);
    ~Rack() override;
    void show (const std::string& id);   ///< a node or a group id ("" closes)
    const std::string& shown() const noexcept { return current; }
    bool isOpen() const noexcept { return ! current.empty(); }
    void rebuild();
    void refreshValues();
    void paint (juce::Graphics&) override;
    void resized() override;
    std::function<void()> onClose;   ///< X pressed

    // tests
    juce::Rectangle<int> identityArea() const noexcept { return identity; }
    juce::Rectangle<int> controlsArea() const noexcept { return viewport.getBounds(); }
    juce::Rectangle<int> displayArea() const noexcept { return display; }
    int controlCount() const;
    bool scrolls() const;   ///< more controls than fit: the centre scrolls sideways

private:
    class Content;
    void timerCallback() override;
    Session& session;
    std::string current;
    juce::Viewport viewport;
    std::unique_ptr<Content> content;
    // (the host outlives the family's component that reads it)
    std::unique_ptr<NativeRack> native;
    std::unique_ptr<osp::plugin::rack::Host> family;
    std::string shownType;
    std::vector<std::unique_ptr<juce::Component>> buttons;   ///< its recording, its plugin, save as module
    InkButton close { juce::String() };
    juce::Rectangle<int> card, identity, display;
    juce::String title, subtitle, help;
    ModuleLook look;
    bool group = false;
};

} // namespace andor::modular::ui
