#pragma once

#include "Widgets.h"

namespace andor::modular::ui
{

/** The board's detail column: one consistent inspector for the selected module (or group),
    in the family's controls (knobs with their scale, mode keys, the graphite step display):
    every control, grouped;
    its recording or hosted plugin; its incoming modulation with depths; its help. Changing
    the selection updates it in place; Escape or X closes it. */
class DetailPanel final : public juce::Component
{
public:
    explicit DetailPanel (Session& s);
    ~DetailPanel() override;
    void show (const std::string& id);   ///< a node or a group id ("" clears)
    const std::string& shown() const noexcept { return current; }
    void rebuild();
    void refreshValues();
    void paint (juce::Graphics&) override;
    void resized() override;
    std::function<void()> onClose;

private:
    class Content;
    Session& session;
    std::string current;
    juce::Viewport viewport;
    std::unique_ptr<Content> content;
    InkButton close { juce::String() };
};

} // namespace andor::modular::ui
