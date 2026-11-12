#include "PluginEditor.h"

namespace andor::modular
{

using namespace ui;

namespace
{
    juce::Rectangle<int> at (juce::Rectangle<float> r) { return r.toNearestInt(); }
}

Editor::Editor (ModularProcessor& p)
    : AudioProcessorEditor (p),
      processor (p),
      session (p.session()),
      volume (p.session()),
      canvas (p.session()),
      rack (p.session()),
      play (p),
      scenes (p),
      pitchWheel ([&p] (float bend) {
          MidiMessage m;
          m.kind = MidiMessage::Kind::pitchBend;
          m.value = bend;
          p.playFromInterface (m);
      }),
      keyboard (p),
      quickAdd (Registry::builtIn())
{
    setLookAndFeel (&lookAndFeel);
    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
    setWantsKeyboardFocus (true);
    setTitle ("ANDOR Modular");
    setOpaque (true);
    addAndMakeVisible (surface);

    // Header.
    presetBar.onMenu = [this] { showPatchesMenu(); };
    presetBar.onPrevious = [this] { stepFactory (-1); };
    presetBar.onNext = [this] { stepFactory (1); };
    saveKey.setIcon (icon::Kind::cassette);
    saveKey.setTooltip ("Save the patch (Cmd/Ctrl + S)");
    saveKey.setTitle ("Save");
    saveKey.onClick = [this] { save(); };
    patchKey.setIcon (icon::Kind::patch);
    patchKey.setTooltip ("PATCH: the modules and their cables (Ctrl + Tab)");
    patchKey.setTitle ("Patch");
    patchKey.onClick = [this] { showPlay (false); };
    playKey.setIcon (icon::Kind::performance);
    playKey.setTooltip ("PLAY: the front panel: the sources, the macros, the scenes (Ctrl + Tab)");
    playKey.setTitle ("Play");
    playKey.onClick = [this] { showPlay (true); };
    menuButton.setIcon (icon::Kind::dots);
    menuButton.setInk (colour::text.withAlpha (0.75f));
    menuButton.setTooltip ("More");
    menuButton.setTitle ("Menu");
    menuButton.onClick = [this] { showMainMenu(); };
    for (auto* c : std::initializer_list<juce::Component*> { &presetBar, &saveKey, &patchKey, &playKey, &volume, &menuButton })
        surface.addAndMakeVisible (*c);
    // Only while a source is soloed: the one place that ends every solo.
    clearSolo.setIcon (icon::Kind::close);
    clearSolo.setInk (colour::accent.darker (0.2f));
    clearSolo.setTooltip ("A source is soloed: click to hear every source again");
    clearSolo.setTitle ("Clear solo");
    clearSolo.onClick = [this] { session.clearSolo(); };
    surface.addChildComponent (clearSolo);

    // The board.
    addModule.setIcon (icon::Kind::plus);
    addModule.setTooltip ("Add a module (Cmd/Ctrl + K, or right-click the board)");
    addModule.onClick = [this] { addModuleFromHeader(); };
    arrange.setIcon (icon::Kind::arrange);
    arrange.setTooltip ("Lay the patch out left to right (signal flow)");
    arrange.onClick = [this] {
        session.perform ("arrange", [this] (Document& d) {
            edit::autoArrange (d, session.registry());
            edit::Result r;
            r.ok = true;
            r.message = "arranged left to right";
            return r;
        });
        canvas.fitAll();
    };
    fit.setIcon (icon::Kind::fit);
    fit.setTooltip ("Fit patch");
    fit.setTitle ("Fit patch");
    fit.onClick = [this] { canvas.fitAll(); };
    undo.setIcon (icon::Kind::undo);
    undo.setTitle ("Undo");
    undo.onClick = [this] { session.undo(); };
    redo.setIcon (icon::Kind::redo);
    redo.setTitle ("Redo");
    redo.onClick = [this] { session.redo(); };
    surface.addAndMakeVisible (canvas);
    surface.addChildComponent (rack);
    surface.addChildComponent (play);
    for (auto* b : { &addModule, &arrange, &fit, &undo, &redo })
        surface.addAndMakeVisible (*b);

    // The macro panel.
    for (int i = 0; i < maxMacros; ++i)
    {
        macros.push_back (std::make_unique<MacroKnob> (p, i));
        surface.addChildComponent (*macros.back());
    }
    surface.addChildComponent (scenes);

    // The keyboard row.
    voices.onClick = [this] { showVoicesMenu(); };
    evolveKey.setIcon (icon::Kind::chevron, true);
    evolveKey.setFontHeight (14.0f);
    evolveKey.setTooltip ("EVOLVE: a seeded variation of the patch (Cmd/Ctrl + E)");
    evolveKey.onClick = [this] { showEvolve (evolvePanel == nullptr); };
    status.onUndo = [this] { session.undo(); };
    for (auto* c : std::initializer_list<juce::Component*> { &pitchWheel, &keyboard, &voices, &evolveKey, &status })
        surface.addAndMakeVisible (*c);
    surface.addChildComponent (quickAdd);

    canvas.onQuickAdd = [this] (const QuickAddContext& ctx, juce::Point<int> screen) { openQuickAdd (ctx, screen); };
    canvas.onOpenDetail = [this] (const std::string& id) { openRack (id); };
    canvas.onEscape = [this] { return dismissTopmost(); };
    rack.onClose = [this] { openRack ({}); };
    play.onEdit = [this] (const std::string& id) {
        showPlay (false);
        canvas.select (id, false);
        openRack (id);
    };
    play.onAddSource = [this] {
        showPlay (false);
        addModuleFromHeader();
    };
    quickAdd.onChoose = [this] (const std::string& type) { choose (type); };
    quickAdd.onClose = [this] { canvas.grabKeyboardFocus(); };
    quickAdd.onEscape = [this] { dismissTopmost(); };
    loadPreferences();

    session.addListener (this);
    // One instrument, scaled as one piece: the window keeps the family's shape.
    setResizable (true, true);
    getConstrainer()->setFixedAspectRatio (static_cast<double> (layout::width) / static_cast<double> (layout::height));
    setResizeLimits (869, 652, 1810, 1358);
    setSize (1280, 960);
    showPlay (session.document().view.playView);
    updateHeader();
    updateStatus();
    startTimerHz (10);
}

Editor::~Editor()
{
    session.removeListener (this);
    if (&juce::LookAndFeel::getDefaultLookAndFeel() == &lookAndFeel)
        juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

juce::Rectangle<float> Editor::boardArea() const
{
    // PATCH: the board takes the band down to the macro panel's foot (a patch needs the room);
    // PLAY: the family's sources band, the macro panel under it.
    return playShown ? layout::board : layout::board.withBottom (layout::macroPanel.getBottom());
}

void Editor::showPlay (bool p)
{
    playShown = p;
    session.document().view.playView = p;
    if (p)
        quickAdd.close();
    patchKey.setToggleState (! p, juce::dontSendNotification);
    playKey.setToggleState (p, juce::dontSendNotification);
    if (p)
    {
        play.refresh();
        scenes.refresh();
    }
    layoutSurface();
}

void Editor::openRack (const std::string& id)
{
    // One rack at a time: another module's swaps it in place. The board keeps its zoom and its
    // place; it only moves (by the least it takes) if the rack would cover the module.
    rack.show (id);
    layoutSurface();
    if (rack.isOpen())
        canvas.ensureVisible (rack.shown());
}

bool Editor::dismissTopmost()
{
    if (! playShown && canvas.cancelGesture())
        return true;
    if (quickAdd.isOpen())
    {
        if (! quickAdd.closeSubmenu())
            quickAdd.close();
        return true;
    }
    if (evolvePanel != nullptr)
    {
        showEvolve (false);
        return true;
    }
    if (playShown)
        return false;
    if (rack.isOpen())
    {
        openRack ({});
        canvas.grabKeyboardFocus();
        return true;
    }
    if (canvas.leaveGroup())
        return true;
    if (! canvas.selection().empty())
    {
        canvas.clearSelection();
        return true;
    }
    return false;
}

namespace
{
    juce::File preferencesFile() { return Session::userDirectory().getChildFile ("Preferences.xml"); }
}

void Editor::loadPreferences()
{
    // The interface's own preferences (never a patch's, never the host's state).
    if (const auto xml = juce::parseXML (preferencesFile()))
        Canvas::traditionalScroll = xml->getStringAttribute ("scroll") == "traditional";
}

void Editor::savePreferences()
{
    juce::XmlElement xml ("AndorModularPreferences");
    xml.setAttribute ("scroll", Canvas::traditionalScroll ? "traditional" : "natural");
    const auto file = preferencesFile();
    file.getParentDirectory().createDirectory();
    xml.writeTo (file);
}

void Editor::showEvolve (bool show)
{
    if (! show)
    {
        evolvePanel.reset();
        evolveKey.setToggleState (false, juce::dontSendNotification);
        skin::repaintKey (evolveKey);
        return;
    }
    evolvePanel = std::make_unique<EvolvePanel> (session);
    evolvePanel->onClose = [this] { juce::MessageManager::callAsync ([safe = juce::Component::SafePointer<Editor> (this)] {
                                        if (safe != nullptr)
                                            safe->showEvolve (false);
                                    }); };
    surface.addAndMakeVisible (*evolvePanel);
    // Over the instrument, its foot above the keyboard row's cards (the key that opened it
    // stays in sight, lit).
    const auto card = at (layout::voicesCard);
    evolvePanel->setTopLeftPosition (card.getRight() - EvolvePanel::cardWidth - EvolvePanel::margin, card.getY() - 10 - EvolvePanel::cardHeight - EvolvePanel::margin);
    evolvePanel->toFront (true);
    evolveKey.setToggleState (true, juce::dontSendNotification);
    skin::repaintKey (evolveKey);
}

void Editor::openQuickAdd (const QuickAddContext& ctx, juce::Point<int> screen)
{
    if (playShown)
        showPlay (false);
    pending = ctx;
    std::vector<const NodeDesc*> only;
    juce::String title;
    if (ctx.kind == QuickAddContext::Kind::port)
    {
        only = edit::compatibleTypes (session.document(), session.registry(), ctx.node, ctx.port, ctx.portIsOutput);
        title = "connect to " + juce::String (ctx.port);
    }
    else if (ctx.kind == QuickAddContext::Kind::cable)
        title = "insert on this cable";
    else if (ctx.kind == QuickAddContext::Kind::selection && ctx.port == "replace")
        title = "replace with";
    else if (! canvas.primarySelection().empty())
        title = "add after the selection";
    if (ctx.kind == QuickAddContext::Kind::port && only.empty())
    {
        session.message ("nothing connects to that port here", false, true);
        return;
    }
    quickAdd.open (surface.getLocalPoint (nullptr, screen), title, only);
}

void Editor::addModuleFromHeader()
{
    QuickAddContext ctx;
    ctx.world = canvas.toWorld (canvas.getLocalBounds().getCentre().toFloat());
    // under the button (the menu keeps itself inside the instrument)
    openQuickAdd (ctx, surface.localPointToGlobal (addModule.getBounds().getBottomLeft().translated (0, 6)));
}

void Editor::choose (const std::string& type)
{
    const auto ctx = pending;
    edit::Result r;
    // A new source that plays a recording starts with one of ANDOR's own, so it sounds at once
    // (an empty one is silent until something is dropped on it): the same transaction.
    auto sounding = [] (Document& d, edit::Result made) {
        static const std::pair<const char*, const char*> starters[] = { { "src.organic", "samples/bowed-c3.wav" },
                                                                        { "src.granular", "samples/rain-texture.wav" },
                                                                        { "src.rhythm", "samples/clockwork-120.wav" } };
        if (made.ok)
            if (auto* n = d.findNode (made.focus))
                for (const auto& [sourceType, key] : starters)
                    if (n->type == sourceType && n->strings["sample"].empty())
                    {
                        n->strings["sample"] = key;
                        made.message += " with ANDOR's own recording: drop yours on it";
                    }
        return made;
    };
    switch (ctx.kind)
    {
        case QuickAddContext::Kind::cable:
            r = session.perform ("insert", [&] (Document& d) { return sounding (d, edit::insertOnConnection (d, session.registry(), ctx.connection, type)); });
            break;
        case QuickAddContext::Kind::port:
            r = session.perform ("add", [&] (Document& d) {
                return sounding (d, edit::addConnectedTo (d, session.registry(), ctx.node, ctx.port, ctx.portIsOutput, type, ctx.world.x, ctx.world.y));
            });
            break;
        case QuickAddContext::Kind::selection:
            r = session.perform ("replace", [&] (Document& d) { return sounding (d, edit::replaceNode (d, session.registry(), ctx.node, type)); });
            break;
        case QuickAddContext::Kind::canvas:
        default:
        {
            const auto selected = canvas.primarySelection();
            r = session.perform ("add " + juce::String (type), [&] (Document& d) { return sounding (d, edit::addModule (d, session.registry(), type, ctx.world.x, ctx.world.y, selected)); });
            if (! r.choices.empty())
            {
                // Several places it could go: a small placement chooser, never a guess.
                juce::PopupMenu menu;
                menu.addSectionHeader ("Insert " + juce::String (session.registry().find (type)->name) + " on...");
                for (const auto& cid : r.choices)
                {
                    const auto* c = session.document().findConnection (cid);
                    const auto* to = c != nullptr ? session.document().findNode (c->to) : nullptr;
                    if (to == nullptr)
                        continue;
                    menu.addItem ("the cable to " + juce::String (session.document().displayName (session.registry(), *to)), [this, cid, type] {
                        session.perform ("insert", [&] (Document& d) { return edit::insertOnConnection (d, session.registry(), cid, type); });
                    });
                }
                menu.showMenuAsync (juce::PopupMenu::Options().withMousePosition());
            }
            break;
        }
    }
    if (r.ok && ! r.focus.empty() && session.document().findNode (r.focus) != nullptr)
        canvas.select (r.focus, false);
    canvas.grabKeyboardFocus();
}

void Editor::stepFactory (int delta)
{
    const auto& items = Session::factoryPatches();
    if (items.empty())
        return;
    const int count = static_cast<int> (items.size());
    int current = -1;
    for (int i = 0; i < count; ++i)
        if (items[static_cast<std::size_t> (i)].name == juce::String (session.document().name))
            current = i;
    if (current < 0)
        current = delta > 0 ? -1 : 0;
    session.loadFactory (((current + delta) % count + count) % count);
}

void Editor::showPatchesMenu()
{
    juce::PopupMenu menu;
    menu.addItem ("New (empty)", [this] { session.newPatch(); });
    menu.addItem ("Open...", [this] {
        auto chooser = std::make_shared<juce::FileChooser> ("Open a patch", Session::userDirectory().getChildFile ("Patches"), "*.andormodular");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles, [this, chooser] (const juce::FileChooser& fc) {
            if (fc.getResult() != juce::File())
                session.loadFile (fc.getResult());
        });
    });
    menu.addItem ("Save", [this] { save(); });
    menu.addItem ("Save as...", [this] { saveAs(); });
    menu.addItem ("Rename...", [this] {
        auto* w = new juce::AlertWindow ("The instrument's name", {}, juce::MessageBoxIconType::NoIcon, this);
        w->addTextEditor ("name", juce::String (session.document().name));
        w->addButton ("RENAME", 1, juce::KeyPress (juce::KeyPress::returnKey));
        w->addButton ("CANCEL", 0, juce::KeyPress (juce::KeyPress::escapeKey));
        w->enterModalState (true, juce::ModalCallbackFunction::create ([this, w] (int result) {
                                if (result != 1)
                                    return;
                                const auto n = w->getTextEditorContents ("name").trim().toStdString();
                                session.perform ("rename", [&] (Document& d) {
                                    d.name = n;
                                    edit::Result r;
                                    r.ok = ! n.empty();
                                    return r;
                                }, false);
                            }),
                            true);
    });
    menu.addSeparator();
    menu.addSectionHeader ("FACTORY PATCHES");
    const auto& items = Session::factoryPatches();
    for (int i = 0; i < static_cast<int> (items.size()); ++i)
        menu.addItem (items[static_cast<std::size_t> (i)].name, true, items[static_cast<std::size_t> (i)].name == juce::String (session.document().name),
                      [this, i] { session.loadFactory (i); });
    menu.addSeparator();
    juce::PopupMenu modules;
    for (const auto& m : Session::factoryModules())
        modules.addItem (m.name, [this, text = m.text] {
            const auto where = canvas.toWorld (canvas.getLocalBounds().getCentre().toFloat());
            session.perform ("add module", [&] (Document& d) { return edit::importModule (d, session.registry(), text, where.x, where.y); });
        });
    const auto userModules = Session::userDirectory().getChildFile ("Modules").findChildFiles (juce::File::findFiles, false, "*.andormodule");
    if (! userModules.isEmpty())
        modules.addSeparator();
    for (const auto& f : userModules)
        modules.addItem (f.getFileNameWithoutExtension(), [this, f] {
            const auto text = f.loadFileAsString().toStdString();
            const auto where = canvas.toWorld (canvas.getLocalBounds().getCentre().toFloat());
            session.perform ("add module", [&] (Document& d) { return edit::importModule (d, session.registry(), text, where.x, where.y); });
        });
    menu.addSubMenu ("Add a module (group)", modules);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&presetBar));
}

void Editor::showMainMenu()
{
    juce::PopupMenu menu;
    menu.addItem ("Undo" + (session.history().canUndo() ? "  " + juce::String (session.history().undoLabel()) : juce::String()), session.history().canUndo(), false,
                  [this] { session.undo(); });
    menu.addItem ("Redo" + (session.history().canRedo() ? "  " + juce::String (session.history().redoLabel()) : juce::String()), session.history().canRedo(), false,
                  [this] { session.redo(); });
    menu.addSeparator();
    menu.addItem ("Add a module...", [this] { addModuleFromHeader(); });
    menu.addItem ("Evolve...", [this] { showEvolve (true); });
    menu.addSeparator();
    menu.addItem ("All notes off", [this] { processor.engine().allNotesOff(); });
    menu.addSeparator();
    juce::PopupMenu size;
    for (const int width : { 869, 1086, 1280, 1448, 1810 })
        size.addItem (juce::String (juce::roundToInt (100.0 * width / 1448.0)) + " %", true, std::abs (getWidth() - width) < 3,
                      [this, width] { setSize (width, juce::roundToInt (static_cast<float> (width) * layout::height / layout::width)); });
    menu.addSubMenu ("Size", size);
    // The board's scrolling: with the gesture as the system delivers it, or the other way.
    juce::PopupMenu scroll;
    auto setScroll = [this] (bool traditional) {
        Canvas::traditionalScroll = traditional;
        savePreferences();
    };
    scroll.addItem ("Natural (the board follows the gesture)", true, ! Canvas::traditionalScroll, [setScroll] { setScroll (false); });
    scroll.addItem ("Traditional (reversed)", true, Canvas::traditionalScroll, [setScroll] { setScroll (true); });
    menu.addSubMenu ("Board scrolling", scroll);
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&menuButton));
}

void Editor::showVoicesMenu()
{
    const auto& settings = session.document().settings;
    juce::PopupMenu menu;
    menu.addSectionHeader ("VOICES (for each independent chain)");
    auto choose = [this] (int id) {
        session.perform ("voices", [id] (Document& d) {
            if (id > 100)
            {
                d.settings.voiceMode = id - 100;
                if (d.settings.glideSeconds <= 0.0)
                    d.settings.glideSeconds = 0.06;
            }
            else
            {
                d.settings.voiceMode = 0;
                d.settings.polyphony = id;
            }
            edit::Result r;
            r.ok = true;
            r.message = id > 100 ? (id == 101 ? "mono" : "legato") : std::to_string (id) + " voices";
            return r;
        });
    };
    for (const int n : { 1, 2, 4, 8, 16 })
        menu.addItem ("Poly " + juce::String (n), true, settings.voiceMode == 0 && settings.polyphony == n, [choose, n] { choose (n); });
    menu.addSeparator();
    menu.addItem ("Mono", true, settings.voiceMode == 1, [choose] { choose (101); });
    menu.addItem ("Legato", true, settings.voiceMode == 2, [choose] { choose (102); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&voices));
}

void Editor::save()
{
    if (session.currentFile() != juce::File())
        session.saveFile (session.currentFile());
    else
        saveAs();
}

void Editor::saveAs()
{
    auto dir = Session::userDirectory().getChildFile ("Patches");
    dir.createDirectory();
    auto chooser = std::make_shared<juce::FileChooser> ("Save the patch", dir.getChildFile (juce::String (session.document().name) + ".andormodular"), "*.andormodular");
    chooser->launchAsync (juce::FileBrowserComponent::saveMode | juce::FileBrowserComponent::canSelectFiles, [this, chooser] (const juce::FileChooser& fc) {
        if (fc.getResult() != juce::File())
            session.saveFile (fc.getResult().withFileExtension ("andormodular"));
    });
}

void Editor::sessionChanged (bool structural)
{
    if (structural)
    {
        canvas.rebuild();
        if (rack.isOpen())
        {
            rack.rebuild();
            if (! rack.isOpen())
                layoutSurface();   // what it showed is gone (another patch, a deleted module): the rack closes
        }
    }
    else
    {
        canvas.refreshValues();
        rack.refreshValues();
    }
    play.refresh();
    scenes.refresh();
    for (auto& k : macros)
        k->refresh();
    volume.refresh();
    updateHeader();
    updateStatus();
}

void Editor::sessionMessage (const juce::String& text, bool canUndo, bool isError)
{
    // A sentence, as the instrument says it.
    status.show (text.substring (0, 1).toUpperCase() + text.substring (1), canUndo, isError);
}

void Editor::updateHeader()
{
    const auto& d = session.document();
    presetBar.setPatchName (juce::String (d.name), session.history().canUndo());
    undo.setEnabled (session.history().canUndo());
    redo.setEnabled (session.history().canRedo());
    undo.setTooltip (session.history().canUndo() ? "Undo " + juce::String (session.history().undoLabel()) + " (Cmd/Ctrl + Z)" : juce::String ("Nothing to undo"));
    redo.setTooltip (session.history().canRedo() ? "Redo " + juce::String (session.history().redoLabel()) + " (Shift + Cmd/Ctrl + Z)" : juce::String ("Nothing to redo"));
    clearSolo.setVisible (session.anySolo());
    const bool mono = d.settings.voiceMode > 0;
    voices.setText (mono ? (d.settings.voiceMode == 1 ? "MONO" : "LEGATO") : "POLY", mono ? juce::String ("1 VOICE") : juce::String (d.settings.polyphony) + (d.settings.polyphony == 1 ? " VOICE" : " VOICES"));
    surface.repaint (at (boardArea().withHeight (layout::boardHeader)));
}

void Editor::updateStatus()
{
    const auto& rep = session.report();
    int problems = 0;
    for (const auto& i : rep.issues)
        problems += i.severity == Issue::Severity::error ? 1 : 0;
    juce::String s;
    const int sounding = processor.engine().activeVoices();
    s << sounding << (sounding == 1 ? " voice" : " voices") << "      CPU " << juce::roundToInt (processor.cpuLoad() * 100.0f) << " %      " << rep.nodes << " of 32 modules      "
      << rep.sources << " of 6 sources";
    if (problems > 0)
        s << "      " << problems << (problems > 1 ? " problems" : " problem");
    status.setIdleText (s);
    voices.setSounding (sounding > 0);
}

void Editor::timerCallback() { updateStatus(); }

//==============================================================================
void Editor::paint (juce::Graphics& g)
{
    // (only seen where a host gives the window another shape than the instrument's)
    g.fillAll (draw::surround());
}

void Editor::paintSurface (juce::Graphics& g)
{
    // InstrumentFrame.cpp, frame::paint: the housing (ANDOR Analog's walnut cheeks from its
    // pack), the identity, the keyboard's bed, the macro panel, then the ceramic keys.
    draw::housing (g, surface.getLocalBounds().toFloat());
    {
        const auto bold = fonts::make (60.0f, Weight::displayBold).withHorizontalScale (1.07f);
        const auto light = fonts::make (60.0f, Weight::displayLight).withHorizontalScale (1.07f);
        const auto logo = design::layout::logo;
        juce::GlyphArrangement mark, rest;
        mark.addLineOfText (bold, "ANDOR", logo.x, logo.y);
        const float w = mark.getBoundingBox (0, -1, true).getRight() - logo.x + 1.0f;
        rest.addLineOfText (light, "/MODULAR", logo.x + w, logo.y);
        g.setColour (colour::text);
        mark.draw (g);
        rest.draw (g);
        g.setFont (fonts::make (15.0f, Weight::regular, 0.16f));
        g.setColour (colour::textSecondary);
        g.drawText ("Invent an instrument", design::layout::logoSubtitle.withHeight (17.0f), juce::Justification::centredLeft, false);
    }

    // The band: the patch board, or the front panel's source cards.
    const auto board = boardArea();
    if (! playShown)
    {
        paintBoard (g, board, design::layout::panelRadius);
        const auto header = board.withHeight (layout::boardHeader);
        g.setColour (board::ink);
        g.setFont (type::panelHeader());
        g.drawText ("PATCH", header.withTrimmedLeft (24.0f).withWidth (80.0f), juce::Justification::centredLeft, false);
        const auto& rep = session.report();
        g.setColour (board::weak.darker (0.2f));
        g.setFont (fonts::make (11.5f, Weight::medium, 0.1f));
        g.drawText (juce::String (rep.nodes) + (rep.nodes == 1 ? " MODULE" : " MODULES") + "      " + juce::String (static_cast<int> (session.document().connections.size()))
                        + " CABLES",
                    header.withTrimmedLeft (104.0f).withWidth (300.0f).translated (0.0f, 1.0f), juce::Justification::centredLeft, false);
        g.setColour (board::rule);
        g.fillRect (juce::Rectangle<float> (board.getX() + 14.0f, header.getBottom(), board.getWidth() - 28.0f, 1.0f));
        // between the board's tools and its history: a short rule
        g.fillRect (juce::Rectangle<float> (static_cast<float> (undo.getX()) - 9.0f, header.getY() + 14.0f, 1.0f, header.getHeight() - 28.0f));
    }
    else
    {
        for (const auto& card : play.cardAreas())
            draw::raised (g, card.translated (play.getX(), play.getY()).toFloat(), design::layout::cardRadius, colour::cardTop, colour::cardBottom);
        // The macros and the scenes share the family's one panel.
        const auto panel = layout::macroPanel;
        draw::raised (g, panel, design::layout::panelRadius, colour::panelTop, colour::panelBottom);
        g.setColour (colour::divider);
        g.fillRect (juce::Rectangle<float> (layout::sceneDividerX, 695.0f, 1.0f, 190.0f));
    }

    // The keyboard's bed (rendered: the keys sit in a recess cut into the housing).
    const auto keys = layout::keyboard;
    bool bed = false;
    if (auto* pieces = skin::active())
        bed = pieces->draw (g, "keybed", juce::Rectangle<float> (keys.getX() - 2.0f, keys.getY() - 3.0f, keys.getWidth() + 4.0f, keys.getHeight() + 5.0f));
    if (! bed)
    {
        juce::Path frame;
        frame.addRoundedRectangle (keys.expanded (2.0f), 6.0f);
        design::CachedShadow (juce::Colour (0x22302418), 3, { 0, 1 }).drawForPath (g, frame);
        g.setColour (juce::Colour (0xffd9d0c3));
        g.fillPath (frame);
        g.setColour (juce::Colour (0xffc4bbad));
        g.strokePath (frame, juce::PathStrokeType (1.0f));
    }
    skin::drawKeys (g, surface);
}

void Editor::resized()
{
    const float scale = std::min (static_cast<float> (getWidth()) / layout::width, static_cast<float> (getHeight()) / layout::height);
    surface.setBounds (0, 0, juce::roundToInt (layout::width), juce::roundToInt (layout::height));
    surface.setTransform (juce::AffineTransform::scale (scale).translated (0.5f * (static_cast<float> (getWidth()) - layout::width * scale),
                                                                             0.5f * (static_cast<float> (getHeight()) - layout::height * scale)));
    layoutSurface();
}

void Editor::layoutSurface()
{
    presetBar.setBounds (at (layout::presetBar));
    saveKey.setBounds (at (layout::saveKey));
    patchKey.setBounds (at (layout::viewKey));
    playKey.setBounds (at (layout::viewKey.translated (48.0f, 0.0f)));
    volume.setBounds (at (layout::volume));
    menuButton.setBounds (at (layout::menu));
    clearSolo.setBounds (juce::roundToInt (layout::viewKey.getRight() + 54.0f), juce::roundToInt (layout::viewKey.getY() + 9.0f), clearSolo.idealWidth(), 30);

    const auto board = at (boardArea());
    const int headerH = juce::roundToInt (layout::boardHeader);
    for (auto* b : { &addModule, &arrange, &fit, &undo, &redo })
        b->setVisible (! playShown);
    canvas.setVisible (! playShown);
    rack.setVisible (! playShown && rack.isOpen());
    play.setVisible (playShown);
    for (auto& k : macros)
        k->setVisible (playShown);
    scenes.setVisible (playShown);
    if (playShown)
    {
        play.setBounds (board);
        for (int i = 0; i < maxMacros; ++i)
            macros[static_cast<std::size_t> (i)]->setBounds (juce::roundToInt (layout::macroFirstX + layout::macroPitch * static_cast<float> (i) - 66.0f), 700, 132, 186);
        scenes.setBounds (at (layout::scenes));
    }
    else
    {
        // The board's header: its tools on the right (ink only, the routing board's way).
        auto tools = board.withHeight (headerH).withTrimmedRight (14).reduced (0, 6);
        redo.setBounds (tools.removeFromRight (32));
        undo.setBounds (tools.removeFromRight (32));
        tools.removeFromRight (16);
        fit.setBounds (tools.removeFromRight (fit.idealWidth()));
        arrange.setBounds (tools.removeFromRight (arrange.idealWidth()));
        addModule.setBounds (tools.removeFromRight (addModule.idealWidth()));
        // The canvas is the middle of the board's own face (see Canvas::setBoardArea).
        auto inner = board.reduced (2, 0).withTrimmedTop (headerH + 1).withTrimmedBottom (7);
        // A module's rack is a drawer along the board's foot: the canvas keeps the whole width
        // (and, as its origin is its top-left, its place: nothing under the pointer moves).
        if (rack.isOpen())
            rack.setBounds (inner.removeFromBottom (Rack::height));
        canvas.setBounds (inner);
        canvas.setBoardArea (boardArea().translated (static_cast<float> (-inner.getX()), static_cast<float> (-inner.getY())));
    }

    auto wheel = at (layout::pitchWheel);
    pitchWheel.setBounds (wheel.withHeight (wheel.getHeight() + 20));
    keyboard.setBounds (at (layout::keyboard));
    keyboard.setKeyWidth (layout::keyboard.getWidth() / 52.0f);   // 88 keys = 52 white keys
    voices.setBounds (at (layout::voicesCard));
    evolveKey.setBounds (at (layout::evolveKey));
    status.setBounds (at (layout::status));
    surface.repaint();
}

bool Editor::keyPressed (const juce::KeyPress& k)
{
    const auto mods = k.getModifiers();
    if (k == juce::KeyPress::escapeKey)
        return dismissTopmost();
    if (mods.isCommandDown() && k.getKeyCode() == 'Z')
    {
        mods.isShiftDown() ? session.redo() : session.undo();
        return true;
    }
    if (mods.isCommandDown() && k.getKeyCode() == 'K')
    {
        addModuleFromHeader();
        return true;
    }
    if (mods.isCommandDown() && k.getKeyCode() == 'S')
    {
        save();
        return true;
    }
    if (mods.isCommandDown() && k.getKeyCode() == 'E')
    {
        showEvolve (evolvePanel == nullptr);
        return true;
    }
    if (k == juce::KeyPress::tabKey && mods.isCtrlDown())
    {
        showPlay (! playShown);
        return true;
    }
    return false;
}

} // namespace andor::modular
