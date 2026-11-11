#include "PluginEditor.h"

namespace andor::modular
{

using namespace ui;

Editor::Editor (ModularProcessor& p)
    : AudioProcessorEditor (p), processor (p), session (p.session()), canvas (p.session()), detail (p.session()), play (p), quickAdd (Registry::builtIn())
{
    setLookAndFeel (&lookAndFeel);
    juce::LookAndFeel::setDefaultLookAndFeel (&lookAndFeel);
    setWantsKeyboardFocus (true);
    setTitle ("ANDOR Modular");

    addAndMakeVisible (canvas);
    addChildComponent (detail);
    addChildComponent (play);
    addChildComponent (quickAdd);
    addChildComponent (toast);
    for (int i = 0; i < maxMacros; ++i)
    {
        strip.push_back (std::make_unique<MacroKnob> (p, i, false));
        addAndMakeVisible (*strip.back());
    }

    name.setEditable (false, true);
    name.setFont (text (15.0f, 600));
    name.setColour (juce::Label::textColourId, colour::bone);
    name.setTooltip ("Double-click to rename the instrument");
    name.onTextChange = [this] {
        const auto n = name.getText().toStdString();
        session.perform ("rename", [&] (Document& d) {
            d.name = n;
            edit::Result r;
            r.ok = true;
            return r;
        }, false);
    };
    addAndMakeVisible (name);
    for (auto* b : { &patches, &undo, &redo, &arrange, &fit, &patchTab, &playTab, &evolve })
        addAndMakeVisible (*b);
    patchTab.setClickingTogglesState (true);
    playTab.setClickingTogglesState (true);
    patchTab.setRadioGroupId (9);
    playTab.setRadioGroupId (9);
    patchTab.setToggleState (true, juce::dontSendNotification);
    patchTab.onClick = [this] { showPlay (false); };
    playTab.onClick = [this] { showPlay (true); };
    patches.onClick = [this] { showPatchesMenu(); };
    undo.onClick = [this] { session.undo(); };
    redo.onClick = [this] { session.redo(); };
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
    fit.onClick = [this] { canvas.fitAll(); };
    evolve.onClick = [this] { juce::CallOutBox::launchAsynchronously (std::make_unique<EvolvePanel> (session), evolve.getScreenBounds(), nullptr); };
    voices.addItem ("POLY 1", 1);
    voices.addItem ("POLY 2", 2);
    voices.addItem ("POLY 4", 4);
    voices.addItem ("POLY 8", 8);
    voices.addItem ("POLY 16", 16);
    voices.addItem ("MONO", 101);
    voices.addItem ("LEGATO", 102);
    voices.setTooltip ("Voices per independent chain (each note stream has its own)");
    voices.onChange = [this] {
        const int id = voices.getSelectedId();
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
    addAndMakeVisible (voices);

    canvas.onQuickAdd = [this] (const QuickAddContext& ctx, juce::Point<int> screen) { openQuickAdd (ctx, screen); };
    canvas.onOpenDetail = [this] (const std::string& id) { openDetail (id); };
    canvas.onSelectionChanged = [this] {
        if (detailShown)
        {
            const auto id = canvas.selection().size() == 1 ? *canvas.selection().begin() : std::string();
            if (! id.empty())
                detail.show (id);
        }
    };
    detail.onClose = [this] {
        detailShown = false;
        detail.setVisible (false);
        resized();
    };
    quickAdd.onChoose = [this] (const std::string& type) { choose (type); };
    quickAdd.onClose = [this] { canvas.grabKeyboardFocus(); };
    toast.onUndo = [this] { session.undo(); };
    play.onPatch = [this] { showPlay (false); };
    play.onSave = [this] { saveAs(); };

    session.addListener (this);
    setResizable (true, true);
    setResizeLimits (869, 560, 2600, 1700);
    setSize (1448, 868);
    showPlay (session.document().view.playView);
    updateBar();
    startTimerHz (10);
}

Editor::~Editor()
{
    session.removeListener (this);
    juce::LookAndFeel::setDefaultLookAndFeel (nullptr);
    setLookAndFeel (nullptr);
}

void Editor::showPlay (bool p)
{
    playShown = p;
    session.document().view.playView = p;
    play.setVisible (p);
    canvas.setVisible (! p);
    detail.setVisible (! p && detailShown);
    for (auto& k : strip)
        k->setVisible (! p);
    arrange.setVisible (! p);
    fit.setVisible (! p);
    patchTab.setToggleState (! p, juce::dontSendNotification);
    playTab.setToggleState (p, juce::dontSendNotification);
    if (p)
        play.refresh();
    resized();
}

void Editor::openDetail (const std::string& id)
{
    detailShown = true;
    detail.show (id);
    detail.setVisible (! playShown);
    resized();
}

void Editor::openQuickAdd (const QuickAddContext& ctx, juce::Point<int> screen)
{
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
    quickAdd.open (getLocalPoint (nullptr, screen), title, only);
}

void Editor::choose (const std::string& type)
{
    const auto ctx = pending;
    edit::Result r;
    switch (ctx.kind)
    {
        case QuickAddContext::Kind::cable:
            r = session.perform ("insert", [&] (Document& d) { return edit::insertOnConnection (d, session.registry(), ctx.connection, type); });
            break;
        case QuickAddContext::Kind::port:
            r = session.perform ("add", [&] (Document& d) {
                return edit::addConnectedTo (d, session.registry(), ctx.node, ctx.port, ctx.portIsOutput, type, ctx.world.x, ctx.world.y);
            });
            break;
        case QuickAddContext::Kind::selection:
            r = session.perform ("replace", [&] (Document& d) { return edit::replaceNode (d, session.registry(), ctx.node, type); });
            break;
        case QuickAddContext::Kind::canvas:
        default:
        {
            const auto selected = canvas.primarySelection();
            r = session.perform ("add " + juce::String (type), [&] (Document& d) { return edit::addModule (d, session.registry(), type, ctx.world.x, ctx.world.y, selected); });
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
    menu.addItem ("Save", [this] {
        if (session.currentFile() != juce::File())
            session.saveFile (session.currentFile());
        else
            saveAs();
    });
    menu.addItem ("Save as...", [this] { saveAs(); });
    menu.addSeparator();
    juce::PopupMenu factory;
    const auto& items = Session::factoryPatches();
    for (int i = 0; i < static_cast<int> (items.size()); ++i)
        factory.addItem (items[static_cast<std::size_t> (i)].name, [this, i] { session.loadFactory (i); });
    menu.addSubMenu ("Factory patches", factory);
    juce::PopupMenu modules;
    for (const auto& m : Session::factoryModules())
        modules.addItem (m.name, [this, text = m.text] {
            const auto at = canvas.toWorld (canvas.getLocalBounds().getCentre().toFloat());
            session.perform ("add module", [&] (Document& d) { return edit::importModule (d, session.registry(), text, at.x, at.y); });
        });
    const auto userModules = Session::userDirectory().getChildFile ("Modules").findChildFiles (juce::File::findFiles, false, "*.andormodule");
    if (! userModules.isEmpty())
        modules.addSeparator();
    for (const auto& f : userModules)
        modules.addItem (f.getFileNameWithoutExtension(), [this, f] {
            const auto text = f.loadFileAsString().toStdString();
            const auto at = canvas.toWorld (canvas.getLocalBounds().getCentre().toFloat());
            session.perform ("add module", [&] (Document& d) { return edit::importModule (d, session.registry(), text, at.x, at.y); });
        });
    menu.addSubMenu ("Add a module (group)", modules);
    menu.addSeparator();
    menu.addItem ("All notes off", [this] { processor.engine().allNotesOff(); });
    menu.showMenuAsync (juce::PopupMenu::Options().withTargetComponent (&patches));
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
        if (detailShown)
            detail.rebuild();
        play.refresh();
        for (auto& k : strip)
            k->refresh();
    }
    else
    {
        canvas.refreshValues();
        detail.refreshValues();
        play.refresh();
        for (auto& k : strip)
            k->refresh();
    }
    updateBar();
}

void Editor::sessionMessage (const juce::String& text, bool canUndo, bool isError)
{
    toast.show (text, canUndo, isError);
    resized();
}

void Editor::updateBar()
{
    const auto& d = session.document();
    name.setText (d.name, juce::dontSendNotification);
    undo.setEnabled (session.history().canUndo());
    redo.setEnabled (session.history().canRedo());
    undo.setTooltip (session.history().canUndo() ? "Undo " + juce::String (session.history().undoLabel()) + " (Cmd/Ctrl + Z)" : juce::String ("Nothing to undo"));
    redo.setTooltip (session.history().canRedo() ? "Redo " + juce::String (session.history().redoLabel()) : juce::String ("Nothing to redo"));
    voices.setSelectedId (d.settings.voiceMode > 0 ? 100 + d.settings.voiceMode : d.settings.polyphony, juce::dontSendNotification);
    repaint (bar);
}

void Editor::timerCallback() { repaint (status); }

void Editor::paint (juce::Graphics& g)
{
    g.fillAll (colour::peat);
    g.setColour (colour::cardHigh);
    g.fillRect (bar);
    g.setColour (colour::line);
    g.drawHorizontalLine (bar.getBottom() - 1, 0.0f, static_cast<float> (getWidth()));
    drawWordmark (g, bar.withWidth (getWidth() < 1200 ? 150 : 190).reduced (16, getWidth() < 1200 ? 17 : 14).toFloat());
    // status: voices, CPU, compile time, the patch's budget and any problems
    const auto& rep = session.report();
    g.setFont (text (10.5f, 500));
    juce::String s;
    s << processor.engine().activeVoices() << " VOICES   CPU " << juce::roundToInt (processor.cpuLoad() * 100.0f) << "%   " << rep.sources << "/6 SOURCES   "
      << rep.nodes << "/32 MODULES";
    int problems = 0;
    for (const auto& i : rep.issues)
        problems += i.severity == Issue::Severity::error ? 1 : 0;
    g.setColour (problems > 0 ? colour::error : colour::boneDim);
    if (problems > 0)
        s << "   " << problems << " PROBLEM" << (problems > 1 ? "S" : "");
    g.drawText (s, status, juce::Justification::centredRight);
    if (! playShown)
    {
        g.setColour (colour::card);
        g.fillRect (stripArea);
        g.setColour (colour::line);
        g.drawHorizontalLine (stripArea.getY(), 0.0f, static_cast<float> (getWidth()));
        if (getWidth() >= 1200)
        {
            g.setColour (colour::boneFaint);
            g.setFont (text (9.5f, 600));
            g.drawText ("PERFORMANCE PANEL", stripArea.withWidth (150).reduced (16, 0), juce::Justification::centredLeft);
        }
    }
}

void Editor::resized()
{
    auto r = getLocalBounds();
    bar = r.removeFromTop (space::topBar);
    // The bar steps down gracefully: the status first, then the labels' room.
    const bool narrow = getWidth() < 1200;
    auto b = bar.reduced (12, 12);
    b.removeFromLeft (narrow ? 150 : 190);
    patches.setBounds (b.removeFromLeft (narrow ? 80 : 92));
    b.removeFromLeft (8);
    name.setVisible (getWidth() >= 960);   // (the name stays on PLAY)
    if (name.isVisible())
    {
        name.setBounds (b.removeFromLeft (narrow ? 96 : std::min (240, getWidth() / 7)));
        b.removeFromLeft (8);
    }
    undo.setBounds (b.removeFromLeft (narrow ? 52 : 64));
    b.removeFromLeft (4);
    redo.setBounds (b.removeFromLeft (narrow ? 52 : 64));
    b.removeFromLeft (narrow ? 8 : 12);
    patchTab.setBounds (b.removeFromLeft (narrow ? 64 : 76));
    playTab.setBounds (b.removeFromLeft (narrow ? 64 : 76));
    b.removeFromLeft (narrow ? 8 : 12);
    evolve.setBounds (b.removeFromLeft (narrow ? 70 : 80));
    b.removeFromLeft (8);
    voices.setBounds (b.removeFromLeft (narrow ? 88 : 100));
    b.removeFromLeft (8);
    if (! playShown)
    {
        arrange.setBounds (b.removeFromLeft (narrow ? 72 : 80));
        b.removeFromLeft (4);
        fit.setBounds (b.removeFromLeft (44));
    }
    status = b.withTrimmedLeft (8);

    if (playShown)
    {
        play.setBounds (r);
    }
    else
    {
        stripArea = r.removeFromBottom (78);
        auto s = stripArea.withTrimmedLeft (narrow ? 16 : 160).withTrimmedRight (16);
        const int w = std::min (110, s.getWidth() / maxMacros);
        for (auto& k : strip)
            k->setBounds (s.removeFromLeft (w).reduced (4, 6));
        if (detailShown)
            detail.setBounds (r.removeFromRight (narrow ? 300 : space::detailWidth));
        canvas.setBounds (r);
    }
    toast.setBounds (getLocalBounds().withSizeKeepingCentre (std::min (560, getWidth() - 48), 40).withY (getHeight() - (playShown ? 180 : 132)));
}

bool Editor::keyPressed (const juce::KeyPress& k)
{
    const auto mods = k.getModifiers();
    if (mods.isCommandDown() && k.getKeyCode() == 'Z')
    {
        mods.isShiftDown() ? session.redo() : session.undo();
        return true;
    }
    if (mods.isCommandDown() && k.getKeyCode() == 'K')
    {
        QuickAddContext ctx;
        ctx.world = canvas.toWorld (canvas.getLocalBounds().getCentre().toFloat());
        openQuickAdd (ctx, canvas.localPointToGlobal (canvas.getLocalBounds().getCentre().translated (-190, -200)));
        return true;
    }
    if (mods.isCommandDown() && k.getKeyCode() == 'S')
    {
        if (session.currentFile() != juce::File())
            session.saveFile (session.currentFile());
        else
            saveAs();
        return true;
    }
    if (mods.isCommandDown() && k.getKeyCode() == 'E')
    {
        evolve.triggerClick();
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
