#pragma once

#include "Widgets.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <set>

namespace andor::modular::ui
{

class Canvas;

/** What Quick Add was opened on (the canvas decides what a chosen type does there). */
struct QuickAddContext
{
    enum class Kind { canvas, cable, port, selection };
    Kind kind = Kind::canvas;
    juce::Point<float> world;           ///< where the new module goes
    std::string connection;             ///< cable
    std::string node, port;             ///< port
    bool portIsOutput = true;
};

/** One module (or a collapsed group) on the canvas: a compact card with its typed ports, its
    Quick controls and a live strip of what it is doing. */
class NodeView final : public juce::Component, public juce::SettableTooltipClient
{
public:
    NodeView (Canvas& owner, std::string id, bool group);
    void rebuild();
    void refreshControls();
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;

    struct Port
    {
        std::string id, name;
        PortKind kind = PortKind::audioMono;
        bool input = true;
        juce::Point<float> at;           ///< local
        std::string innerNode;           ///< a group card's port: the member it stands for
    };
    const std::vector<Port>& ports() const noexcept { return portList; }
    int portAt (juce::Point<float> local) const;
    juce::Point<float> portPosition (const std::string& node, const std::string& port, bool input) const;
    const std::string& id() const noexcept { return nodeId; }
    bool isGroup() const noexcept { return group; }
    bool isSource() const noexcept;
    std::unique_ptr<juce::AccessibilityHandler> createAccessibilityHandler() override;

private:
    void paintTelemetry (juce::Graphics&, juce::Rectangle<float>);
    Canvas& canvas;
    std::string nodeId;
    bool group = false;
    std::vector<Port> portList;
    std::vector<std::unique_ptr<juce::Component>> controls;
    juce::Rectangle<int> telemetryArea, header, ledArea;
    int hoverPort = -1;
};

class Canvas final : public juce::Component, public juce::FileDragAndDropTarget, public juce::SettableTooltipClient, private juce::Timer
{
public:
    explicit Canvas (Session& s);
    ~Canvas() override;

    std::function<void (const QuickAddContext&, juce::Point<int> screen)> onQuickAdd;
    std::function<void (const std::string& id)> onOpenDetail;
    std::function<void()> onSelectionChanged;

    void rebuild();                     ///< modules or cables changed
    void refreshValues();               ///< values changed
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseWheelMove (const juce::MouseEvent&, const juce::MouseWheelDetails&) override;
    void mouseMagnify (const juce::MouseEvent&, float scale) override;
    bool keyPressed (const juce::KeyPress&) override;
    bool isInterestedInFileDrag (const juce::StringArray& files) override;
    void filesDropped (const juce::StringArray& files, int x, int y) override;

    // world <-> canvas
    juce::Point<float> toWorld (juce::Point<float> p) const noexcept;
    juce::Point<float> toCanvas (juce::Point<float> w) const noexcept;
    void fitAll();
    void resetZoom();
    float zoomLevel() const noexcept { return zoom; }

    const std::set<std::string>& selection() const noexcept { return selected; }
    void select (const std::string& id, bool add);
    void clearSelection();
    Session& session() noexcept { return model; }

    /** Called by NodeViews. */
    void nodePressed (NodeView&, const juce::MouseEvent&);
    void nodeDragged (NodeView&, const juce::MouseEvent&);
    void nodeReleased (NodeView&, const juce::MouseEvent&);
    void portPressed (NodeView&, int port, const juce::MouseEvent&);
    void showNodeMenu (NodeView&);
    void enterGroup (const std::string& groupId);
    void moveViewBy (juce::Point<float> deltaWorld);
    void updateNodeTransforms();

    /** The module a chosen type should follow (for Quick Add with a selection). */
    std::string primarySelection() const;

private:
    void timerCallback() override;
    void paintCables (juce::Graphics&);
    juce::Path cablePath (juce::Point<float> a, juce::Point<float> b) const;
    std::string cableAt (juce::Point<float> p) const;
    void showCableMenu (const std::string& connection, juce::Point<int> screen);
    NodeView* viewFor (const std::string& nodeOrGroup) const;
    /** Where a cable end is drawn: the node's port, its collapsed group's port, or the rail. */
    bool endPoint (const std::string& node, const std::string& port, bool input, juce::Point<float>& out) const;
    std::string visibleOwner (const std::string& node) const;
    void finishCable (juce::Point<float> at);
    void breadcrumbClick (juce::Point<float> p);

    Session& model;
    std::vector<std::unique_ptr<NodeView>> views;
    std::set<std::string> selected;
    float zoom = 1.0f;
    juce::Point<float> pan;   ///< world point at the canvas' top-left

    enum class Drag { none, pan, move, rubber, cable };
    Drag drag = Drag::none;
    juce::Point<float> dragStart, dragNow, panAtStart;
    std::map<std::string, juce::Point<float>> startPositions;
    bool moved = false;
    struct CableDrag
    {
        std::string node, port;
        bool fromOutput = true;
        PortKind kind = PortKind::audioMono;
    } cable;
    std::string hoverCable;
    juce::Rectangle<float> breadcrumbArea;
    int frame = 0;
};

} // namespace andor::modular::ui
