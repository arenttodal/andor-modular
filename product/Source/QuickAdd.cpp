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
    search.setFont (fonts::make (15.5f, Weight::regular, 0.01f));
    search.setTextToShowWhenEmpty ("Type a sound, a process, a controller...", colour::textMicro);
    search.setColour (juce::TextEditor::backgroundColourId, colour::buttonTop);
    search.setColour (juce::TextEditor::outlineColourId, colour::hairline);
    search.setColour (juce::TextEditor::focusedOutlineColourId, colour::accent.withAlpha (0.75f));
    search.setColour (juce::TextEditor::textColourId, colour::text);
    search.addListener (this);
    search.addKeyListener (this);
    search.setEscapeAndReturnKeysConsumed (true);
    search.setTitle ("Quick Add search");
    search.setIndents (11, 9);
    addAndMakeVisible (search);
    list.setModel (this);
    list.setRowHeight (44);
    list.setColour (juce::ListBox::backgroundColourId, juce::Colours::transparentBlack);
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
    // The card, with room around it for its shadow.
    auto bounds = juce::Rectangle<int> (p.x - margin, p.y - margin, cardWidth + 2 * margin, cardHeight + 2 * margin);
    if (auto* parent = getParentComponent())
        bounds = bounds.constrainedWithin (parent->getLocalBounds());
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
    const auto c = getLocalBounds().reduced (margin);
    paintPopover (g, c.toFloat());
    const auto top = c.withHeight (34).reduced (18, 0).translated (0, 4);
    g.setColour (colour::textSecondary);
    g.setFont (type::popupTitle (12.5f));
    g.drawText (heading.isEmpty() ? juce::String ("ADD A MODULE") : heading.toUpperCase(), top, juce::Justification::centredLeft, false);
    g.setFont (fonts::make (10.5f, Weight::regular, 0.08f));
    g.setColour (colour::textMicro);
    g.drawText ("RETURN ADDS    ESC CLOSES", top, juce::Justification::centredRight, false);
}

void QuickAdd::resized()
{
    auto r = getLocalBounds().reduced (margin).reduced (12, 0);
    r.removeFromTop (38);
    search.setBounds (r.removeFromTop (38).reduced (4, 0));
    r.removeFromTop (8);
    list.setBounds (r.withTrimmedBottom (10));
}

void QuickAdd::paintListBoxItem (int row, juce::Graphics& g, int width, int height, bool selected)
{
    if (row < 0 || row >= static_cast<int> (rows.size()))
        return;
    const auto& item = rows[static_cast<std::size_t> (row)];
    if (item.desc == nullptr)
    {
        // A section: small, tracked, a rule to the right of it.
        const auto font = fonts::make (10.5f, Weight::medium, 0.14f);
        g.setColour (colour::textMicro.darker (0.2f));
        g.setFont (font);
        g.drawText (item.header, 8, height - 20, width - 16, 16, juce::Justification::centredLeft, false);
        const float textW = juce::GlyphArrangement::getStringWidth (font, item.header);
        g.setColour (colour::hairline);
        g.fillRect (juce::Rectangle<float> (18.0f + textW, static_cast<float> (height) - 12.0f, static_cast<float> (width) - textW - 30.0f, 1.0f));
        return;
    }
    if (selected)
    {
        // The chosen row: a raised key under it.
        draw::button (g, juce::Rectangle<float> (3.0f, 2.0f, static_cast<float> (width - 10), static_cast<float> (height - 4)), 8.0f, false, true, colour::accentSoft);
    }
    const auto* d = item.desc;
    // What it will look like on the board, in miniature: a device's enamel and motif, a
    // source's ceramic and badge, a modulator's cream face.
    const auto look = lookFor (d->type, d->role == NodeRole::source ? 0 : -1);
    const auto thumb = juce::Rectangle<float> (12.0f, 0.5f * static_cast<float> (height) - 13.0f, 38.0f, 26.0f);
    {
        juce::Path shape;
        shape.addRoundedRectangle (thumb, 5.0f);
        g.setColour (juce::Colours::black.withAlpha (0.14f));
        g.fillRoundedRectangle (thumb.translated (0.0f, 1.2f), 5.0f);
        juce::Graphics::ScopedSaveState state (g);
        g.reduceClipRegion (shape);
        if (look.face == ModuleLook::Face::enamel && look.id != nullptr)
        {
            g.setGradientFill (juce::ColourGradient (look.id->enamel.brighter (0.06f), 0.0f, thumb.getY(), look.id->enamel.darker (0.07f), 0.0f, thumb.getBottom(), false));
            g.fillRect (thumb);
            andor::analog::paintFxMotif (g, look.id->motif, thumb.reduced (1.0f, 2.0f), *look.id, andor::analog::MotifScale::thumb);
        }
        else
        {
            const bool terminal = look.face == ModuleLook::Face::terminal;
            g.setGradientFill (juce::ColourGradient (terminal ? board::terminalFace : colour::cardTop, 0.0f, thumb.getY(), terminal ? board::terminalFace : colour::cardBottom, 0.0f,
                                                     thumb.getBottom(), false));
            g.fillRect (thumb);
            if (look.badge >= 0)
                paintBadge (g, thumb.withSizeKeepingCentre (17.0f, 17.0f), 0, "A");
            else
                drawSocket (g, thumb.getCentre(), look.face == ModuleLook::Face::key ? PortKind::notes : PortKind::control, true, false, 13.0f);
        }
    }
    g.setColour ((look.face == ModuleLook::Face::enamel ? look.edge : colour::knobRim).withAlpha (0.9f));
    g.drawRoundedRectangle (thumb.reduced (0.5f), 5.0f, 1.0f);

    g.setColour (d->available ? colour::text : colour::textMicro);
    g.setFont (fonts::make (13.5f, Weight::medium, 0.05f));
    g.drawText (juce::String (d->name), 60, 5, width - 170, 18, juce::Justification::centredLeft, true);
    g.setColour (colour::textSecondary.withAlpha (d->available ? 1.0f : 0.7f));
    g.setFont (type::micro (11.5f));
    g.drawText (d->available ? juce::String (d->description) : juce::String ("Not available: ") + d->unavailableReason, 60, 22, width - 74, 16,
                juce::Justification::centredLeft, true);
    g.setFont (fonts::make (9.5f, Weight::medium, 0.12f));
    const juce::String category (juce::String (d->category));
    juce::String tag = category == "Sources" ? "SOURCE" : category == "Processors" ? "PROCESSOR" : category == "Controllers" ? "CONTROLLER"
                     : category == "Utilities" ? "UTILITY" : category.toUpperCase();
    if (d->cost.heavy)
        tag = "CPU  " + tag;
    g.setColour (d->cost.heavy ? colour::arp.darker (0.25f) : colour::textMicro);
    g.drawText (tag, width - 118, 6, 100, 16, juce::Justification::centredRight, false);
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
