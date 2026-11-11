#pragma once

#include "Canvas.h"

namespace andor::modular::ui
{

/**
    Quick Add: a floating search at the cursor. Type (fuzzy: "anlg" finds ANALOG OSC), arrows to
    move, Return to add, Escape to close. Empty: recent modules, then every category. Opened on a
    port or a cable, it lists only modules that fit there.
*/
class QuickAdd final : public juce::Component, private juce::ListBoxModel, private juce::TextEditor::Listener, private juce::KeyListener
{
public:
    explicit QuickAdd (const Registry& reg);
    ~QuickAdd() override;

    /** Opens at `localPoint` of the parent; `only`: restrict to these types (empty: all). */
    void open (juce::Point<int> localPoint, const juce::String& title, std::vector<const NodeDesc*> only = {});
    void close();
    std::function<void (const std::string& type)> onChoose;
    std::function<void()> onClose;

    void paint (juce::Graphics&) override;
    void resized() override;
    static void remember (const std::string& type);

    // tests
    void setQuery (const juce::String& q);
    std::vector<std::string> visibleTypes() const;
    void chooseSelected();

private:
    struct Row
    {
        const NodeDesc* desc = nullptr;   ///< null: a header
        juce::String header;
    };
    int getNumRows() override { return static_cast<int> (rows.size()); }
    void paintListBoxItem (int row, juce::Graphics&, int width, int height, bool selected) override;
    void listBoxItemClicked (int row, const juce::MouseEvent&) override;
    void listBoxItemDoubleClicked (int row, const juce::MouseEvent&) override;
    void returnKeyPressed (int row) override;
    void textEditorTextChanged (juce::TextEditor&) override;
    void textEditorReturnKeyPressed (juce::TextEditor&) override;
    void textEditorEscapeKeyPressed (juce::TextEditor&) override;
    using juce::Component::keyPressed;
    bool keyPressed (const juce::KeyPress&, juce::Component*) override;
    void update();
    void moveSelection (int delta);

    const Registry& registry;
    juce::String heading;
    juce::TextEditor search;
    juce::ListBox list;
    std::vector<Row> rows;
    std::vector<const NodeDesc*> restriction;
    static std::vector<std::string>& recents();
};

} // namespace andor::modular::ui
