#include "QuickAdd.h"

namespace andor::modular::ui
{

namespace
{
    constexpr int rootWidth = 268, subWidth = 232, rowH = 30, headerH = 22, pad = 7, searchH = 32, margin = 26;

    // The order the categories are listed in (what the registry has beyond these follows).
    const char* const order[] = { "Sources", "Processors", "Controllers", "Spatial", "Effects", "Utilities" };

    juce::Colour categoryInk (const juce::String& name)
    {
        if (name == "Sources") return colour::identity (0).arc;
        if (name == "Processors") return colour::macro (colour::Macro::character);
        if (name == "Controllers") return colour::arp;
        if (name == "Spatial") return colour::macro (colour::Macro::echo);
        if (name == "Effects") return colour::macro (colour::Macro::movement);
        return colour::textMicro;
    }
}

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
    if (r.size() > 3)
        r.resize (3);
}

juce::String QuickAdd::categoryOf (const NodeDesc& d)
{
    // (the registry files effects under Processors / Spatial; the menu lists them together)
    return d.type.rfind ("fx.", 0) == 0 ? juce::String ("Effects") : juce::String (d.category);
}

juce::String QuickAdd::rowName (const NodeDesc& d)
{
    const auto type = edit::sourceTypeName (d.type);
    if (type.empty())
        return juce::String (d.name);
    return d.type == "osc.analog" ? juce::String ("SOURCE") : "SOURCE " + juce::String::fromUTF8 ("\xc2\xb7 ") + juce::String (type);
}

QuickAdd::QuickAdd (const Registry& reg) : registry (reg)
{
    setWantsKeyboardFocus (false);
    search.setFont (fonts::make (14.0f, Weight::regular, 0.01f));
    search.setTextToShowWhenEmpty ("Search modules...", colour::textMicro);
    search.setColour (juce::TextEditor::backgroundColourId, colour::buttonTop);
    search.setColour (juce::TextEditor::outlineColourId, colour::hairline);
    search.setColour (juce::TextEditor::focusedOutlineColourId, colour::accent.withAlpha (0.7f));
    search.setColour (juce::TextEditor::textColourId, colour::text);
    search.addListener (this);
    search.addKeyListener (this);
    search.setEscapeAndReturnKeysConsumed (true);
    search.setTitle ("Search modules");
    search.setIndents (9, 8);
    addAndMakeVisible (search);
    setTitle ("Add a module");
    setVisible (false);
}

QuickAdd::~QuickAdd() { search.removeKeyListener (this); }

bool QuickAdd::allowed (const NodeDesc* d) const
{
    return d != nullptr && d->type != "util.output" && (restriction.empty() || std::find (restriction.begin(), restriction.end(), d) != restriction.end());
}

std::vector<const NodeDesc*> QuickAdd::modulesIn (const juce::String& category) const
{
    std::vector<const NodeDesc*> out;
    for (const auto* d : registry.all())
        if (allowed (d) && categoryOf (*d) == category)
            out.push_back (d);
    // The one SOURCE first (an oscillator, playable at once), then the same source starting
    // as each other type, in the order its TYPE menu has them.
    const auto rank = [] (const NodeDesc* d) {
        const auto& types = edit::sourceTypes();
        for (std::size_t i = 0; i < types.size(); ++i)
            if (d->type == types[i].type)
                return static_cast<int> (i);
        return static_cast<int> (types.size());
    };
    std::stable_sort (out.begin(), out.end(), [&] (const NodeDesc* a, const NodeDesc* b) { return rank (a) < rank (b); });
    return out;
}

std::vector<juce::String> QuickAdd::categories() const
{
    std::vector<juce::String> names;
    for (const char* name : order)
        if (! modulesIn (name).empty())
            names.emplace_back (name);
    for (const auto* d : registry.all())
        if (allowed (d) && std::find (names.begin(), names.end(), categoryOf (*d)) == names.end())
            names.push_back (categoryOf (*d));
    return names;
}

void QuickAdd::open (juce::Point<int> p, const juce::String& title, std::vector<const NodeDesc*> only)
{
    heading = title;
    restriction = std::move (only);
    anchor = p;
    if (auto* parent = getParentComponent())
        setBounds (parent->getLocalBounds());   // (the whole instrument: a click outside the cards closes it)
    search.clear();
    searching = false;
    inSub = false;
    subOwner = -1;
    subRows.clear();
    rebuild();
    setVisible (true);
    toFront (true);
    search.grabKeyboardFocus();
}

void QuickAdd::close()
{
    if (! isVisible())
        return;
    stopTimer();
    setVisible (false);
    if (onClose)
        onClose();
}

bool QuickAdd::closeSubmenu()
{
    if (subOwner < 0)
        return false;
    subOwner = -1;
    inSub = false;
    subRows.clear();
    layout();
    repaint();
    return true;
}

void QuickAdd::setQuery (const juce::String& q)
{
    search.setText (q, false);
    searching = q.trim().isNotEmpty();
    rebuild();
}

void QuickAdd::rebuild()
{
    rootRows.clear();
    subRows.clear();
    subOwner = -1;
    inSub = false;
    const auto q = search.getText().trim().toStdString();
    searching = ! q.empty();
    if (searching)
    {
        auto found = registry.search (q, 40);
        // "so", "source", "sources": the one SOURCE leads (a module named RHYTHMIC SOURCE would)
        if (const auto typed = juce::String (q).toLowerCase(); typed.length() >= 2 && juce::String ("sources").startsWith (typed))
            if (const auto* one = registry.find ("osc.analog"); one != nullptr)
            {
                found.erase (std::remove (found.begin(), found.end(), one), found.end());
                found.insert (found.begin(), one);
            }
        for (const auto* d : found)
            if (allowed (d) && rootRows.size() < 8)
                rootRows.push_back ({ Row::Kind::module, d, {} });
    }
    else
    {
        bool header = false;
        for (const auto& t : recents())
            if (const auto* d = registry.find (t); allowed (d))
            {
                if (! header)
                    rootRows.push_back ({ Row::Kind::header, nullptr, "RECENT" });
                header = true;
                rootRows.push_back ({ Row::Kind::module, d, {} });
            }
        if (header)
            rootRows.push_back ({ Row::Kind::header, nullptr, "ALL" });
        for (const auto& name : categories())
            rootRows.push_back ({ Row::Kind::category, nullptr, name });
    }
    rootSelected = -1;
    for (int i = 0; i < static_cast<int> (rootRows.size()) && rootSelected < 0; ++i)
        if (rootRows[static_cast<std::size_t> (i)].kind != Row::Kind::header)
            rootSelected = i;
    layout();
    repaint();
}

void QuickAdd::layout()
{
    auto height = [] (const std::vector<Row>& rows) {
        int h = 0;
        for (const auto& r : rows)
            h += r.kind == Row::Kind::header ? headerH : rowH;
        return h;
    };
    const int top = pad + (heading.isNotEmpty() ? 20 : 0) + searchH + 6;
    const int rootH = top + std::max (rowH, height (rootRows)) + pad;
    const auto bounds = getLocalBounds().reduced (margin - 14);
    root = juce::Rectangle<int> (anchor.x, anchor.y, rootWidth, rootH).constrainedWithin (bounds);
    search.setBounds (root.getX() + pad + 1, root.getY() + pad + (heading.isNotEmpty() ? 20 : 0), root.getWidth() - 2 * pad - 2, searchH);
    sub = {};
    if (subOwner >= 0 && ! subRows.empty())
    {
        int y = root.getY() + top;
        for (int i = 0; i < subOwner; ++i)
            y += rootRows[static_cast<std::size_t> (i)].kind == Row::Kind::header ? headerH : rowH;
        const int subH = pad + height (subRows) + pad;
        // beside the root: to its right, or to its left where the instrument ends
        int x = root.getRight() + 3;
        if (x + subWidth > bounds.getRight())
            x = root.getX() - 3 - subWidth;
        sub = juce::Rectangle<int> (x, y - pad, subWidth, subH).constrainedWithin (bounds);
    }
}

void QuickAdd::resized() { layout(); }

void QuickAdd::showSubmenu (int rootRow)
{
    if (rootRow < 0 || rootRow >= static_cast<int> (rootRows.size()) || rootRows[static_cast<std::size_t> (rootRow)].kind != Row::Kind::category)
        return;
    if (subOwner == rootRow)
        return;
    subOwner = rootRow;
    subRows.clear();
    for (const auto* d : modulesIn (rootRows[static_cast<std::size_t> (rootRow)].text))
        subRows.push_back ({ Row::Kind::module, d, {} });
    subSelected = subRows.empty() ? -1 : 0;
    layout();
    repaint();
}

void QuickAdd::openCategory (const juce::String& name)
{
    for (int i = 0; i < static_cast<int> (rootRows.size()); ++i)
        if (rootRows[static_cast<std::size_t> (i)].kind == Row::Kind::category && rootRows[static_cast<std::size_t> (i)].text == name)
        {
            rootSelected = i;
            showSubmenu (i);
            inSub = true;
        }
}

juce::String QuickAdd::openCategoryName() const
{
    return subOwner >= 0 && subOwner < static_cast<int> (rootRows.size()) ? rootRows[static_cast<std::size_t> (subOwner)].text : juce::String();
}

std::vector<std::string> QuickAdd::visibleTypes() const
{
    std::vector<std::string> out;
    for (const auto& r : ! searching && subOwner >= 0 ? subRows : rootRows)
        if (r.kind == Row::Kind::module && r.desc != nullptr)
            out.push_back (r.desc->type);
    return out;
}

//==============================================================================
int QuickAdd::rowAt (const std::vector<Row>& rows, juce::Rectangle<int> card, int top, juce::Point<int> p) const
{
    if (! card.contains (p))
        return -1;
    int y = card.getY() + top;
    for (int i = 0; i < static_cast<int> (rows.size()); ++i)
    {
        const int h = rows[static_cast<std::size_t> (i)].kind == Row::Kind::header ? headerH : rowH;
        if (p.y >= y && p.y < y + h)
            return rows[static_cast<std::size_t> (i)].kind == Row::Kind::header ? -1 : i;
        y += h;
    }
    return -1;
}

void QuickAdd::paintRows (juce::Graphics& g, const std::vector<Row>& rows, juce::Rectangle<int> card, int top, int selected, bool isRoot)
{
    int y = card.getY() + top;
    for (int i = 0; i < static_cast<int> (rows.size()); ++i)
    {
        const auto& row = rows[static_cast<std::size_t> (i)];
        if (row.kind == Row::Kind::header)
        {
            g.setColour (colour::textMicro.darker (0.15f));
            g.setFont (fonts::make (9.5f, Weight::medium, 0.14f));
            g.drawText (row.text, card.getX() + pad + 6, y + 5, card.getWidth() - 2 * pad - 12, headerH - 6, juce::Justification::centredLeft, false);
            y += headerH;
            continue;
        }
        const auto r = juce::Rectangle<int> (card.getX() + pad, y, card.getWidth() - 2 * pad, rowH);
        const bool lit = i == selected && (isRoot ? ! inSub || row.kind == Row::Kind::category : inSub);
        const bool owner = isRoot && i == subOwner;
        if (lit || owner)
        {
            g.setColour (lit ? palette::recessed : palette::recessed.withAlpha (0.55f));
            g.fillRoundedRectangle (r.toFloat().reduced (0.0f, 1.0f), 6.0f);
        }
        const float cy = static_cast<float> (r.getCentreY());
        if (row.kind == Row::Kind::category)
        {
            g.setColour (categoryInk (row.text));
            g.fillEllipse (juce::Rectangle<float> (7.0f, 7.0f).withCentre ({ static_cast<float> (r.getX()) + 12.0f, cy }));
            g.setColour (colour::text);
            g.setFont (fonts::make (13.5f, Weight::medium, 0.03f));
            g.drawText (row.text, r.withTrimmedLeft (26).withTrimmedRight (22), juce::Justification::centredLeft, false);
            icon::draw (g, icon::Kind::chevron, juce::Rectangle<float> (14.0f, 14.0f).withCentre ({ static_cast<float> (r.getRight()) - 12.0f, cy }), colour::textSecondary, 1.3f);
        }
        else if (row.desc != nullptr)
        {
            const auto* d = row.desc;
            // a chip of the face it will wear on the board
            const auto look = lookFor (d->type, d->role == NodeRole::source ? 0 : -1);
            const auto chip = juce::Rectangle<float> (14.0f, 10.0f).withCentre ({ static_cast<float> (r.getX()) + 13.0f, cy });
            g.setColour (look.face == ModuleLook::Face::enamel && look.id != nullptr ? look.id->enamel : (look.face == ModuleLook::Face::terminal ? board::terminalFace : colour::cardBottom));
            g.fillRoundedRectangle (chip, 2.5f);
            g.setColour ((look.face == ModuleLook::Face::enamel ? look.edge : colour::knobRim).withAlpha (0.9f));
            g.drawRoundedRectangle (chip, 2.5f, 0.8f);
            g.setColour (d->available ? colour::text : colour::textMicro);
            g.setFont (fonts::make (13.0f, Weight::regular, 0.03f));
            const bool tagged = (isRoot && searching) || d->cost.heavy || ! d->available;
            g.drawText (rowName (*d), r.withTrimmedLeft (28).withTrimmedRight (tagged ? 78 : 8), juce::Justification::centredLeft, true);
            if (tagged)
            {
                // one quiet word at the end: where it lives (search), or what to know
                g.setFont (fonts::make (9.0f, Weight::medium, 0.1f));
                g.setColour (! d->available ? colour::textMicro : (d->cost.heavy && ! (isRoot && searching) ? colour::arp.darker (0.25f) : colour::textMicro));
                g.drawText (! d->available ? juce::String ("SOON") : ((isRoot && searching) ? categoryOf (*d).toUpperCase() : juce::String ("CPU")),
                            r.withTrimmedRight (8), juce::Justification::centredRight, false);
            }
        }
        y += rowH;
    }
}

void QuickAdd::paint (juce::Graphics& g)
{
    paintPopover (g, root.toFloat(), 11.0f);
    const int top = pad + (heading.isNotEmpty() ? 20 : 0) + searchH + 6;
    if (heading.isNotEmpty())
    {
        g.setColour (colour::textSecondary);
        g.setFont (fonts::make (10.0f, Weight::medium, 0.12f));
        g.drawText (heading.toUpperCase(), root.getX() + pad + 6, root.getY() + pad, root.getWidth() - 2 * pad - 12, 18, juce::Justification::centredLeft, true);
    }
    if (rootRows.empty())
    {
        g.setColour (colour::textMicro);
        g.setFont (type::micro (12.5f));
        g.drawText (searching ? "Nothing by that name" : "Nothing fits here", root.withTrimmedTop (top).reduced (pad + 6, 0).withHeight (rowH), juce::Justification::centredLeft, false);
    }
    paintRows (g, rootRows, root, top, rootSelected, true);
    if (! sub.isEmpty())
    {
        paintPopover (g, sub.toFloat(), 11.0f);
        paintRows (g, subRows, sub, pad, subSelected, false);
    }
}

void QuickAdd::mouseMove (const juce::MouseEvent& e)
{
    const int top = pad + (heading.isNotEmpty() ? 20 : 0) + searchH + 6;
    const auto p = e.getPosition();
    if (! sub.isEmpty() && sub.contains (p))
    {
        stopTimer();
        const int row = rowAt (subRows, sub, pad, p);
        if (row >= 0 && (row != subSelected || ! inSub))
        {
            subSelected = row;
            inSub = true;
            repaint();
        }
        return;
    }
    const int row = rowAt (rootRows, root, top, p);
    if (row < 0)
        return;
    if (row != rootSelected || inSub)
    {
        rootSelected = row;
        inSub = false;
        repaint();
    }
    // A category opens after the pointer rests on it a moment: crossing other rows on the way
    // to an open submenu changes nothing.
    if (rootRows[static_cast<std::size_t> (row)].kind == Row::Kind::category && row != subOwner)
    {
        pendingHover = row;
        startTimer (subOwner < 0 ? 90 : 170);
    }
    else
        stopTimer();
}

void QuickAdd::mouseExit (const juce::MouseEvent&) { stopTimer(); }

juce::String QuickAdd::getTooltip()
{
    const NodeDesc* d = nullptr;
    if (inSub && subSelected >= 0 && subSelected < static_cast<int> (subRows.size()))
        d = subRows[static_cast<std::size_t> (subSelected)].desc;
    else if (rootSelected >= 0 && rootSelected < static_cast<int> (rootRows.size()))
        d = rootRows[static_cast<std::size_t> (rootSelected)].desc;
    if (d == nullptr)
        return {};
    return d->available ? juce::String (d->description) + (d->cost.heavy ? "\nHeavy on the CPU." : "") : "Not available: " + juce::String (d->unavailableReason);
}

void QuickAdd::timerCallback()
{
    stopTimer();
    if (pendingHover == rootSelected && ! inSub)
        showSubmenu (pendingHover);
}

void QuickAdd::mouseDown (const juce::MouseEvent& e)
{
    if (! root.contains (e.getPosition()) && ! sub.contains (e.getPosition()))
        close();   // outside: closed, nothing placed
}

void QuickAdd::mouseUp (const juce::MouseEvent& e)
{
    if (! isVisible())
        return;
    const int top = pad + (heading.isNotEmpty() ? 20 : 0) + searchH + 6;
    const auto p = e.getPosition();
    if (! sub.isEmpty() && sub.contains (p))
    {
        if (const int row = rowAt (subRows, sub, pad, p); row >= 0)
            choose (subRows[static_cast<std::size_t> (row)].desc);
        return;
    }
    if (const int row = rowAt (rootRows, root, top, p); row >= 0)
    {
        if (rootRows[static_cast<std::size_t> (row)].kind == Row::Kind::category)
        {
            stopTimer();
            showSubmenu (row);
        }
        else
            choose (rootRows[static_cast<std::size_t> (row)].desc);
    }
}

void QuickAdd::choose (const NodeDesc* d)
{
    if (d == nullptr || ! d->available || ! isVisible())
        return;
    remember (d->type);
    stopTimer();
    setVisible (false);
    if (onChoose)
        onChoose (d->type);   // (exactly once: the menu is already gone)
}

void QuickAdd::chooseSelected()
{
    if (! searching && subOwner >= 0 && subSelected >= 0 && subSelected < static_cast<int> (subRows.size()) && (inSub || rootSelected == subOwner))
    {
        choose (subRows[static_cast<std::size_t> (subSelected)].desc);
        return;
    }
    if (rootSelected < 0 || rootSelected >= static_cast<int> (rootRows.size()))
        return;
    const auto& row = rootRows[static_cast<std::size_t> (rootSelected)];
    if (row.kind == Row::Kind::category)
    {
        showSubmenu (rootSelected);
        inSub = true;
        repaint();
    }
    else
        choose (row.desc);
}

void QuickAdd::move (int delta)
{
    auto step = [delta] (const std::vector<Row>& rows, int from) {
        int i = from;
        for (int guard = 0; guard < static_cast<int> (rows.size()); ++guard)
        {
            i = juce::jlimit (0, std::max (0, static_cast<int> (rows.size()) - 1), i + delta);
            if (rows.empty() || rows[static_cast<std::size_t> (i)].kind != Row::Kind::header || i == 0 || i == static_cast<int> (rows.size()) - 1)
                break;
        }
        return rows.empty() || rows[static_cast<std::size_t> (i)].kind == Row::Kind::header ? from : i;
    };
    if (inSub)
        subSelected = step (subRows, subSelected);
    else
    {
        rootSelected = step (rootRows, rootSelected);
        if (subOwner >= 0 && rootSelected != subOwner)
            closeSubmenu();
    }
    repaint();
}

void QuickAdd::textEditorTextChanged (juce::TextEditor&) { rebuild(); }
void QuickAdd::textEditorReturnKeyPressed (juce::TextEditor&) { chooseSelected(); }

void QuickAdd::textEditorEscapeKeyPressed (juce::TextEditor&)
{
    if (onEscape)
        onEscape();
    else if (! closeSubmenu())
        close();
}

bool QuickAdd::pressKey (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::returnKey)
    {
        chooseSelected();
        return true;
    }
    if (k == juce::KeyPress::escapeKey)
    {
        textEditorEscapeKeyPressed (search);
        return true;
    }
    return keyPressed (k, &search);
}

bool QuickAdd::keyPressed (const juce::KeyPress& k, juce::Component*)
{
    if (k == juce::KeyPress::downKey || k == juce::KeyPress::upKey)
    {
        move (k == juce::KeyPress::downKey ? 1 : -1);
        return true;
    }
    if (k == juce::KeyPress::rightKey && ! searching && ! inSub && rootSelected >= 0 && rootRows[static_cast<std::size_t> (rootSelected)].kind == Row::Kind::category)
    {
        showSubmenu (rootSelected);
        inSub = true;
        repaint();
        return true;
    }
    if (k == juce::KeyPress::leftKey && inSub)
    {
        closeSubmenu();
        return true;
    }
    return false;
}

} // namespace andor::modular::ui
