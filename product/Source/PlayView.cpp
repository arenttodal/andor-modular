#include "PlayView.h"

#include "PluginProcessor.h"

#include "core/Prng.h"

namespace andor::modular::ui
{

//==============================================================================
MacroKnob::MacroKnob (ModularProcessor& p, int i, bool large) : processor (p), slot (i), big (large)
{
    knob.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    knob.setMouseDragSensitivity (220);
    knob.setInterceptsMouseClicks (true, false);
    knob.getProperties().set ("bipolar", false);
    attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.parameters(), ModularProcessor::macroId (i), knob);
    knob.setLookAndFeel (nullptr);
    addAndMakeVisible (knob);
    knob.addMouseListener (this, false);
    knob.setTitle ("Macro " + juce::String (i + 1));
    refresh();
}

void MacroKnob::refresh()
{
    const auto& m = processor.session().document().macros[static_cast<std::size_t> (slot)];
    knob.setTooltip (m.used() ? juce::String (m.name) + ": moves " + juce::String (static_cast<int> (m.mappings.size())) + " parameter(s). Right-click to edit."
                              : juce::String ("Macro ") + juce::String (slot + 1) + " is free: right-click any knob in PATCH and choose Assign to macro.");
    knob.setAlpha (m.used() ? 1.0f : 0.45f);
    repaint();
}

void MacroKnob::paint (juce::Graphics& g)
{
    const auto& m = processor.session().document().macros[static_cast<std::size_t> (slot)];
    auto r = getLocalBounds();
    auto label = r.removeFromBottom (big ? 34 : 16);
    g.setColour (m.used() ? colour::bone : colour::boneFaint);
    g.setFont (text (big ? 13.0f : 10.0f, 600));
    g.drawFittedText (m.used() ? juce::String (m.name).toUpperCase() : juce::String ("MACRO ") + juce::String (slot + 1), label.removeFromTop (big ? 18 : 16),
                      juce::Justification::centred, 1, 0.8f);
    if (big)
    {
        g.setColour (colour::boneDim);
        g.setFont (text (11.0f));
        g.drawText (m.used() ? juce::String (juce::roundToInt (knob.getValue() * 100.0)) + " %" : juce::String ("unassigned"), label, juce::Justification::centred);
    }
    const auto k = knob.getBounds().toFloat();
    const auto identity = colour::macro (slot + m.color);
    drawKnob (g, k, static_cast<float> (knob.getValue()), false, 0.0f, false, knob.isMouseOverOrDragging(), m.used() ? identity : colour::sporeDim);
    // the macro's identity: a small coloured tick above its knob
    g.setColour (identity.withAlpha (m.used() ? 1.0f : 0.3f));
    g.fillRoundedRectangle (k.getCentreX() - 8.0f, k.getY() - 6.0f, 16.0f, 3.0f, 1.5f);
}

void MacroKnob::resized()
{
    auto r = getLocalBounds();
    r.removeFromBottom (big ? 34 : 16);
    r.removeFromTop (big ? 8 : 4);
    const int d = std::min (r.getWidth(), r.getHeight());
    knob.setBounds (r.withSizeKeepingCentre (d, d));
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
    for (int c = 0; c < 8; ++c)
        colours.addColouredItem (100 + c, "Colour " + juce::String (c + 1), colour::macro (c), true, ((i + m.color) % 8) == c, nullptr);
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
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (this), [&s, i] (int result) {
        if (result >= 100 && result < 108)
            s.perform ("macro colour", [&] (Document& d) {
                d.macros[static_cast<std::size_t> (i)].color = (result - 100 - i + 8) % 8;   // stored relative to the slot's own colour
                edit::Result r;
                r.ok = true;
                return r;
            }, false);
    });
}

//==============================================================================
EvolvePanel::EvolvePanel (Session& s) : session (s)
{
    seed = s.document().evolve.seed + static_cast<std::uint64_t> (s.document().evolve.generation) * 7919u + 1;
    for (auto* b : { &subtle, &moderate, &wild })
    {
        b->setClickingTogglesState (true);
        b->setRadioGroupId (17);
        addAndMakeVisible (*b);
    }
    subtle.setToggleState (true, juce::dontSendNotification);
    subtle.setTooltip ("Tunings, filters, envelopes, drift");
    moderate.setTooltip ("Also shapes, motion and modulation depths");
    wild.setTooltip ("Also new processors and modulations (within the budgets)");
    for (auto* t : { &lockScale, &lockRhythm, &lockRouting, &keepLevel })
        addAndMakeVisible (*t);
    lockScale.setToggleState (true, juce::dontSendNotification);
    keepLevel.setToggleState (true, juce::dontSendNotification);
    seedLabel.setFont (text (12.0f, 500));
    seedLabel.setColour (juce::Label::textColourId, colour::boneDim);
    seedLabel.setText ("SEED " + juce::String (static_cast<juce::int64> (seed)), juce::dontSendNotification);
    addAndMakeVisible (seedLabel);
    dice.onClick = [this] {
        seed = osp::Prng::splitMix64 (seed);
        seed %= 1000000;
        seedLabel.setText ("SEED " + juce::String (static_cast<juce::int64> (seed)), juce::dontSendNotification);
    };
    addAndMakeVisible (dice);
    go.onClick = [this] { run(); };
    go.setColour (juce::TextButton::buttonColourId, colour::ember.darker (0.2f));
    addAndMakeVisible (go);
    compare.setEnabled (false);
    compare.onClick = [this] {
        // A/B: the original is one undo away, the variation one redo away.
        if (showingOriginal ? session.redo() : session.undo())
            showingOriginal = ! showingOriginal;
        compare.setButtonText (showingOriginal ? "A (ORIGINAL)" : "B (EVOLVED)");
    };
    addAndMakeVisible (compare);
    keep.setEnabled (false);
    keep.onClick = [this] {
        if (showingOriginal)
            session.redo();
        showingOriginal = false;
        evolved = false;
        compare.setEnabled (false);
        keep.setEnabled (false);
        summary = "kept (one UNDO goes back)";
        repaint();
    };
    addAndMakeVisible (keep);
    setSize (380, 330);
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
    compare.setButtonText ("B (EVOLVED)");
    summary.clear();
    for (std::size_t i = 0; i < changes.size() && i < 6; ++i)
        summary << changes[i] << "\n";
    if (changes.size() > 6)
        summary << "... and " << static_cast<int> (changes.size() - 6) << " more";
    repaint();
}

void EvolvePanel::paint (juce::Graphics& g)
{
    g.fillAll (colour::cardHigh);
    g.setColour (colour::bone);
    g.setFont (identity (22.0f, true));
    g.drawText ("EVOLVE", 16, 10, 200, 28, juce::Justification::centredLeft);
    g.setColour (colour::boneDim);
    g.setFont (text (11.5f));
    g.drawText ("A seeded variation of this patch: one undo, always repeatable.", 16, 36, getWidth() - 32, 16, juce::Justification::centredLeft);
    g.setColour (colour::bone);
    g.setFont (text (11.0f));
    g.drawFittedText (summary, 16, 236, getWidth() - 32, 90, juce::Justification::topLeft, 7);
}

void EvolvePanel::resized()
{
    auto r = getLocalBounds().reduced (16).withTrimmedTop (48);
    auto row = r.removeFromTop (28);
    const int w = row.getWidth() / 3;
    subtle.setBounds (row.removeFromLeft (w).reduced (2, 0));
    moderate.setBounds (row.removeFromLeft (w).reduced (2, 0));
    wild.setBounds (row.reduced (2, 0));
    r.removeFromTop (8);
    auto locks = r.removeFromTop (52);
    lockScale.setBounds (locks.getX(), locks.getY(), locks.getWidth() / 2, 24);
    lockRhythm.setBounds (locks.getCentreX(), locks.getY(), locks.getWidth() / 2, 24);
    lockRouting.setBounds (locks.getX(), locks.getY() + 26, locks.getWidth() / 2, 24);
    keepLevel.setBounds (locks.getCentreX(), locks.getY() + 26, locks.getWidth() / 2, 24);
    r.removeFromTop (8);
    auto seedRow = r.removeFromTop (28);
    seedLabel.setBounds (seedRow.removeFromLeft (140));
    dice.setBounds (seedRow.removeFromRight (110));
    r.removeFromTop (8);
    auto buttons = r.removeFromTop (32);
    go.setBounds (buttons.removeFromLeft (110));
    buttons.removeFromLeft (8);
    compare.setBounds (buttons.removeFromLeft (120));
    buttons.removeFromLeft (8);
    keep.setBounds (buttons);
}

//==============================================================================
PlayView::PlayView (ModularProcessor& p) : processor (p), session (p.session())
{
    for (int i = 0; i < maxMacros; ++i)
    {
        macros.push_back (std::make_unique<MacroKnob> (p, i, true));
        addAndMakeVisible (*macros.back());
    }
    static const char* numerals[sceneCount] = { "I", "II", "III", "IV" };
    for (int s = 0; s < sceneCount; ++s)
    {
        auto& b = scenes[static_cast<std::size_t> (s)];
        b.setButtonText (numerals[s]);
        b.onClick = [this, s] { sceneClicked (s, storeArmed || juce::ModifierKeys::currentModifiers.isShiftDown()); };
        b.setTooltip ("Recall this scene (Shift-click or STORE first to capture the patch into it)");
        addAndMakeVisible (b);
    }
    store.setClickingTogglesState (true);
    store.onClick = [this] { storeArmed = store.getToggleState(); };
    store.setTooltip ("Then click a scene to store the patch's current values and macros in it");
    addAndMakeVisible (store);
    sceneFader.setSliderStyle (juce::Slider::LinearHorizontal);
    sceneFader.setTextBoxStyle (juce::Slider::NoTextBox, true, 0, 0);
    sceneFader.setTitle ("Scene morph");
    sceneFader.setTooltip ("Morph between the stored scenes (continuous values glide; modes switch at the middle)");
    sceneAttachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (p.parameters(), "scene", sceneFader);
    addAndMakeVisible (sceneFader);
    evolve.onClick = [this] {
        auto panel = std::make_unique<EvolvePanel> (session);
        juce::CallOutBox::launchAsynchronously (std::move (panel), evolve.getScreenBounds(), nullptr);
    };
    evolve.setTooltip ("A seeded variation of the patch (Cmd/Ctrl + E)");
    addAndMakeVisible (evolve);
    save.onClick = [this] {
        if (onSave)
            onSave();
    };
    addAndMakeVisible (save);
    patch.onClick = [this] {
        if (onPatch)
            onPatch();
    };
    patch.setTooltip ("Back to the patch (Tab)");
    addAndMakeVisible (patch);
    setTitle ("Play");
    startTimerHz (30);
}

void PlayView::sceneClicked (int index, bool storeIt)
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
        session.message ("scene " + juce::String (index + 1) + " is empty: STORE, then click it, to capture the patch now");
    refresh();
}

void PlayView::refresh()
{
    const auto& doc = session.document();
    for (auto& m : macros)
        m->refresh();
    for (int s = 0; s < sceneCount; ++s)
    {
        const auto& sc = doc.scenes[static_cast<std::size_t> (s)];
        auto& b = scenes[static_cast<std::size_t> (s)];
        static const char* numerals[sceneCount] = { "I", "II", "III", "IV" };
        b.setButtonText (sc.captured && ! sc.name.empty() ? juce::String (numerals[s]) + "  " + juce::String (sc.name) : juce::String (numerals[s]));
        b.setAlpha (sc.captured ? 1.0f : 0.5f);
        b.setToggleState (sc.captured && std::abs (processor.engine().scenePosition() - static_cast<float> (s)) < 0.05f, juce::dontSendNotification);
    }
    repaint();
}

void PlayView::timerCallback()
{
    processor.engine().copyScope (scope.data(), static_cast<int> (scope.size()));
    for (auto& m : macros)
        m->repaint();
    repaint (visual);
    static int n = 0;
    if ((++n % 10) == 0)
        refresh();
}

void PlayView::paint (juce::Graphics& g)
{
    g.fillAll (colour::peat);
    const auto& doc = session.document();
    // the instrument's name, in ANDOR's identity face
    auto h = header;
    g.setColour (colour::bone);
    g.setFont (identity (40.0f, true));
    g.drawText (juce::String (doc.name).toUpperCase(), h.removeFromTop (48), juce::Justification::centredLeft);
    g.setColour (colour::boneDim);
    g.setFont (text (13.0f));
    g.drawFittedText (doc.description, h, juce::Justification::topLeft, 2);

    // the visualisation: the output's own waveform, drawn as a soft line with its trace
    auto v = visual.toFloat();
    g.setColour (colour::card);
    g.fillRoundedRectangle (v, 10.0f);
    g.setColour (colour::line);
    g.drawRoundedRectangle (v.reduced (0.5f), 10.0f, 1.0f);
    g.setColour (colour::line.withAlpha (0.5f));
    g.drawHorizontalLine (static_cast<int> (v.getCentreY()), v.getX() + 16.0f, v.getRight() - 16.0f);
    // find a rising zero crossing to steady the trace
    int start = 0;
    for (int i = 1; i < 512; ++i)
        if (scope[static_cast<std::size_t> (i - 1)] <= 0.0f && scope[static_cast<std::size_t> (i)] > 0.0f)
        {
            start = i;
            break;
        }
    juce::Path trace;
    const int count = 512;
    float peak = 0.02f;
    for (int i = 0; i < count; ++i)
        peak = std::max (peak, std::abs (scope[static_cast<std::size_t> (start + i)]));
    const float scale = std::min (1.0f, 0.45f / peak) * v.getHeight();
    for (int i = 0; i < count; ++i)
    {
        const float x = v.getX() + 16.0f + (v.getWidth() - 32.0f) * static_cast<float> (i) / static_cast<float> (count - 1);
        const float y = v.getCentreY() - scope[static_cast<std::size_t> (start + i)] * scale;
        if (i == 0)
            trace.startNewSubPath (x, y);
        else
            trace.lineTo (x, y);
    }
    g.setColour (colour::ember.withAlpha (0.25f));
    g.strokePath (trace, juce::PathStrokeType (6.0f, juce::PathStrokeType::curved));
    g.setColour (colour::bone);
    g.strokePath (trace, juce::PathStrokeType (1.6f, juce::PathStrokeType::curved));
    // status: voices, level, CPU
    g.setFont (text (11.0f, 500));
    g.setColour (colour::boneDim);
    const auto status = juce::String (processor.engine().activeVoices()) + " VOICES     PEAK " + juce::String (juce::Decibels::gainToDecibels (processor.engine().outputPeak(), -60.0f), 1)
                        + " dB     CPU " + juce::String (juce::roundToInt (processor.cpuLoad() * 100.0f)) + " %";
    g.drawText (status, visual.reduced (16, 10), juce::Justification::topRight);

    // scene section label
    g.setColour (colour::boneFaint);
    g.setFont (text (10.0f, 600));
    g.drawText ("SCENES", sceneFader.getX(), sceneFader.getY() - 54, 120, 14, juce::Justification::centredLeft);
}

void PlayView::resized()
{
    auto r = getLocalBounds().reduced (32, 24);
    header = r.removeFromTop (88);
    auto bottom = r.removeFromBottom (64);
    auto buttons = bottom.removeFromRight (360);
    patch.setBounds (buttons.removeFromRight (110).withSizeKeepingCentre (110, 34));
    buttons.removeFromRight (8);
    save.setBounds (buttons.removeFromRight (100).withSizeKeepingCentre (100, 34));
    buttons.removeFromRight (8);
    evolve.setBounds (buttons.removeFromRight (120).withSizeKeepingCentre (120, 34));
    // scenes
    auto sceneArea = bottom.removeFromLeft (std::min (bottom.getWidth(), 560));
    auto keys = sceneArea.removeFromTop (30);
    store.setBounds (keys.removeFromRight (70));
    keys.removeFromRight (8);
    const int kw = keys.getWidth() / sceneCount;
    for (int s = 0; s < sceneCount; ++s)
        scenes[static_cast<std::size_t> (s)].setBounds (keys.removeFromLeft (kw).reduced (2, 0));
    sceneFader.setBounds (sceneArea.withTrimmedTop (6).withHeight (26));
    r.removeFromBottom (24);
    // macros: one row of eight, or two rows of four on narrow windows
    const bool twoRows = getWidth() < 1100 || session.document().view.panelLayout == 1;
    auto macroArea = r.removeFromBottom (twoRows ? 260 : 150);
    const int perRow = twoRows ? 4 : 8;
    const int mw = macroArea.getWidth() / perRow;
    for (int i = 0; i < maxMacros; ++i)
    {
        const int row = i / perRow, col = i % perRow;
        macros[static_cast<std::size_t> (i)]->setBounds (macroArea.getX() + col * mw, macroArea.getY() + row * (macroArea.getHeight() / (twoRows ? 2 : 1)), mw,
                                                        macroArea.getHeight() / (twoRows ? 2 : 1));
    }
    r.removeFromBottom (16);
    visual = r;
}

} // namespace andor::modular::ui
