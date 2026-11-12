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
namespace
{
    const CompiledNode* liveSource (Session& s, const std::string& id)
    {
        auto* g = s.graph();
        if (g == nullptr)
            return nullptr;
        const auto it = g->nodeIndexById.find (id);
        return it == g->nodeIndexById.end() ? nullptr : &g->nodes[static_cast<std::size_t> (it->second)];
    }

    float live (const CompiledNode* n, int i)
    {
        return n != nullptr && i < n->telemetryCount.load (std::memory_order_relaxed) ? n->telemetry[static_cast<std::size_t> (i)].load (std::memory_order_relaxed) : 0.0f;
    }

    /** One cycle of the wave an ANALOG OSC is set to (the picture of its setting, as the
        family's source displays draw it; -1..1 at phase 0..1). */
    float analogWave (int model, int wave, float width, float sub, float phase)
    {
        const float saw = 2.0f * phase - 1.0f;
        const float tri = 1.0f - 4.0f * std::abs (phase - 0.5f);
        const float pulse = phase < width ? 1.0f : -1.0f;
        if (model == 0)   // CLASSIC: SAW, TRI, PULSE, SAW+TRI, SAW+PULSE, TRI+PULSE, ALL
        {
            static constexpr float mix[7][3] = { { 1, 0, 0 }, { 0, 1, 0 }, { 0, 0, 1 }, { 0.5f, 0.5f, 0 }, { 0.5f, 0, 0.5f }, { 0, 0.5f, 0.5f }, { 0.34f, 0.33f, 0.33f } };
            const auto& m = mix[juce::jlimit (0, 6, wave)];
            return m[0] * saw + m[1] * tri + m[2] * pulse;
        }
        if (model == 1)   // DCO: SAW, PULSE, SAW+PULSE, SUB ONLY (the sub an octave below)
        {
            const float subWave = std::fmod (phase * 0.5f, 1.0f) < 0.5f ? 1.0f : -1.0f;
            const float main = wave == 0 ? saw : (wave == 1 ? pulse : (wave == 2 ? 0.5f * (saw + pulse) : 0.0f));
            return wave == 3 ? subWave : (main * (1.0f - 0.4f * sub) + 0.4f * sub * subWave);
        }
        // WIDE: SAW, PULSE, TRIANGLE, SQUARE, SUPER SAW (seven detuned saws)
        switch (wave)
        {
            case 1: return pulse;
            case 2: return tri;
            case 3: return phase < 0.5f ? 1.0f : -1.0f;
            case 4:
            {
                static constexpr float detune[7] = { 1.0f, 1.012f, 0.988f, 1.031f, 0.969f, 1.058f, 0.942f };
                float sum = 0.0f;
                for (int i = 0; i < 7; ++i)
                    sum += 2.0f * std::fmod (phase * detune[i] * 2.0f + 0.137f * static_cast<float> (i), 1.0f) - 1.0f;
                return sum / 3.6f;
            }
            default: return saw;
        }
    }
}

SourceCard::SourceCard (Session& s, std::string nodeId, int ordinal, bool isCompact) : session (s), node (std::move (nodeId)), compact (isCompact)
{
    const auto* n = session.document().findNode (node);
    const auto* d = n != nullptr ? session.registry().find (n->type) : nullptr;
    look = lookFor (n != nullptr ? n->type : std::string(), ordinal);
    const auto& identity = colour::identity (std::max (0, look.badge));

    mute.setFontHeight (11.0f);
    mute.setTooltip ("Mute this source (it keeps running; a muted source stays silent even when soloed)");
    mute.setTitle ("Mute");
    solo.setFontHeight (11.0f);
    solo.setTooltip ("Solo this source: only it sounds. Shift-click adds it to, or takes it out of, the soloed ones. Click again to end the solo.");
    solo.setTitle ("Solo");
    solo.onClick = [this] { session.toggleSolo (node, juce::ModifierKeys::currentModifiers.isShiftDown()); };
    addAndMakeVisible (solo);
    mute.onClick = [this] {
        if (const auto* target = session.document().findNode (node))
            session.setMuted (node, ! target->muted);
    };
    addAndMakeVisible (mute);
    edit.setIcon (icon::Kind::patch);
    edit.setTooltip ("Show this source in PATCH, with every control");
    edit.setTitle ("Edit in PATCH");
    edit.onClick = [this] {
        if (onEdit)
            onEdit (node);
    };
    addAndMakeVisible (edit);

    if (d != nullptr)
    {
        // Its modes in the display's corner, its switches as keys, then its knobs: the quick
        // ones first (those on its tile), filled up with the rest that apply now.
        std::vector<const ParamDesc*> wanted;
        // (filled up only with continuous controls: a setting such as a sample's ROOT belongs to
        // its detail column)
        for (const bool quick : { true, false })
            for (const auto& p : d->params)
                if (p.quick == quick && d->paramShown (p, n->params) && (quick || (p.curve != ParamCurve::integer && p.curve != ParamCurve::toggle)))
                    wanted.push_back (&p);
        const int maxKnobs = 5;
        for (const auto* p : wanted)
        {
            if (p->curve == ParamCurve::choice)
            {
                if (modes.size() >= 2)
                    continue;
                auto m = std::make_unique<ParamMode> (session, node, p->id);
                m->setOnDisplay (true);
                m->setInks (colour::wellText, identity.wave);
                addAndMakeVisible (*m);
                modes.push_back (std::move (m));
            }
            else if (p->curve == ParamCurve::toggle)
            {
                if (toggles.size() >= 2 || ! p->quick)
                    continue;
                auto t = std::make_unique<ParamToggle> (session, node, p->id, false);
                addAndMakeVisible (*t);
                toggles.push_back (std::move (t));
            }
            else if (static_cast<int> (knobs.size()) < maxKnobs)
            {
                auto k = std::make_unique<ParamKnob> (session, node, p->id, compact ? ParamKnob::Style::detail : ParamKnob::Style::panel);
                k->setInks (colour::textSecondary, colour::text, identity.arc);
                addAndMakeVisible (*k);
                knobs.push_back (std::move (k));
            }
        }
    }
    setTitle (n != nullptr ? juce::String (session.document().displayName (session.registry(), *n)) : juce::String());
    refresh();
    startTimerHz (25);
}

void SourceCard::refresh()
{
    const auto* n = session.document().findNode (node);
    const bool muted = n != nullptr && n->muted, soloed = n != nullptr && n->solo;
    const bool nowAudible = n != nullptr && n->enabled && audibleGain (session.document(), session.registry(), *n) > 0.0;
    if (muted != mute.getToggleState() || soloed != solo.getToggleState() || nowAudible != audible)
    {
        mute.setToggleState (muted, juce::dontSendNotification);
        solo.setToggleState (soloed, juce::dontSendNotification);
        audible = nowAudible;
        repaint();
    }
    for (auto& k : knobs)
        k->refresh();
    for (auto& m : modes)
        m->refresh();
    for (auto& t : toggles)
        t->refresh();
}

void SourceCard::timerCallback()
{
    if (isVisible() && getParentComponent() != nullptr && getParentComponent()->isVisible())
        repaint (well);
}

void SourceCard::resized()
{
    auto r = getLocalBounds().reduced (14, 0);
    const int headerH = compact ? 54 : 72, badge = compact ? 34 : 48;
    auto header = r.removeFromTop (headerH).withTrimmedTop (compact ? 10 : 12);
    header.removeFromLeft (badge + 9);
    {
        // M over S beside the badge, as on ANDOR Analog's cards.
        auto keys = header.removeFromLeft (28).withHeight (badge);
        const int each = (badge - 2) / 2;
        mute.setBounds (keys.removeFromTop (each));
        solo.setBounds (keys.removeFromBottom (each));
    }
    edit.setBounds (header.removeFromRight (40).withHeight (std::min (badge, 34)).translated (0, (badge - std::min (badge, 34)) / 2));
    for (auto it = toggles.rbegin(); it != toggles.rend(); ++it)
    {
        header.removeFromRight (7);
        (*it)->setBounds (header.removeFromRight (74).withHeight (std::min (badge, 30)).translated (0, (badge - std::min (badge, 30)) / 2));
    }
    const int knobRow = knobs.empty() ? 0 : (compact ? 92 : 118);
    auto foot = r.removeFromBottom (knobRow + (compact ? 8 : 14));
    well = r.withTrimmedBottom (compact ? 8 : 14);
    int right = well.getRight() - 12;
    for (auto it = modes.rbegin(); it != modes.rend(); ++it)
    {
        const int w = (*it)->idealWidth();
        (*it)->setBounds (right - w, well.getY() + 8, w, 20);
        right -= w + 14;
    }
    if (! knobs.empty())
    {
        auto row = foot.withHeight (knobRow);
        const int cell = std::min (compact ? 84 : 104, row.getWidth() / static_cast<int> (std::max<std::size_t> (knobs.size(), 4)));
        int x = row.getCentreX() - cell * static_cast<int> (knobs.size()) / 2;
        for (auto& k : knobs)
        {
            k->setBounds (x, row.getY(), cell, row.getHeight());
            x += cell;
        }
    }
}

void SourceCard::paint (juce::Graphics& g)
{
    skin::drawKeys (g, *this);
    const auto& doc = session.document();
    const auto* n = doc.findNode (node);
    if (n == nullptr)
        return;
    const int badge = compact ? 34 : 48;
    paintBadge (g, juce::Rectangle<float> (14.0f, compact ? 10.0f : 12.0f, static_cast<float> (badge), static_cast<float> (badge)), std::max (0, look.badge), look.badgeLetter);
    // Its name, as the family writes a source's.
    const int nameX = mute.getRight() + 12;
    const int nameRight = (toggles.empty() ? edit.getX() : toggles.front()->getX()) - 8;
    g.setColour (audible ? colour::text : colour::textMicro);
    g.setFont (fonts::make (compact ? 13.5f : 15.0f, Weight::regular, 0.06f));
    g.drawFittedText (juce::String (doc.displayName (session.registry(), *n)).toUpperCase(), nameX, compact ? 10 : 12, std::max (20, nameRight - nameX), badge,
                      juce::Justification::centredLeft, 1, 0.7f);

    draw::well (g, well.toFloat(), 10.0f, colour::wellA);
    {
        juce::Graphics::ScopedSaveState clip (g);
        g.reduceClipRegion (well.reduced (2));
        paintDisplay (g, well.toFloat());
        if (! audible)
        {
            // Why it is silent, truthfully: muted, bypassed in PATCH, or another source is soloed.
            g.setColour (colour::wellA.withAlpha (0.6f));
            g.fillRect (well);
            g.setColour (colour::wellText);
            g.setFont (fonts::make (11.5f, Weight::medium, 0.14f));
            g.drawText (n->muted ? "MUTED" : (! n->enabled ? "BYPASSED" : "ANOTHER SOURCE IS SOLOED"), well, juce::Justification::centred, false);
        }
    }
}

void SourceCard::paintDisplay (juce::Graphics& g, juce::Rectangle<float> area) { paintSourceDisplay (g, area, session, node, look, compact); }

void paintSourceDisplay (juce::Graphics& g, juce::Rectangle<float> area, Session& session, const std::string& node, const ModuleLook& look, bool compact, bool owner)
{
    const auto& doc = session.document();
    const auto* n = doc.findNode (node);
    if (n == nullptr)
        return;
    const auto* node_ = liveSource (session, node);
    const auto& identity = colour::identity (std::max (0, look.badge));
    const auto wave = identity.wave;
    const auto inner = area.reduced (16.0f, compact ? 12.0f : 30.0f).withTrimmedTop (compact ? 14.0f : 6.0f);
    const float mid = inner.getCentreY();
    auto value = [&] (const char* id) { return static_cast<float> (doc.paramValue (session.registry(), node, id)); };
    auto corner = [&] (const juce::String& text, juce::Colour c, juce::Justification where = juce::Justification::bottomLeft) {
        g.setColour (c);
        g.setFont (fonts::make (11.5f, Weight::medium, 0.1f));
        g.drawText (text, area.reduced (14.0f, 9.0f), where, false);
    };
    g.setColour (colour::wellGrid);
    g.fillRect (juce::Rectangle<float> (inner.getX(), mid - 0.5f, inner.getWidth(), 1.0f));
    const auto& type = n->type;

    auto trace = [&] (const std::function<float (float)>& at, int points) {
        // The family's source display: the line in the layer's light, a soft fill to the axis.
        juce::Path line;
        for (int i = 0; i < points; ++i)
        {
            const float t = static_cast<float> (i) / static_cast<float> (points - 1);
            const float y = mid - 0.5f * inner.getHeight() * 0.86f * juce::jlimit (-1.0f, 1.0f, at (t));
            if (i == 0)
                line.startNewSubPath (inner.getX(), y);
            else
                line.lineTo (inner.getX() + inner.getWidth() * t, y);
        }
        juce::Path fill (line);
        fill.lineTo (inner.getRight(), mid);
        fill.lineTo (inner.getX(), mid);
        fill.closeSubPath();
        g.setGradientFill (juce::ColourGradient (wave.withAlpha (0.22f), 0.0f, inner.getY(), wave.withAlpha (0.05f), 0.0f, inner.getBottom(), false));
        g.fillPath (fill);
        g.setColour (wave);
        g.strokePath (line, juce::PathStrokeType (1.5f, juce::PathStrokeType::mitered, juce::PathStrokeType::rounded));
    };

    if (type == "osc.analog")
    {
        const int model = juce::roundToInt (value ("model"));
        const int shape = juce::roundToInt (value (model == 0 ? "wave" : (model == 1 ? "dcoWave" : "wideWave")));
        const float width = value ("width"), sub = model == 1 ? value ("sub") : 0.0f, level = value ("level");
        trace ([&] (float t) { return (0.35f + 0.65f * std::sqrt (level)) * analogWave (model, shape, width, sub, std::fmod (t * 2.0f, 1.0f)); }, 720);
        if (live (node_, 0) > 0.0f)
            corner (juce::String (live (node_, 0), 1) + " Hz", wave);
        return;
    }
    if (type == "src.noise")
    {
        // (a fixed picture of its colour: the same in every session and screenshot)
        const int colourIndex = juce::roundToInt (value ("color"));
        osp::Prng random (std::uint64_t { 0x6e6f697365 } + static_cast<std::uint64_t> (colourIndex));
        const float smooth = colourIndex == 0 ? 0.0f : (colourIndex == 1 ? 0.6f : 0.92f);
        const float gain = (colourIndex == 2 ? 3.2f : (colourIndex == 1 ? 1.5f : 0.9f)) * (0.3f + 0.7f * value ("level"));
        float state = 0.0f;
        trace ([&] (float) {
            state = smooth * state + (1.0f - smooth) * static_cast<float> (random.bipolar());
            return gain * state;
        }, 360);
        return;
    }
    if (type == "src.organic" || type == "src.granular" || type == "src.rhythm")
    {
        const auto it = n->strings.find ("sample");
        const auto key = it != n->strings.end() ? it->second : std::string();
        const auto asset = key.empty() ? nullptr : session.assets().resolve (session.resolveKey (key).getFullPathName().toStdString());
        if (asset == nullptr || key.empty())
        {
            corner ("DROP A RECORDING ON IT IN PATCH", colour::arpOnDark, juce::Justification::centred);
            return;
        }
        if (asset->status == SampleAsset::Status::missing || asset->status == SampleAsset::Status::failed)
        {
            corner (asset->status == SampleAsset::Status::missing ? "MISSING: RELINK IT IN PATCH" : "UNREADABLE", juce::Colour (0xffe28a6c), juce::Justification::centred);
            return;
        }
        if (! asset->overview.empty())
        {
            const float w = inner.getWidth() / static_cast<float> (asset->overview.size());
            g.setColour (wave.withAlpha (0.8f));
            for (std::size_t i = 0; i < asset->overview.size(); ++i)
            {
                const float h = std::max (1.0f, inner.getHeight() * 0.9f * asset->overview[i]);
                g.fillRect (inner.getX() + w * static_cast<float> (i), mid - h * 0.5f, std::max (1.0f, w - 0.6f), h);
            }
        }
        float head = -1.0f;
        if (type == "src.organic" && node_ != nullptr && node_->telemetryCount.load() > 0)
            head = live (node_, 0);
        if (type == "src.granular")
        {
            head = live (node_, 1);
            const int count = node_ != nullptr ? node_->telemetryCount.load() - 2 : 0;
            g.setColour (juce::Colours::white.withAlpha (0.85f));
            for (int i = 0; i < count; ++i)
                g.fillEllipse (juce::Rectangle<float> (4.0f, 4.0f).withCentre ({ inner.getX() + inner.getWidth() * live (node_, 2 + i),
                                                                                 mid + inner.getHeight() * 0.32f * (static_cast<float> ((i * 7) % 11) / 5.0f - 1.0f) }));
        }
        if (head >= 0.0f)
        {
            g.setColour (juce::Colours::white.withAlpha (0.9f));
            g.fillRect (juce::Rectangle<float> (inner.getX() + inner.getWidth() * juce::jlimit (0.0f, 1.0f, head), inner.getY(), 1.3f, inner.getHeight()));
        }
        corner (session.resolveKey (key).getFileNameWithoutExtension(), colour::wellText);
        if (asset->status != SampleAsset::Status::ready)
            corner (asset->status == SampleAsset::Status::loading ? "LOADING" : "PLAYABLE, ANALYSING", colour::arpOnDark, juce::Justification::bottomRight);
        else if (type == "src.rhythm" && asset->tempoBpm > 0.0)
            corner (juce::String (asset->tempoBpm, 1) + " BPM", wave, juce::Justification::bottomRight);
        else if (type == "src.granular")
            corner (juce::String (static_cast<int> (live (node_, 0))) + " GRAINS", wave, juce::Justification::bottomRight);
        else if (! asset->pitched() && doc.paramValue (session.registry(), node, "root") < 0.0)
            corner ("NO CLEAR PITCH: C4 PLAYS IT AS RECORDED", colour::wellText, juce::Justification::bottomRight);
        return;
    }
    if (type == "src.external")
        corner (session.externals().status (node).toUpperCase(), colour::wellText, juce::Justification::centred);
    // any other source: what leaves it now
    float level = node_ != nullptr ? node_->peak.load (std::memory_order_relaxed) : 0.0f;
    if (node_ != nullptr && owner)
        const_cast<CompiledNode*> (node_)->peak.store (level * 0.8f, std::memory_order_relaxed);   // decays between frames
    const auto bar = juce::Rectangle<float> (inner.getX(), area.getBottom() - 22.0f, inner.getWidth(), 4.0f);
    g.setColour (wave.withAlpha (0.25f));
    g.fillRoundedRectangle (bar, 2.0f);
    g.setColour (wave);
    g.fillRoundedRectangle (bar.withWidth (bar.getWidth() * juce::jlimit (0.0f, 1.0f, level)), 2.0f);
}

//==============================================================================
PlayView::PlayView (ModularProcessor& p) : session (p.session())
{
    setTitle ("Play");
    addSource.setIcon (icon::Kind::plus);
    addSource.setFontHeight (12.5f);
    addSource.onClick = [this] {
        if (onAddSource)
            onAddSource();
    };
    addChildComponent (addSource);
    rebuild();
}

std::string PlayView::signature() const
{
    // The sources and what each shows (a model change brings other controls).
    std::string s;
    const auto& doc = session.document();
    for (const auto& n : doc.nodes)
    {
        const auto* d = session.registry().find (n.type);
        if (d == nullptr || d->role != NodeRole::source)
            continue;
        s += n.id + ":" + n.type + ":";
        for (const auto& p : d->params)
            if (d->paramShown (p, n.params))
                s += p.id + ",";
        s += ";";
    }
    return s;
}

void PlayView::refresh()
{
    if (signature() != built)
    {
        rebuild();
        return;
    }
    for (auto& c : cards)
        c->refresh();
}

void PlayView::rebuild()
{
    built = signature();
    cards.clear();
    const auto& doc = session.document();
    std::vector<std::string> sources;
    for (const auto& n : doc.nodes)
        if (const auto* d = session.registry().find (n.type); d != nullptr && d->role == NodeRole::source)
            sources.push_back (n.id);
    const bool compact = sources.size() > 4;
    for (std::size_t i = 0; i < sources.size() && i < 6; ++i)
    {
        cards.push_back (std::make_unique<SourceCard> (session, sources[i], static_cast<int> (i), compact));
        cards.back()->onEdit = [this] (const std::string& id) {
            if (onEdit)
                onEdit (id);
        };
        addAndMakeVisible (*cards.back());
    }
    addSource.setVisible (cards.empty());
    resized();
    if (auto* parent = getParentComponent())
        parent->repaint();   // (the cards' faces are the surface's to draw)
}

std::vector<juce::Rectangle<int>> PlayView::cardAreas() const
{
    // The family's band: one, two or three cards across (design::layout::cardGap); Modular
    // goes on to four, then to two rows of three.
    const int count = std::max<int> (1, static_cast<int> (cards.size()));
    const int columns = count > 4 ? 3 : count, rows = count > 4 ? 2 : 1;
    const int gap = juce::roundToInt (design::layout::cardGap);
    const int w = (getWidth() - gap * (columns - 1)) / columns, h = (getHeight() - gap * (rows - 1)) / rows;
    std::vector<juce::Rectangle<int>> areas;
    for (int i = 0; i < count; ++i)
        areas.emplace_back ((i % columns) * (w + gap), (i / columns) * (h + gap), i % columns == columns - 1 ? getWidth() - (columns - 1) * (w + gap) : w, h);
    return areas;
}

void PlayView::resized()
{
    const auto areas = cardAreas();
    for (std::size_t i = 0; i < cards.size() && i < areas.size(); ++i)
        cards[i]->setBounds (areas[i]);
    addSource.setBounds (getLocalBounds().withSizeKeepingCentre (176, 40));
}

void PlayView::paint (juce::Graphics& g)
{
    if (cards.empty())
    {
        // An empty patch: its one card, and the one thing to do.
        skin::drawKeys (g, *this);
        g.setColour (colour::textSecondary);
        g.setFont (type::micro (14.0f));
        g.drawText ("No source yet", getLocalBounds().withSizeKeepingCentre (300, 20).translated (0, -40), juce::Justification::centred, false);
    }
}

} // namespace andor::modular::ui
