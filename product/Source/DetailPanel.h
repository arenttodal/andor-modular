#pragma once

#include "Widgets.h"

namespace andor::modular::ui
{

/** One consistent inspector for the selected module (or group): every control, grouped;
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
    juce::TextButton close { "X" };
};

} // namespace andor::modular::ui
