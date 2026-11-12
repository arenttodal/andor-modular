#pragma once

#include "Canvas.h"

namespace andor::modular::ui
{

/**
    Quick Add: a compact menu at the pointer, never a catalogue. Its root lists a search line,
    up to three recent modules and the categories; a category opens its modules in a submenu
    beside it (on hover, or with the right arrow). Typing at any time turns the root into the
    best matches. Opened on a port or a cable it offers only what fits there.

    It is an overlay over the whole instrument while open (nothing is dimmed): a click outside
    its cards closes it and places nothing. Escape goes back one level (submenu, then the menu):
    Editor::dismissTopmost.

    The registry is the catalogue: categories come from NodeDesc::category (effects, "fx.*",
    listed as their own group), in a fixed order.
*/
class QuickAdd final : public juce::Component, public juce::TooltipClient, private juce::TextEditor::Listener, private juce::KeyListener, private juce::Timer
{
public:
    explicit QuickAdd (const Registry& reg);
    ~QuickAdd() override;

    /** Opens with its root's corner at `localPoint` of the parent (kept inside it);
        `only`: restrict to these types (empty: all). */
    void open (juce::Point<int> localPoint, const juce::String& title, std::vector<const NodeDesc*> only = {});
    void close();
    bool isOpen() const noexcept { return isVisible(); }
    /** Escape: closes the submenu if one is open (true), else nothing (false: the owner closes
        the menu). */
    bool closeSubmenu();
    std::function<void (const std::string& type)> onChoose;
    std::function<void()> onClose;
    /** Escape pressed inside (the owner decides what closes: one level at a time). */
    std::function<void()> onEscape;

    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
    void mouseExit (const juce::MouseEvent&) override;
    static void remember (const std::string& type);
    /** What the module under the pointer is (its one-line description; why it is unavailable). */
    juce::String getTooltip() override;

    // tests
    void setQuery (const juce::String& q);
    /** The modules offered now, in order: the search results, or the open submenu's, or (root)
        the recent ones. */
    std::vector<std::string> visibleTypes() const;
    std::vector<juce::String> categories() const;
    void openCategory (const juce::String& name);
    juce::String openCategoryName() const;
    void chooseSelected();
    /** A key as typed in the search line (the arrows, Return, Escape). */
    bool pressKey (const juce::KeyPress&);
    juce::Rectangle<int> rootBounds() const noexcept { return root; }
    juce::Rectangle<int> submenuBounds() const noexcept { return sub; }
    static juce::String categoryOf (const NodeDesc&);

private:
    struct Row
    {
        enum class Kind { header, module, category };
        Kind kind = Kind::module;
        const NodeDesc* desc = nullptr;
        juce::String text;
    };
    void textEditorTextChanged (juce::TextEditor&) override;
    void textEditorReturnKeyPressed (juce::TextEditor&) override;
    void textEditorEscapeKeyPressed (juce::TextEditor&) override;
    using juce::Component::keyPressed;
    bool keyPressed (const juce::KeyPress&, juce::Component*) override;
    void timerCallback() override;
    void rebuild();
    void layout();
    void showSubmenu (int rootRow);
    void choose (const NodeDesc*);
    bool allowed (const NodeDesc*) const;
    std::vector<const NodeDesc*> modulesIn (const juce::String& category) const;
    int rowAt (const std::vector<Row>& rows, juce::Rectangle<int> card, int top, juce::Point<int> p) const;
    void paintRows (juce::Graphics&, const std::vector<Row>& rows, juce::Rectangle<int> card, int top, int selected, bool isRoot);
    void move (int delta);

    const Registry& registry;
    juce::String heading;
    juce::TextEditor search;
    std::vector<Row> rootRows, subRows;
    std::vector<const NodeDesc*> restriction;
    juce::Rectangle<int> root, sub;
    juce::Point<int> anchor;
    int rootSelected = -1, subSelected = -1, subOwner = -1, pendingHover = -1;
    bool inSub = false, searching = false;
    static std::vector<std::string>& recents();
};

} // namespace andor::modular::ui
