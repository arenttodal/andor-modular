#include "PlayView.h"

#include "PluginProcessor.h"

#include "core/Prng.h"

namespace andor::modular::ui
{

//==============================================================================
MacroKnob::MacroKnob (ModularProcessor& p, int i) : processor (p), slot (i)
{
    // InstrumentFrame.cpp, setUpMacro: the rendered ivory cap, the macro's identity on a thin
    // value ring, whole percent in its bubble.
    knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setRotaryParameters (LookAndFeel::rotaryStart, LookAndFeel::rotaryEnd, true);
    knob.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    knob.setMouseDragSensitivity (220);
    knob.getProperties().set ("thinArc", 2.4);
    knob.getProperties().set ("sprite", "macro-ivory");
    knob.getProperties().set ("spritePilot", true);
    knob.setColour (juce::TooltipWindow::textColourId, palette::valueBubbleText);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.parameters(), ModularProcessor::macroId (i), knob);
    knob.textFromValueFunction = [] (double v) { return juce::String (juce::roundToInt (v * 100.0)) + " %"; };
    knob.updateText();
    addAndMakeVisible (knob);
    knob.addMouseListener (this, false);
    knob.setTitle ("Macro " + juce::String (i + 1));
    refresh();
}

void MacroKnob::refresh()
{
    const auto& m = processor.session().document().macros[static_cast<std::size_t> (slot)];
    const auto newName = m.used() && ! m.name.empty() ? juce::String (m.name).toUpperCase() : "MACRO " + juce::String (slot + 1);
    const auto newIdentity = macroColour (slot + m.color);
    knob.setTooltip (m.used() ? juce::String (m.name) + ": moves " + juce::String (static_cast<int> (m.mappings.size())) + " parameter(s). Right-click to edit."
                              : juce::String ("Macro ") + juce::String (slot + 1) + " is free: right-click any knob in PATCH and choose Assign to macro.");
    knob.setDoubleClickReturnValue (true, m.defaultValue);
    if (newName == name && newIdentity == identity && used == m.used())
        return;
    name = newName;
    identity = newIdentity;
    used = m.used();
    knob.getProperties().set ("arc", static_cast<juce::int64> (identity.getARGB()));
    knob.setAlpha (used ? 1.0f : 0.5f);
    knob.setPopupDisplayEnabled (true, true, getParentComponent());
    repaint();
}

void MacroKnob::paint (juce::Graphics& g)
{
    const auto r = getLocalBounds().toFloat();
    g.setColour (used ? colour::text : colour::textMicro);
    g.setFont (type::macroLabel (14.5f));
    g.drawFittedText (name, getLocalBounds().removeFromTop (30).reduced (2, 0), juce::Justification::centred, 1, 0.72f);
    // Under the knob its small lens: lit while the macro moves something (the family's lens
    // of that identity, rendered; its halo drawn).
    const juce::Point<float> centre (r.getCentreX(), static_cast<float> (knob.getBounds().getCentreY()) + 65.0f);
    const auto& m = processor.session().document().macros[static_cast<std::size_t> (slot)];
    if (used)
    {
        juce::ColourGradient halo (identity.withAlpha (0.3f), centre.x, centre.y, identity.withAlpha (0.0f), centre.x + 10.0f, centre.y, true);
        g.setGradientFill (halo);
        g.fillEllipse (juce::Rectangle<float> (20.0f, 20.0f).withCentre (centre));
    }
    if (auto* pieces = skin::active())
        if (pieces->draw (g, juce::String ("led-") + macroLens (slot + m.color) + (used ? "-lit" : "-unlit"), juce::Rectangle<float> (9.0f, 9.0f).withCentre (centre)))
            return;
    draw::led (g, centre, 9.0f, used ? identity.withMultipliedBrightness (1.2f) : identity.interpolatedWith (colour::panelBottom, 0.35f), used ? 0.8f : 0.0f);
}

void MacroKnob::resized()
{
    const int side = std::min (getWidth(), 124);
    knob.setBounds (juce::Rectangle<int> (side, side).withCentre ({ getWidth() / 2, 106 }));
}

void MacroKnob::mouseDown (const juce::MouseEvent& e)
{
    if (e.mods.isPopupMenu())
        showMenu();
}

void MacroKnob::showMenu()
{
    auto& s = processor.session();
    const int i = slot;
    const auto& m = s.document().macros[static_cast<std::size_t> (i)];
    juce::PopupMenu menu;
    menu.addSectionHeader ("MACRO " + juce::String (i + 1) + (m.name.empty() ? juce::String() : "  " + juce::String (m.name)));
    menu.addItem ("Rename...", [this, &s, i] {
        auto* w = new juce::AlertWindow ("Macro name", {}, juce::MessageBoxIconType::NoIcon, this);
        w->addTextEditor ("name", juce::String (s.document().macros[static_cast<std::size_t> (i)].name));
        w->addButton ("RENAME", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([&s, i, w] (int r) {
                                if (r == 1)
                                    s.perform ("rename macro", [&] (Document& d) {
                                        d.macros[static_cast<std::size_t> (i)].name = w->getTextEditorContents ("name").toUpperCase().toStdString();
                                        edit::Result res;
                                        res.ok = true;
                                        return res;
                                    }, false);
                            }),
                            true);
    });
    juce::PopupMenu colours;
    static const char* const identities[] = { "Rust", "Ochre", "Moss", "Mineral blue", "Spore", "Verdigris" };
    for (int c = 0; c < 6; ++c)
        colours.addColouredItem (100 + c, identities[c], macroColour (c), true, macroIdentity (i + m.color) == macroIdentity (c), nullptr);
    menu.addSubMenu ("Colour", colours);
    menu.addItem ("Set its default here", [&s, i, this] {
        s.perform ("macro default", [&] (Document& d) {
            d.macros[static_cast<std::size_t> (i)].defaultValue = knob.getValue();
            edit::Result r;
            r.ok = true;
            return r;
        }, false);
    });
    menu.addItem ("Back to its default", [&s, i, this] { processor.setMacro (i, static_cast<float> (s.document().macros[static_cast<std::size_t> (i)].defaultValue)); });
    for (std::size_t k = 0; k < m.mappings.size(); ++k)
    {
        const auto& mm = m.mappings[k];
        const auto* node = s.document().findNode (mm.node);
        const auto* grp = s.document().findGroup (mm.node);
        juce::String what = (node != nullptr ? juce::String (s.document().displayName (s.registry(), *node)) : (grp != nullptr ? juce::String (grp->name) : juce::String (mm.node)))
                            + "  " + mm.param;
        juce::PopupMenu one;
        auto setRange = [&s, i, k] (double lo, double hi, MacroCurve curve) {
            s.perform ("macro range", [&] (Document& d) {
                auto& x = d.macros[static_cast<std::size_t> (i)].mappings[k];
                x.min = lo;
                x.max = hi;
                x.curve = curve;
                edit::Result r;
                r.ok = true;
                return r;
            }, false);
        };
        one.addItem ("Full range", [setRange] { setRange (0.0, 1.0, MacroCurve::linear); });
        one.addItem ("Upper half", [setRange] { setRange (0.5, 1.0, MacroCurve::linear); });
        one.addItem ("Lower half", [setRange] { setRange (0.0, 0.5, MacroCurve::linear); });
        one.addItem ("Gentle (middle third)", [setRange] { setRange (0.33, 0.66, MacroCurve::linear); });
        one.addItem ("Inverted", [setRange] { setRange (1.0, 0.0, MacroCurve::linear); });
        one.addItem ("Slow start (exponential)", [setRange, lo = mm.min, hi = mm.max] { setRange (lo, hi, MacroCurve::exponential); });
        one.addItem ("Fast start (logarithmic)", [setRange, lo = mm.min, hi = mm.max] { setRange (lo, hi, MacroCurve::logarithmic); });
        one.addItem ("S-curve (bipolar)", [setRange, lo = mm.min, hi = mm.max] { setRange (lo, hi, MacroCurve::bipolar); });
        one.addSeparator();
        one.addItem ("Remove this mapping", [&s, i, k] {
            s.perform ("unmap", [&] (Document& d) {
                auto& maps = d.macros[static_cast<std::size_t> (i)].mappings;
                if (k < maps.size())
                    maps.erase (maps.begin() + static_cast<long> (k));
                edit::Result r;
                r.ok = true;
                r.message = "mapping removed";
                return r;
            });
        });
        menu.addSubMenu (what + "  " + juce::String (juce::roundToInt (mm.min * 100)) + "-" + juce::String (juce::roundToInt (mm.max * 100)) + " %  "
                             + macroCurveName (mm.curve),
                         one);
    }
    if (! m.used())
        menu.addItem ("Assign: right-click any knob in PATCH > Assign to macro " + juce::String (i + 1), false, false, nullptr);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (knob), [&s, i] (int result) {
        if (result >= 100 && result < 106)
            s.perform ("macro colour", [&] (Document& d) {
                // (stored relative to the slot's own identity; slots 0..5 wear the six in order)
                static constexpr int slotOf[6] = { 0, 1, 2, 3, 5, 4 };   // identity -> the first slot that wears it
                int wanted = 0;
                for (int k = 0; k < 6; ++k)
                    if (macroIdentity (slotOf[k]) == macroIdentity (result - 100))
                        wanted = slotOf[k];
                d.macros[static_cast<std::size_t> (i)].color = (wanted - i + 8) % 8;
                edit::Result r;
                r.ok = true;
                return r;
            }, false);
    });
}

//==============================================================================
ScenePanel::ScenePanel (ModularProcessor& p) : processor (p), session (p.session())
{
    static const char* const numerals[sceneCount] = { "I", "II", "III", "IV" };
    for (int s = 0; s < sceneCount; ++s)
    {
        auto& b = scenes[static_cast<std::size_t> (s)];
        b = std::make_unique<KeyButton> (numerals[s], skin::KeyRole::modifier);
        b->setFontHeight (15.0f);
        b->onClick = [this, s] { sceneClicked (s, storeArmed || juce::ModifierKeys::currentModifiers.isShiftDown()); };
        b->setTooltip ("Recall this scene (Shift-click, or STORE first, to capture the patch into it)");
        addAndMakeVisible (*b);
    }
    store.setFontHeight (12.5f);
    store.setClickingTogglesState (true);
    store.onClick = [this] {
        storeArmed = store.getToggleState();
        repaint();
    };
    store.setTooltip ("Then pick a scene: the patch's current values and macros are stored in it");
    addAndMakeVisible (store);
    fader.setStops (sceneCount);
    fader.setTitle ("Scene morph");
    fader.setTooltip ("Morph between the stored scenes (continuous values glide; modes switch at the middle)");
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.parameters(), "scene", fader);
    addAndMakeVisible (fader);
    setTitle ("Scenes");
}

void ScenePanel::sceneClicked (int index, bool storeIt)
{
    if (storeIt)
    {
        session.perform ("store scene", [&] (Document& d) {
            edit::captureScene (d, session.registry(), index);
            edit::Result r;
            r.ok = true;
            r.message = "stored scene " + d.scenes[static_cast<std::size_t> (index)].name;
            return r;
        });
        storeArmed = false;
        store.setToggleState (false, juce::dontSendNotification);
    }
    else if (session.document().scenes[static_cast<std::size_t> (index)].captured)
        processor.setScene (static_cast<float> (index));
    else
        session.message ("scene " + juce::String (index + 1) + " is empty: STORE, then pick it, to capture the patch now");
    refresh();
}

void ScenePanel::refresh()
{
    const auto& doc = session.document();
    for (int s = 0; s < sceneCount; ++s)
    {
        const auto& sc = doc.scenes[static_cast<std::size_t> (s)];
        auto& b = *scenes[static_cast<std::size_t> (s)];
        b.setEnabled (sc.captured || storeArmed);
        const bool here = sc.captured && std::abs (processor.engine().scenePosition() - static_cast<float> (s)) < 0.05f;
        if (here != b.getToggleState())
        {
            b.setToggleState (here, juce::dontSendNotification);
            skin::repaintKey (b);
        }
    }
    repaint (0, 0, getWidth(), 24);
}

void ScenePanel::paint (juce::Graphics& g)
{
    skin::drawKeys (g, *this);
    g.setFont (fonts::make (12.0f, Weight::medium, 0.12f));
    g.setColour (colour::textSecondary);
    g.drawText ("SCENES", getLocalBounds().removeFromTop (20), juce::Justification::centredLeft, false);
    // which scene (or between which two) the patch is at
    const auto& doc = session.document();
    const float pos = processor.engine().scenePosition();
    const int nearest = juce::jlimit (0, sceneCount - 1, juce::roundToInt (pos));
    juce::String where;
    if (storeArmed)
        where = "PICK A SCENE";
    else if (std::abs (pos - static_cast<float> (nearest)) < 0.05f)
        where = doc.scenes[static_cast<std::size_t> (nearest)].captured ? juce::String (doc.scenes[static_cast<std::size_t> (nearest)].name).toUpperCase() : juce::String();
    else
        where = "MORPH";
    g.setFont (fonts::make (11.5f, Weight::regular, 0.06f));
    g.setColour (storeArmed ? colour::accent : colour::textMicro.darker (0.15f));
    g.drawFittedText (where, getLocalBounds().removeFromTop (20).withTrimmedLeft (70), juce::Justification::centredRight, 1, 0.75f);
}

void ScenePanel::resized()
{
    auto r = getLocalBounds();
    r.removeFromTop (30);
    auto keys = r.removeFromTop (44);
    const int gap = 8, w = (keys.getWidth() - gap * (sceneCount - 1)) / sceneCount;
    for (int s = 0; s < sceneCount; ++s)
        scenes[static_cast<std::size_t> (s)]->setBounds (keys.getX() + s * (w + gap), keys.getY(), w, keys.getHeight());
    r.removeFromTop (14);
    fader.setBounds (r.removeFromTop (36));
    r.removeFromTop (12);
    store.setBounds (r.removeFromTop (34).removeFromLeft (96));
}

//==============================================================================
EvolvePanel::EvolvePanel (Session& s) : session (s)
{
    seed = s.document().evolve.seed + static_cast<std::uint64_t> (s.document().evolve.generation) * 7919u + 1;
    for (auto* b : { &subtle, &moderate, &wild })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (17);
        b->setFontHeight (12.0f);
        addAndMakeVisible (*b);
    }
    subtle.setToggleState (true, juce::dontSendNotification);
    subtle.setTooltip ("Tunings, filters, envelopes, drift");
    moderate.setTooltip ("Also shapes, motion and modulation depths");
    wild.setTooltip ("Also new processors and modulations (within the budgets)");
    for (auto* t : { &lockScale, &lockRhythm, &lockRouting, &keepLevel })
    {
        t->setClickingTogglesState (true);
        t->setFontHeight (11.0f);
        addAndMakeVisible (*t);
    }
    lockScale.setToggleState (true, juce::dontSendNotification);
    keepLevel.setToggleState (true, juce::dontSendNotification);
    for (auto* b : { &dice, &go, &compare, &keep })
    {
        b->setFontHeight (12.5f);
        addAndMakeVisible (*b);
    }
    dice.onClick = [this] {
        seed = osp::Prng::splitMix64 (seed);
        seed %= 1000000;
        repaint();
    };
    go.onClick = [this] { run(); };
    compare.setEnabled (false);
    compare.onClick = [this] {
        // A/B: the original is one undo away, the variation one redo away.
        if (showingOriginal ? session.redo() : session.undo())
            showingOriginal = ! showingOriginal;
        compare.setButtonText (showingOriginal ? "A  ORIGINAL" : "B  EVOLVED");
    };
    keep.setEnabled (false);
    keep.onClick = [this] {
        if (showingOriginal)
            session.redo();
        showingOriginal = false;
        evolved = false;
        compare.setEnabled (false);
        keep.setEnabled (false);
        summary = "Kept (one UNDO goes back).";
        repaint();
    };
    close.setIcon (icon::Kind::close);
    close.setTooltip ("Close (Esc)");
    close.onClick = [this] {
        if (onClose)
            onClose();
    };
    addAndMakeVisible (close);
    setWantsKeyboardFocus (true);
    setTitle ("Evolve");
    setSize (cardWidth + 2 * margin, cardHeight + 2 * margin);
}

bool EvolvePanel::keyPressed (const juce::KeyPress& k)
{
    if (k == juce::KeyPress::escapeKey && onClose)
    {
        onClose();
        return true;
    }
    return false;
}

void EvolvePanel::run()
{
    edit::EvolveOptions o;
    o.intensity = wild.getToggleState() ? edit::EvolveOptions::Intensity::wild
                                        : (moderate.getToggleState() ? edit::EvolveOptions::Intensity::moderate : edit::EvolveOptions::Intensity::subtle);
    o.seed = seed;
    o.lockScale = lockScale.getToggleState();
    o.lockRhythm = lockRhythm.getToggleState();
    o.lockRouting = lockRouting.getToggleState();
    o.preserveLevel = keepLevel.getToggleState();
    if (showingOriginal)
        session.redo();
    std::vector<std::string> changes;
    const auto r = session.perform ("EVOLVE", [&] (Document& d) {
        auto result = edit::evolve (d, session.registry(), o);
        edit::Result er;
        er.ok = result.ok;
        if (result.ok)
        {
            changes = result.changes;
            d = std::move (result.doc);
            er.message = "EVOLVE: " + std::to_string (changes.size()) + " changes (seed " + std::to_string (o.seed) + ")";
        }
        else
            er.message = "EVOLVE found no valid variation (try another seed or fewer locks)";
        return er;
    });
    evolved = r.ok;
    showingOriginal = false;
    compare.setEnabled (evolved);
    keep.setEnabled (evolved);
    compare.setButtonText ("B  EVOLVED");
    summary.clear();
    for (std::size_t i = 0; i < changes.size() && i < 5; ++i)
        summary << changes[i] << "\n";
    if (changes.size() > 5)
        summary << "... and " << static_cast<int> (changes.size() - 5) << " more";
    repaint();
}

void EvolvePanel::paint (juce::Graphics& g)
{
    const auto c = card();
    paintPopover (g, c.toFloat());
    skin::drawKeys (g, *this);
    auto r = c.reduced (20, 0);
    // The popups' title: small, tracked; the rule under it.
    g.setColour (colour::textSecondary);
    g.setFont (type::popupTitle (13.0f));
    g.drawText ("EVOLVE", r.getX(), c.getY() + 14, 200, 20, juce::Justification::centredLeft, false);
    g.setColour (colour::hairline);
    g.fillRect (r.getX(), c.getY() + 42, r.getWidth(), 1);
    g.setColour (colour::textSecondary);
    g.setFont (type::micro (12.5f));
    g.drawText ("A seeded variation of this patch: one undo, always repeatable.", r.getX(), c.getY() + 50, r.getWidth(), 18, juce::Justification::centredLeft, false);
    g.setFont (type::popupLabel (11.0f));
    g.setColour (colour::textMicro.darker (0.2f));
    g.drawText ("HOW FAR", r.getX(), subtle.getY() - 18, 120, 14, juce::Justification::centredLeft, false);
    g.drawText ("WHAT STAYS", r.getX(), lockScale.getY() - 18, 120, 14, juce::Justification::centredLeft, false);
    g.drawText ("SEED", r.getX(), dice.getY() + 2, 60, 14, juce::Justification::centredLeft, false);
    g.setColour (colour::text);
    g.setFont (type::popupValue (15.0f));
    g.drawText (juce::String (static_cast<juce::int64> (seed)), r.getX(), dice.getY() + 15, 140, 18, juce::Justification::centredLeft, false);
    g.setColour (colour::textSecondary);
    g.setFont (type::micro (12.0f));
    g.drawFittedText (summary, r.getX(), go.getBottom() + 10, r.getWidth(), c.getBottom() - go.getBottom() - 18, juce::Justification::topLeft, 6, 0.9f);
}

void EvolvePanel::resized()
{
    const auto c = card();
    close.setBounds (c.getRight() - 40, c.getY() + 9, 32, 30);
    auto r = c.reduced (20, 0).withTrimmedTop (96);
    auto row = r.removeFromTop (34);
    const int gap = 8, w = (row.getWidth() - 2 * gap) / 3;
    subtle.setBounds (row.removeFromLeft (w));
    row.removeFromLeft (gap);
    moderate.setBounds (row.removeFromLeft (w));
    row.removeFromLeft (gap);
    wild.setBounds (row);
    r.removeFromTop (28);
    const int half = (r.getWidth() - gap) / 2;
    auto locks = r.removeFromTop (32);
    lockScale.setBounds (locks.removeFromLeft (half));
    locks.removeFromLeft (gap);
    lockRhythm.setBounds (locks);
    r.removeFromTop (gap);
    locks = r.removeFromTop (32);
    lockRouting.setBounds (locks.removeFromLeft (half));
    locks.removeFromLeft (gap);
    keepLevel.setBounds (locks);
    r.removeFromTop (14);
    auto seedRow = r.removeFromTop (36);
    dice.setBounds (seedRow.removeFromRight (112));
    r.removeFromTop (12);
    auto buttons = r.removeFromTop (38);
    go.setBounds (buttons.removeFromLeft (118));
    buttons.removeFromLeft (gap);
    compare.setBounds (buttons.removeFromLeft (124));
    buttons.removeFromLeft (gap);
    keep.setBounds (buttons);
}

//==============================================================================
PlayView::PlayView (ModularProcessor& p) : processor (p), session (p.session())
{
    setTitle ("Play");
    setInterceptsMouseClicks (false, false);
    startTimerHz (30);
}

void PlayView::refresh() { repaint(); }

void PlayView::timerCallback()
{
    if (! isVisible())
        return;
    processor.engine().copyScope (scope.data(), static_cast<int> (scope.size()));
    repaint (well.getSmallestIntegerContainer());
}

void PlayView::resized()
{
    const auto r = getLocalBounds().toFloat();
    well = juce::Rectangle<float> (r.getX() + 0.39f * r.getWidth(), r.getY() + 34.0f, 0.61f * r.getWidth() - 36.0f, r.getHeight() - 68.0f);
}

void PlayView::paint (juce::Graphics& g)
{
    const auto& doc = session.document();
    const auto r = getLocalBounds().toFloat();
    const float left = 44.0f, columnW = well.getX() - left - 36.0f;

    // The instrument's name in ANDOR's identity face, and what it says about itself.
    g.setColour (colour::textMicro.darker (0.2f));
    g.setFont (fonts::make (12.0f, Weight::medium, 0.14f));
    g.drawText ("INSTRUMENT", juce::Rectangle<float> (left, 34.0f, columnW, 16.0f), juce::Justification::centredLeft, false);
    g.setColour (colour::text);
    g.setFont (fonts::make (52.0f, Weight::displayBold).withHorizontalScale (1.04f));
    g.drawFittedText (juce::String (doc.name).toUpperCase(), juce::Rectangle<float> (left - 2.0f, 52.0f, columnW, 62.0f).toNearestInt(), juce::Justification::centredLeft, 1, 0.55f);
    g.setColour (colour::textSecondary);
    g.setFont (type::micro (15.5f));
    g.drawFittedText (doc.description, juce::Rectangle<float> (left, 120.0f, columnW, 62.0f).toNearestInt(), juce::Justification::topLeft, 3, 0.9f);

    // What it is made of: its sources, each with its badge (the family's layer identities).
    g.setColour (board::rule);
    g.fillRect (juce::Rectangle<float> (left, 198.0f, columnW, 1.0f));
    g.setColour (colour::textMicro.darker (0.2f));
    g.setFont (fonts::make (12.0f, Weight::medium, 0.14f));
    g.drawText ("SOURCES", juce::Rectangle<float> (left, 210.0f, columnW, 16.0f), juce::Justification::centredLeft, false);
    int ordinal = 0, row = 0;
    const int perColumn = 5;
    const float rowH = 44.0f, colW = 0.5f * columnW;
    for (const auto& n : doc.nodes)
    {
        const auto* d = session.registry().find (n.type);
        if (d == nullptr || d->role != NodeRole::source)
            continue;
        const auto look = lookFor (n.type, ordinal++);
        if (row >= 2 * perColumn)
            continue;
        const float x = left + (row >= perColumn ? colW : 0.0f), y = 236.0f + rowH * static_cast<float> (row % perColumn);
        if (look.badge >= 0)
            paintBadge (g, { x, y + 4.0f, 28.0f, 28.0f }, look.badge, look.badgeLetter);
        g.setColour (n.enabled ? colour::text : colour::textMicro);
        g.setFont (fonts::make (15.0f, Weight::medium, 0.03f));
        g.drawFittedText (juce::String (doc.displayName (session.registry(), n)), juce::Rectangle<float> (x + 38.0f, y + 2.0f, colW - 46.0f, 18.0f).toNearestInt(),
                          juce::Justification::centredLeft, 1, 0.75f);
        g.setColour (colour::textMicro.darker (0.15f));
        g.setFont (type::micro (12.0f));
        // (its mode when it has one, else what it is; never its name twice)
        juce::String kind = juce::String (doc.displayName (session.registry(), n)).equalsIgnoreCase (juce::String (d->name)) ? juce::String (d->description).upToFirstOccurrenceOf (":", false, false)
                                                                                                               : juce::String (d->name);
        for (const auto& p : d->params)
            if (p.quick && p.curve == ParamCurve::choice && d->paramShown (p, n.params) && ! p.choices.empty())
            {
                const int choice = juce::jlimit (0, static_cast<int> (p.choices.size()) - 1, static_cast<int> (std::lround (doc.paramValue (session.registry(), n.id, p.id))));
                kind = juce::String (d->name) + "   " + juce::String (p.choices[static_cast<std::size_t> (choice)]);
                break;
            }
        g.drawFittedText (kind + (n.enabled ? "" : "  (off)"), juce::Rectangle<float> (x + 38.0f, y + 19.0f, colW - 46.0f, 15.0f).toNearestInt(),
                          juce::Justification::centredLeft, 1, 0.75f);
        ++row;
    }
    if (ordinal == 0)
    {
        g.setColour (colour::textMicro);
        g.setFont (type::micro (14.0f));
        g.drawText ("No source yet: open PATCH and add one.", juce::Rectangle<float> (left, 240.0f, columnW, 20.0f), juce::Justification::centredLeft, false);
    }
    g.setColour (colour::textMicro.darker (0.15f));
    g.setFont (type::micro (12.5f));
    const auto& rep = session.report();
    g.drawText (juce::String (rep.nodes) + " modules    " + juce::String (rep.sources) + " of 6 sources    " + juce::String (static_cast<int> (doc.connections.size())) + " cables",
                juce::Rectangle<float> (left, r.getBottom() - 54.0f, columnW, 18.0f), juce::Justification::centredLeft, false);

    // The display: the output itself, steadied on a rising crossing (the family's graphite
    // well; the trace in the first source's light).
    draw::well (g, well, 12.0f, colour::wellA);
    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (well.reduced (2.0f).getSmallestIntegerContainer());
        const auto inner = well.reduced (22.0f, 30.0f);
        g.setColour (colour::wellGrid);
        g.fillRect (juce::Rectangle<float> (inner.getX(), inner.getCentreY() - 0.5f, inner.getWidth(), 1.0f));
        for (int i = 1; i < 8; ++i)
            g.fillRect (juce::Rectangle<float> (inner.getX() + inner.getWidth() * static_cast<float> (i) / 8.0f, inner.getCentreY() - 3.0f, 1.0f, 6.0f));
        int start = 0;
        for (int i = 1; i < 512; ++i)
            if (scope[static_cast<std::size_t> (i - 1)] <= 0.0f && scope[static_cast<std::size_t> (i)] > 0.0f)
            {
                start = i;
                break;
            }
        const int count = 512;
        float peak = 0.02f;
        for (int i = 0; i < count; ++i)
            peak = std::max (peak, std::abs (scope[static_cast<std::size_t> (start + i)]));
        const float scale = std::min (1.0f, 0.46f / peak) * inner.getHeight();
        juce::Path trace;
        for (int i = 0; i < count; ++i)
        {
            const float x = inner.getX() + inner.getWidth() * static_cast<float> (i) / static_cast<float> (count - 1);
            const float y = inner.getCentreY() - scope[static_cast<std::size_t> (start + i)] * scale;
            if (i == 0)
                trace.startNewSubPath (x, y);
            else
                trace.lineTo (x, y);
        }
        const auto wave = colour::identity (0).wave;
        g.setColour (wave.withAlpha (0.16f));
        g.strokePath (trace, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
        g.setColour (wave);
        g.strokePath (trace, juce::PathStrokeType (1.7f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

        g.setFont (fonts::make (11.5f, Weight::medium, 0.12f));
        g.setColour (colour::wellText);
        const auto labels = well.reduced (20.0f, 12.0f);
        g.drawText ("OUTPUT", labels, juce::Justification::topLeft, false);
        const float db = juce::Decibels::gainToDecibels (processor.engine().outputPeak(), -60.0f);
        g.drawText ("PEAK  " + (db <= -59.9f ? juce::String ("--") : juce::String (db, 1) + " dB"), labels, juce::Justification::topRight, false);
        const int voices = processor.engine().activeVoices();
        g.drawText (juce::String (voices) + (voices == 1 ? " VOICE" : " VOICES"), labels, juce::Justification::bottomLeft, false);
        g.drawText ("CPU  " + juce::String (juce::roundToInt (processor.cpuLoad() * 100.0f)) + " %", labels, juce::Justification::bottomRight, false);
    }
}

} // namespace andor::modular::ui
