#include "QuickAdd.h"

namespace andor::modular::ui
{

std::vector<std::string>& QuickAdd::recents()
{
    static std::vector<std::string> r;
    return r;
}

void QuickAdd::remember (const std::string& type)
{
    auto& r = recents();
    r.erase (std::remove (r.begin(), r.end(), type), r.end());
    r.insert (r.begin(), type);
    if (r.size() > 5)
        r.resize (5);
}

QuickAdd::QuickAdd (const Registry& reg) : registry (reg)
{
    setWantsKeyboardFocus (false);
    search.setFont (text (15.0f));
    search.setTextToShowWhenEmpty ("Type a sound, a process, a controller...", colour::boneFaint);
    search.addListener (this);
    search.addKeyListener (this);
    search.setEscapeAndReturnKeysConsumed (true);
    search.setTitle ("Quick Add search");
    search.setIndents (10, 8);
    addAndMakeVisible (search);
    list.setModel (this);
    list.setRowHeight (40);
    list.setColour (juce::ListBox::backgroundColourId, colour::cardHigh);
    list.setTitle ("Modules");
    addAndMakeVisible (list);
    setVisible (false);
}

QuickAdd::~QuickAdd() { search.removeKeyListener (this); }

void QuickAdd::open (juce::Point<int> p, const juce::String& title, std::vector<const NodeDesc*> only)
{
    heading = title;
    restriction = std::move (only);
    search.clear();
    update();
    const int w = 380, h = 420;
    auto* parent = getParentComponent();
    auto bounds = juce::Rectangle<int> (p.x, p.y, w, h);
    if (parent != nullptr)
        bounds = bounds.constrainedWithin (parent->getLocalBounds().reduced (8));
    setBounds (bounds);
    setVisible (true);
    toFront (true);
    search.grabKeyboardFocus();
}

void QuickAdd::close()
{
    setVisible (false);
    if (onClose)
        onClose();
}

void QuickAdd::setQuery (const juce::String& q)
{
    search.setText (q, true);
    update();
}

std::vector<std::string> QuickAdd::visibleTypes() const
{
    std::vector<std::string> out;
    for (const auto& r : rows)
        if (r.desc != nullptr)
            out.push_back (r.desc->type);
    return out;
}

void QuickAdd::update()
{
    rows.clear();
    const auto q = search.getText().trim().toStdString();
    auto allowed = [&] (const NodeDesc* d) {
        return restriction.empty() || std::find (restriction.begin(), restriction.end(), d) != restriction.end();
    };
    if (q.empty())
    {
        bool header = false;
        for (const auto& t : recents())
            if (const auto* d = registry.find (t); d != nullptr && allowed (d))
            {
                if (! header)
                {
                    rows.push_back ({ nullptr, "RECENT" });
                    header = true;
                }
                rows.push_back ({ d, {} });
            }
        std::string category;
        for (const auto* d : registry.all())
        {
            if (! allowed (d) || d->type == "util.output")
                continue;
            if (d->category != category)
            {
                category = d->category;
                rows.push_back ({ nullptr, juce::String (category).toUpperCase() });
            }
            rows.push_back ({ d, {} });
        }
    }
    else
        for (const auto* d : registry.search (q, 40))
            if (allowed (d))
                rows.push_back ({ d, {} });
    list.updateContent();
    for (int i = 0; i < static_cast<int> (rows.size()); ++i)
        if (rows[static_cast<std::size_t> (i)].desc != nullptr)
        {
            list.selectRow (i);
            list.scrollToEnsureRowIsOnscreen (i);
            break;
        }
    repaint();
}

void QuickAdd::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat();
    g.setColour (juce::Colours::black.withAlpha (0.45f));
    g.fillRoundedRectangle (r.translated (0.0f, 6.0f), 8.0f);
    g.setColour (colour::cardHigh);
    g.fillRoundedRectangle (r, 8.0f);
    g.setColour (colour::ember.withAlpha (0.7f));
    g.drawRoundedRectangle (r.reduced (0.5f), 8.0f, 1.0f);
    g.setColour (colour::boneDim);
    g.setFont (text (10.5f, 600));
    g.drawText (heading.isEmpty() ? juce::String ("ADD A MODULE") : heading.toUpperCase(), getLocalBounds().removeFromTop (24).reduced (12, 0), juce::Justification::centredLeft);
    g.setFont (text (10.0f));
    g.setColour (colour::boneFaint);
    g.drawText ("RETURN adds   ESC closes", getLocalBounds().removeFromTop (24).reduced (12, 0), juce::Justification::centredRight);
}

void QuickAdd::resized()
{
    auto r = getLocalBounds().reduced (8);
    r.removeFromTop (16);
    search.setBounds (r.removeFromTop (34));
    r.removeFromTop (6);
    list.setBounds (r);
}

void QuickAdd::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (row < 0 || row >= static_cast<int> (rows.size()))
        return;
    const auto& item = rows[static_cast<std::size_t> (row)];
    if (item.desc == nullptr)
    {
        g.setColour (colour::ember);
        g.setFont (text (10.0f, 600));
        g.drawText (item.header, 10, height - 18, width - 20, 16, juce::Justification::bottomLeft);
        return;
    }
    if (selected)
    {
        g.setColour (colour::raised);
        g.fillRoundedRectangle (2.0f, 1.0f, static_cast<float> (width - 4), static_cast<float> (height - 2), 4.0f);
        g.setColour (colour::ember);
        g.fillRect (2, 6, 2, height - 12);
    }
    const auto* d = item.desc;
    g.setColour (d->available ? colour::bone : colour::boneFaint);
    g.setFont (text (13.0f, 600));
    g.drawText (d->name, 12, 3, width - 120, 18, juce::Justification::centredLeft);
    g.setColour (colour::boneDim);
    g.setFont (text (11.0f));
    g.drawText (d->available ? juce::String (d->description) : juce::String ("NOT AVAILABLE: ") + d->unavailableReason, 12, 20, width - 24, 16,
                juce::Justification::centredLeft, true);
    g.setFont (text (9.0f, 600));
    const juce::String category (d->category);
    juce::String tag = category == "Sources" ? "SOURCE" : category == "Processors" ? "PROCESSOR" : category == "Controllers" ? "CONTROLLER"
                     : category == "Utilities" ? "UTILITY" : category.toUpperCase();
    if (d->cost.heavy)
        tag = "CPU  " + tag;
    g.setColour (d->cost.heavy ? colour::warn : colour::boneFaint);
    g.drawText (tag, width - 110, 4, 98, 16, juce::Justification::centredRight);
}

void QuickAdd::chooseSelected()
{
    const int row = list.getSelectedRow();
    if (row < 0 || row >= static_cast<int> (rows.size()) || rows[static_cast<std::size_t> (row)].desc == nullptr)
        return;
    const auto* d = rows[static_cast<std::size_t> (row)].desc;
    if (! d->available)
        return;
    remember (d->type);
    setVisible (false);
    if (onChoose)
        onChoose (d->type);
}

void QuickAdd::listBoxItemClicked (int row, const juce::MouseEvent&)
{
    list.selectRow (row);
    chooseSelected();
}

void QuickAdd::listBoxItemDoubleClicked (int row, const juce::MouseEvent&)
{
    list.selectRow (row);
    chooseSelected();
}
void QuickAdd::returnKeyPressed (int) { chooseSelected(); }
void QuickAdd::textEditorTextChanged (juce::TextEditor&) { update(); }
void QuickAdd::textEditorReturnKeyPressed (juce::TextEditor&) { chooseSelected(); }
void QuickAdd::textEditorEscapeKeyPressed (juce::TextEditor&) { close(); }

void QuickAdd::moveSelection (int delta)
{
    int row = list.getSelectedRow();
    for (int guard = 0; guard < static_cast<int> (rows.size()); ++guard)
    {
        row = juce::jlimit (0, static_cast<int> (rows.size()) - 1, row + delta);
        if (rows[static_cast<std::size_t> (row)].desc != nullptr)
            break;
        if (row == 0 || row == static_cast<int> (rows.size()) - 1)
            break;
    }
    list.selectRow (row);
    list.scrollToEnsureRowIsOnscreen (row);
}

bool QuickAdd::keyPressed (const juce::KeyPress& k, juce::Component*)
{
    if (k == juce::KeyPress::downKey)
    {
        moveSelection (1);
        return true;
    }
    if (k == juce::KeyPress::upKey)
    {
        moveSelection (-1);
        return true;
    }
    return false;
}

} // namespace andor::modular::ui
