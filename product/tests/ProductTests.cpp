// ANDOR Modular product tests: the processor as a host drives it, its state, its editor and
// the musician's first gestures through the real interface. MODULAR_SCREENSHOT_DIR=dir also
// writes the reference screenshots (869 / 1448 / 1810 wide).

#include "PluginEditor.h"
#include "PluginProcessor.h"

#include "ModularMaterialsData.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_session.hpp>
#include <catch2/catch_test_macros.hpp>

#include <juce_gui_basics/juce_gui_basics.h>

using namespace andor::modular;

namespace
{
    /** Plays `seconds` with `midi` at the start; returns the left channel. */
    std::vector<float> run (ModularProcessor& p, double seconds, std::vector<juce::MidiMessage> atStart = {}, int block = 512)
    {
        std::vector<float> out;
        juce::AudioBuffer<float> buffer (2, block);
        const int blocks = static_cast<int> (seconds * 48000.0 / block);
        for (int b = 0; b < blocks; ++b)
        {
            juce::MidiBuffer midi;
            if (b == 0)
                for (const auto& m : atStart)
                    midi.addEvent (m, 0);
            buffer.clear();
            p.processBlock (buffer, midi);
            out.insert (out.end(), buffer.getReadPointer (0), buffer.getReadPointer (0) + block);
            juce::MessageManager::getInstance()->runDispatchLoopUntil (1);
        }
        return out;
    }

    double rms (const std::vector<float>& x, std::size_t from = 0)
    {
        double s = 0.0;
        for (std::size_t i = from; i < x.size(); ++i)
            s += static_cast<double> (x[i]) * x[i];
        return x.size() > from ? std::sqrt (s / static_cast<double> (x.size() - from)) : 0.0;
    }

    void snapshot (juce::Component& c, const juce::String& name)
    {
        const auto dir = juce::SystemStats::getEnvironmentVariable ("MODULAR_SCREENSHOT_DIR", {});
        if (dir.isEmpty())
            return;
        const auto image = c.createComponentSnapshot (c.getLocalBounds(), true, 1.0f);
        juce::File (dir).createDirectory();
        juce::FileOutputStream out (juce::File (dir).getChildFile (name + ".png"));
        out.setPosition (0);
        out.truncate();
        juce::PNGImageFormat().writeImageToStream (image, out);
    }

    void settle (int ms = 60) { juce::MessageManager::getInstance()->runDispatchLoopUntil (ms); }

    /** The processor with its editor open, as in a host. */
    struct Instrument
    {
        Instrument()
        {
            processor.prepareToPlay (48000.0, 512);
            base.reset (processor.createEditor());
            editor = dynamic_cast<Editor*> (base.get());
            // (preferences belong to the musician's machine: tests start from the default)
            ui::Canvas::traditionalScroll = false;
        }
        Session& session() { return processor.session(); }
        bool loadFactory (const juce::String& name)
        {
            const auto& items = Session::factoryPatches();
            for (int i = 0; i < static_cast<int> (items.size()); ++i)
                if (items[static_cast<std::size_t> (i)].name == name)
                    return session().loadFactory (i);
            return false;
        }
        std::string firstOfType (const std::string& type)
        {
            for (const auto& n : session().document().nodes)
                if (n.type == type)
                    return n.id;
            return {};
        }
        ModularProcessor processor;
        std::unique_ptr<juce::AudioProcessorEditor> base;
        Editor* editor = nullptr;
    };

    juce::MouseEvent mouseAt (juce::Component& c, juce::Point<float> p, juce::ModifierKeys mods = juce::ModifierKeys::leftButtonModifier)
    {
        const auto now = juce::Time::getCurrentTime();
        return { juce::Desktop::getInstance().getMainMouseSource(), p, mods, 0.0f, 0.0f, 0.0f, 0.0f, 0.0f, &c, &c, now, p, now, 1, false };
    }

    /** The patch without where the board is looked at from (pan, zoom, the group shown: saved
        with it, but never an edit). */
    std::string patchOnly (Document doc)
    {
        doc.view = {};
        return saveToString (doc);
    }

    /** Every pair of tiles on the board (the group being shown) that touch. */
    int overlappingTiles (const Document& doc, const Registry& reg)
    {
        std::vector<edit::TileRect> rects;
        for (const auto& n : doc.nodes)
            if (n.group == doc.view.currentGroup)
                rects.push_back (edit::tileRect (doc, reg, n.id));
        for (const auto& g : doc.groups)
            if (g.parent == doc.view.currentGroup)
                rects.push_back (edit::tileRect (doc, reg, g.id));
        int count = 0;
        for (std::size_t a = 0; a < rects.size(); ++a)
            for (std::size_t b = a + 1; b < rects.size(); ++b)
                if (rects[a].x < rects[b].x + rects[b].w && rects[b].x < rects[a].x + rects[a].w && rects[a].y < rects[b].y + rects[b].h && rects[b].y < rects[a].y + rects[a].h)
                    ++count;
        return count;
    }
}

TEST_CASE ("The processor plays its factory patch and every factory patch", "[product]")
{
    ModularProcessor p;
    p.prepareToPlay (48000.0, 512);
    const auto out = run (p, 1.0, { juce::MidiMessage::noteOn (1, 48, 0.8f), juce::MidiMessage::noteOn (1, 55, 0.8f) });
    CHECK (rms (out, 4800) > 0.01);
    CHECK (p.getLatencySamples() == maxChunk);   // the engine's grid
    for (int i = 0; i < static_cast<int> (Session::factoryPatches().size()); ++i)
    {
        INFO (Session::factoryPatches()[static_cast<std::size_t> (i)].name);
        REQUIRE (p.session().loadFactory (i));
        // Recordings load in the background: wait for them like a musician would.
        p.session().assets().waitIdle (20.0);
        settle (50);
        const auto o = run (p, 1.5, { juce::MidiMessage::noteOn (1, 48, 0.8f), juce::MidiMessage::noteOn (1, 60, 0.8f) });
        CHECK (rms (o, 9600) > 0.003);
    }
    CHECK (Session::factoryPatches().size() == 12);
    CHECK (Session::factoryModules().size() == 6);
}

TEST_CASE ("Host parameters are a fixed bank with stable ids", "[product]")
{
    ModularProcessor p;
    juce::StringArray ids;
    for (auto* param : p.getParameters())
        if (auto* withId = dynamic_cast<juce::AudioProcessorParameterWithID*> (param))
            ids.add (withId->paramID);
    REQUIRE (ids.size() == maxMacros + 1 + hostBankSize);
    CHECK (ids[0] == "macro1");
    CHECK (ids[8] == "scene");
    CHECK (ids[9] == "assign01");
    CHECK (ids[40] == "assign32");
}

TEST_CASE ("Session state: save and restore give the same patch and the same sound", "[product][state]")
{
    juce::MemoryBlock state;
    std::string patch;
    {
        ModularProcessor p;
        p.prepareToPlay (48000.0, 256);
        REQUIRE (p.session().loadFactory (0));
        p.setMacro (0, 0.8f);
        settle();
        p.getStateInformation (state);
        patch = saveToString (p.session().document());
    }
    ModularProcessor q;
    q.prepareToPlay (48000.0, 256);
    q.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    settle();
    CHECK (saveToString (q.session().document()) == patch);
    CHECK (q.parameters().getRawParameterValue ("macro1")->load() == 0.8f);
    // Unreadable state is refused without harm.
    const char junk[] = "not a state";
    q.setStateInformation (junk, sizeof junk);
    CHECK (saveToString (q.session().document()) == patch);
}

TEST_CASE ("0-to-sound through the interface: empty patch, Quick Add 'analog', Return, play", "[product][ui]")
{
    ModularProcessor p;
    p.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor());
    auto* editor = dynamic_cast<Editor*> (base.get());
    REQUIRE (editor != nullptr);
    p.session().newPatch();
    settle();
    CHECK (p.session().document().nodes.empty());
    const auto started = juce::Time::getMillisecondCounterHiRes();
    ui::QuickAddContext ctx;
    ctx.world = { 200.0f, 200.0f };
    editor->openQuickAdd (ctx, editor->getScreenBounds().getCentre());
    editor->quickAddView().setQuery ("analog");
    const auto shown = editor->quickAddView().visibleTypes();
    REQUIRE_FALSE (shown.empty());
    CHECK (shown.front() == "osc.analog");
    editor->quickAddView().chooseSelected();
    const double ms = juce::Time::getMillisecondCounterHiRes() - started;
    INFO ("Quick Add to a compiled, playable graph: " << ms << " ms");
    CHECK (ms < 100.0);
    CHECK (p.session().document().nodes.size() == 5);
    const auto out = run (p, 0.6, { juce::MidiMessage::noteOn (1, 57, 0.9f) });
    CHECK (rms (out, 4800) > 0.01);
    // and a filter inserted on the selection, undone in one step
    REQUIRE_FALSE (editor->canvasView().selection().empty());
    const auto before = saveToString (p.session().document());
    editor->quickAddView().setQuery ("filter");
    editor->openQuickAdd (ctx, editor->getScreenBounds().getCentre());
    editor->quickAddView().setQuery ("filter");
    editor->quickAddView().chooseSelected();
    CHECK (p.session().document().nodes.size() == 6);
    p.session().undo();
    CHECK (saveToString (p.session().document()) == before);
}

TEST_CASE ("A recording whose path is not ASCII opens, plays and comes back from the saved session", "[product][organic][live]")
{
    // (A guard, not the cause of the reported silence: this JUCE reads a std::string as UTF-8.)
    const std::string name = "Str\xc3\xb8k \xe2\x80\x93 caf\xc3\xa9.wav";   // "Strøk – café.wav"
    const auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile (juce::String ("andor-modular-\xc3\xa6\xc3\xb8\xc3\xa5"));
    dir.createDirectory();
    const auto file = dir.getChildFile (juce::String (name));
    Session::installFactoryContent();
    REQUIRE (Session::factoryDirectory().getChildFile ("samples/bowed-c3.wav").copyFileTo (file));

    juce::MemoryBlock state;
    {
        ModularProcessor p;
        p.prepareToPlay (48000.0, 512);
        p.session().newPatch();
        const auto made = p.session().perform ("add", [&] (Document& d) { return edit::addModule (d, p.session().registry(), "src.organic", 0, 0); });
        REQUIRE (made.ok);
        p.session().setNodeString (made.focus, "sample", p.session().keyFor (file));
        p.session().assets().waitIdle (20.0);
        settle (80);
        const auto asset = p.session().assets().resolve (p.session().resolveKey (p.session().document().findNode (made.focus)->strings.at ("sample")).getFullPathName().toStdString());
        REQUIRE (asset != nullptr);
        CHECK (asset->status == SampleAsset::Status::ready);
        const auto out = run (p, 1.0, { juce::MidiMessage::noteOn (1, 48, 0.8f) });
        CHECK (rms (out, 9600) > 0.005);
        // a patch name beyond ASCII too
        p.session().perform ("rename", [] (Document& d) {
            d.name = "R\xc3\xaaverie";
            edit::Result r;
            r.ok = true;
            return r;
        }, false);
        p.getStateInformation (state);
    }
    ModularProcessor q;
    q.prepareToPlay (48000.0, 512);
    q.setStateInformation (state.getData(), static_cast<int> (state.getSize()));
    q.session().assets().waitIdle (20.0);
    settle (80);
    CHECK (q.session().document().name == "R\xc3\xaaverie");
    const auto again = run (q, 1.0, { juce::MidiMessage::noteOn (1, 48, 0.8f) });
    CHECK (rms (again, 9600) > 0.005);
    dir.deleteRecursively();
}

TEST_CASE ("A sample source added from Quick Add sounds at once; M, S and the light never rebuild the graph", "[product][source][mute][solo]")
{
    ModularProcessor p;
    p.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor());
    auto* editor = dynamic_cast<Editor*> (base.get());
    REQUIRE (editor != nullptr);
    p.session().newPatch();
    settle();
    ui::QuickAddContext ctx;
    ctx.world = { 200.0f, 200.0f };
    editor->openQuickAdd (ctx, editor->getScreenBounds().getCentre());
    editor->quickAddView().setQuery ("organic");
    editor->quickAddView().chooseSelected();
    p.session().assets().waitIdle (20.0);
    settle (80);
    std::string organic;
    for (const auto& n : p.session().document().nodes)
        if (n.type == "src.organic")
            organic = n.id;
    REQUIRE_FALSE (organic.empty());
    CHECK (p.session().document().findNode (organic)->strings.at ("sample") == "samples/bowed-c3.wav");
    auto out = run (p, 0.6, { juce::MidiMessage::noteOn (1, 48, 0.9f) });
    CHECK (rms (out, 9600) > 0.005);

    // M: silent, the same graph, the note still held; M again: back
    const int swaps = p.engine().swapsCompleted();
    p.session().setMuted (organic, true);
    out = run (p, 0.3);
    CHECK (rms (out, 7200) < 1.0e-5);
    p.session().setMuted (organic, false);
    out = run (p, 0.3);
    CHECK (rms (out, 7200) > 0.005);
    // S on the only source, then an oscillator beside it: soloed, the oscillator adds nothing
    p.session().toggleSolo (organic, false);
    CHECK (p.session().anySolo());
    out = run (p, 0.3);
    CHECK (rms (out, 7200) > 0.005);
    p.session().toggleSolo (organic, false);   // the same key again: no solo
    CHECK_FALSE (p.session().anySolo());
    // the light of a processor: bypassed and back, live
    std::string envelope;
    for (const auto& n : p.session().document().nodes)
        if (n.type == "proc.vca")
            envelope = n.id;
    REQUIRE_FALSE (envelope.empty());
    p.session().setEnabled (envelope, false);
    out = run (p, 0.2);
    p.session().setEnabled (envelope, true);
    out = run (p, 0.2);
    CHECK (p.engine().swapsCompleted() == swaps);
    CHECK (p.engine().activeVoices() == 1);
    // each is one undo step
    const auto before = saveToString (p.session().document());
    p.session().setMuted (organic, true);
    CHECK (p.session().document().findNode (organic)->muted);
    p.session().undo();
    CHECK (saveToString (p.session().document()) == before);
}

TEST_CASE ("Quick Add is a compact menu: categories, a submenu beside it, search, one module for each Return", "[product][ui][quickadd]")
{
    Instrument in;
    REQUIRE (in.editor != nullptr);
    in.editor->setSize (1448, 1086);
    REQUIRE (in.loadFactory ("Analog Organic Morph"));
    settle();
    auto& menu = in.editor->quickAddView();
    ui::QuickAddContext ctx;
    ctx.world = { 900.0f, 40.0f };
    const auto opening = juce::Time::getMillisecondCounterHiRes();
    in.editor->openQuickAdd (ctx, in.editor->getScreenBounds().getCentre());
    const double openMs = juce::Time::getMillisecondCounterHiRes() - opening;
    REQUIRE (menu.isOpen());
    INFO ("Quick Add open (laid out, ready to paint): " << openMs << " ms");
    CHECK (openMs < 50.0);
    std::printf ("quick add: open in %.2f ms\n", openMs);

    // The root: narrow, short, the six categories in their order, at most three recent modules.
    const auto names = menu.categories();
    REQUIRE (names.size() == 6);
    CHECK (names[0] == "Sources");
    CHECK (names[1] == "Processors");
    CHECK (names[2] == "Controllers");
    CHECK (names[3] == "Spatial");
    CHECK (names[4] == "Effects");
    CHECK (names[5] == "Utilities");
    CHECK (menu.rootBounds().getWidth() >= 246);
    CHECK (menu.rootBounds().getWidth() <= 292);
    CHECK (menu.rootBounds().getHeight() <= 400);   // never the catalogue of every module
    CHECK (menu.visibleTypes().size() <= 3);
    CHECK (menu.submenuBounds().isEmpty());
    CHECK (in.editor->getLocalBounds().contains (menu.rootBounds()));

    // A category opens its modules beside the root, inside the instrument; every module is in
    // exactly one category.
    std::size_t total = 0;
    for (const auto& name : names)
    {
        menu.openCategory (name);
        CHECK (menu.openCategoryName() == name);
        const auto sub = menu.submenuBounds();
        CHECK_FALSE (sub.isEmpty());
        CHECK_FALSE (sub.intersects (menu.rootBounds()));
        CHECK (menu.getLocalBounds().contains (sub));
        const auto types = menu.visibleTypes();
        CHECK_FALSE (types.empty());
        for (const auto& t : types)
            CHECK (ui::QuickAdd::categoryOf (*Registry::builtIn().find (t)) == name);
        total += types.size();
    }
    CHECK (total == Registry::builtIn().all().size() - 1);   // (every module but OUTPUT, which a patch has once)

    // Typing: the root becomes the best matches (a handful), the submenu goes.
    menu.setQuery ("fil");
    CHECK (menu.submenuBounds().isEmpty());
    auto found = menu.visibleTypes();
    REQUIRE_FALSE (found.empty());
    CHECK (found.size() <= 8);
    CHECK (found.front() == "proc.filter");
    menu.setQuery ("zzzz");
    CHECK (menu.visibleTypes().empty());
    menu.setQuery ({});
    CHECK (menu.visibleTypes().size() <= 3);

    // The keyboard: Down to a category, Right into it, Left back, Right, Return: one module.
    const auto nodesBefore = in.session().document().nodes.size();
    int guard = 0;
    while (menu.openCategoryName() != "Controllers" && guard++ < 12)
    {
        menu.pressKey (juce::KeyPress (juce::KeyPress::downKey));
        menu.pressKey (juce::KeyPress (juce::KeyPress::rightKey));
        if (menu.openCategoryName() != "Controllers")
            menu.pressKey (juce::KeyPress (juce::KeyPress::leftKey));
    }
    REQUIRE (menu.openCategoryName() == "Controllers");
    menu.pressKey (juce::KeyPress (juce::KeyPress::leftKey));
    CHECK (menu.openCategoryName().isEmpty());
    CHECK (menu.isOpen());
    menu.pressKey (juce::KeyPress (juce::KeyPress::rightKey));
    const auto offered = menu.visibleTypes();
    REQUIRE_FALSE (offered.empty());
    const int swaps = in.processor.engine().swapsCompleted();
    menu.pressKey (juce::KeyPress (juce::KeyPress::returnKey));
    CHECK_FALSE (menu.isOpen());
    CHECK (in.session().document().nodes.size() == nodesBefore + 1);
    menu.pressKey (juce::KeyPress (juce::KeyPress::returnKey));   // a second Return: the menu is gone, nothing more
    CHECK (in.session().document().nodes.size() == nodesBefore + 1);
    CHECK (in.session().document().nodes.back().type == offered.front());
    CHECK (overlappingTiles (in.session().document(), in.session().registry()) == 0);
    run (in.processor, 0.1);
    CHECK (in.processor.engine().swapsCompleted() <= swaps + 1);
    in.session().undo();
    CHECK (in.session().document().nodes.size() == nodesBefore);
    // ...and it is the first of the recent modules next time
    in.editor->openQuickAdd (ctx, in.editor->getScreenBounds().getCentre());
    REQUIRE_FALSE (menu.visibleTypes().empty());
    CHECK (menu.visibleTypes().front() == offered.front());
    menu.close();

    // On a port: only what connects there.
    const auto filter = in.firstOfType ("proc.filter");
    REQUIRE_FALSE (filter.empty());
    ui::QuickAddContext port;
    port.kind = ui::QuickAddContext::Kind::port;
    port.node = filter;
    port.port = "env";
    port.portIsOutput = false;
    in.editor->openQuickAdd (port, in.editor->getScreenBounds().getCentre());
    REQUIRE (menu.isOpen());
    std::size_t offeredOnPort = 0;
    for (const auto& name : menu.categories())
    {
        menu.openCategory (name);
        offeredOnPort += menu.visibleTypes().size();
    }
    CHECK (offeredOnPort > 0);
    CHECK (offeredOnPort < total);
    menu.close();
}

TEST_CASE ("Escape closes one thing at a time, the topmost first, and never touches the history", "[product][ui][escape]")
{
    Instrument in;
    REQUIRE (in.editor != nullptr);
    in.editor->setSize (1448, 1086);
    REQUIRE (in.loadFactory ("Analog Organic Morph"));
    settle();
    auto& editor = *in.editor;
    auto& canvas = editor.canvasView();
    const auto filter = in.firstOfType ("proc.filter");
    REQUIRE_FALSE (filter.empty());
    const auto patch = patchOnly (in.session().document());
    const bool couldUndo = in.session().history().canUndo();

    // Build the whole stack: a selection, the rack, EVOLVE, Quick Add with a submenu, a drag.
    canvas.select (filter, false);
    editor.openRack (filter);
    editor.showEvolve (true);
    ui::QuickAddContext ctx;
    editor.openQuickAdd (ctx, editor.getScreenBounds().getCentre());
    editor.quickAddView().openCategory ("Sources");
    REQUIRE_FALSE (editor.quickAddView().submenuBounds().isEmpty());

    // 1: Quick Add's submenu
    CHECK (editor.dismissTopmost());
    CHECK (editor.quickAddView().isOpen());
    CHECK (editor.quickAddView().submenuBounds().isEmpty());
    // 2: Quick Add
    CHECK (editor.dismissTopmost());
    CHECK_FALSE (editor.quickAddView().isOpen());
    CHECK (editor.isEvolveShown());
    // 3: EVOLVE
    CHECK (editor.dismissTopmost());
    CHECK_FALSE (editor.isEvolveShown());
    CHECK (editor.rackView().isOpen());
    // a drag under way (a selection box on the empty board) goes first, and only it
    canvas.mouseDown (mouseAt (canvas, { 6.0f, static_cast<float> (canvas.getHeight()) - 6.0f }, juce::ModifierKeys::leftButtonModifier | juce::ModifierKeys::shiftModifier));
    CHECK (editor.dismissTopmost());
    CHECK (editor.rackView().isOpen());
    CHECK (canvas.selection().count (filter) == 1);
    // 4: the rack
    CHECK (editor.dismissTopmost());
    CHECK_FALSE (editor.rackView().isOpen());
    CHECK (canvas.selection().count (filter) == 1);
    // 5: the selection
    CHECK (editor.dismissTopmost());
    CHECK (canvas.selection().empty());
    // nothing left
    CHECK_FALSE (editor.dismissTopmost());
    CHECK (patchOnly (in.session().document()) == patch);
    CHECK (in.session().history().canUndo() == couldUndo);

    // The same through the keys: Escape typed in Quick Add's search line, and on the board.
    canvas.select (filter, false);
    editor.openRack (filter);
    editor.openQuickAdd (ctx, editor.getScreenBounds().getCentre());
    editor.quickAddView().openCategory ("Effects");
    editor.quickAddView().pressKey (juce::KeyPress (juce::KeyPress::escapeKey));
    CHECK (editor.quickAddView().isOpen());
    CHECK (editor.quickAddView().submenuBounds().isEmpty());
    editor.quickAddView().pressKey (juce::KeyPress (juce::KeyPress::escapeKey));
    CHECK_FALSE (editor.quickAddView().isOpen());
    CHECK (editor.rackView().isOpen());
    CHECK (canvas.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
    CHECK_FALSE (editor.rackView().isOpen());
    CHECK (canvas.selection().count (filter) == 1);
    CHECK (canvas.keyPressed (juce::KeyPress (juce::KeyPress::escapeKey)));
    CHECK (canvas.selection().empty());

    // Inside a group: Escape leaves it (after anything open in it), one level.
    in.session().perform ("group", [&] (Document& d) { return edit::group (d, in.session().registry(), { filter }, "G"); });
    REQUIRE_FALSE (in.session().document().groups.empty());
    const auto afterGroup = patchOnly (in.session().document());
    const auto undoLabel = in.session().history().undoLabel();
    canvas.enterGroup (in.session().document().groups.front().id);
    CHECK_FALSE (in.session().document().view.currentGroup.empty());
    CHECK (editor.dismissTopmost());
    CHECK (in.session().document().view.currentGroup.empty());
    CHECK (patchOnly (in.session().document()) == afterGroup);
    CHECK (in.session().history().undoLabel() == undoLabel);
}

TEST_CASE ("The rack: one module's controls across the board's foot; the board keeps its width and its place", "[product][ui][rack]")
{
    Instrument in;
    REQUIRE (in.editor != nullptr);
    in.editor->setSize (1448, 1086);
    REQUIRE (in.loadFactory ("Analog Organic Morph"));
    settle();
    auto& editor = *in.editor;
    auto& canvas = editor.canvasView();
    auto& rack = editor.rackView();
    canvas.fitAll();
    const auto closedBounds = canvas.getBounds();
    const float zoom = canvas.zoomLevel();
    CHECK_FALSE (rack.isOpen());
    CHECK_FALSE (rack.isVisible());

    const auto filter = in.firstOfType ("proc.filter"), osc = in.firstOfType ("osc.analog");
    REQUIRE_FALSE (filter.empty());
    REQUIRE_FALSE (osc.empty());
    const int swapsBefore = in.processor.engine().swapsCompleted();
    const auto opening = juce::Time::getMillisecondCounterHiRes();
    editor.openRack (filter);
    const double openMs = juce::Time::getMillisecondCounterHiRes() - opening;
    std::printf ("rack: open in %.2f ms\n", openMs);
    CHECK (openMs < 50.0);
    REQUIRE (rack.isOpen());
    CHECK (rack.isVisible());
    CHECK (rack.shown() == filter);
    // (opening, swapping and closing a rack never touch the running graph)
    run (in.processor, 0.05);
    CHECK (in.processor.engine().swapsCompleted() == swapsBefore);
    // full width, under the canvas, above the keyboard; the canvas only gave up height
    CHECK (rack.getWidth() == closedBounds.getWidth());
    CHECK (canvas.getWidth() == closedBounds.getWidth());
    CHECK (canvas.getX() == closedBounds.getX());
    CHECK (canvas.getY() == closedBounds.getY());
    CHECK (rack.getY() == canvas.getBottom());
    CHECK (rack.getBottom() == closedBounds.getBottom());
    CHECK (rack.getHeight() >= 210);
    CHECK (rack.getHeight() <= 290);
    CHECK (canvas.zoomLevel() == zoom);
    // identity left, controls centre, live display right
    CHECK (rack.identityArea().getRight() < rack.controlsArea().getX());
    CHECK (rack.controlsArea().getRight() < rack.displayArea().getX());
    CHECK (rack.displayArea().getWidth() > 150);
    // every control of the module in its current mode
    {
        const auto* n = in.session().document().findNode (filter);
        const auto* d = in.session().registry().find (n->type);
        int shown = 0;
        for (const auto& prm : d->params)
            shown += d->paramShown (prm, n->params) ? 1 : 0;
        CHECK (rack.controlCount() == shown);
        CHECK (shown > 0);
    }
    // the module it shows stays in sight above it
    for (int i = 0; i < canvas.getNumChildComponents(); ++i)
        if (auto* tile = dynamic_cast<ui::NodeView*> (canvas.getChildComponent (i)); tile != nullptr && tile->id() == filter)
            CHECK (canvas.getLocalBounds().expanded (2).contains (tile->getBoundsInParent()));

    // another module: swapped in place (never two)
    const auto origin = canvas.viewOrigin();
    editor.openRack (osc);
    CHECK (rack.shown() == osc);
    CHECK (rack.getBounds().getHeight() == rack.wantedHeight());
    CHECK (canvas.zoomLevel() == zoom);
    juce::ignoreUnused (origin);

    // a value changed in the rack is the patch's value (the same parameter as on the tile)
    const auto before = in.session().document().paramValue (in.session().registry(), filter, "cutoff");
    in.session().beginGesture ("cutoff");
    in.session().setParam (filter, "cutoff", before * 0.5);
    CHECK (in.session().document().paramValue (in.session().registry(), filter, "cutoff") != before);
    in.session().undo();

    // deleting the module closes its rack; the canvas takes its room back
    editor.openRack (filter);
    const auto bounds = canvas.getBounds();
    CHECK (bounds.getHeight() < closedBounds.getHeight());
    editor.openRack ({});
    CHECK_FALSE (rack.isOpen());
    CHECK (canvas.getBounds() == closedBounds);
    editor.openRack (filter);
    in.session().perform ("delete", [&] (Document& d) {
        d.removeNode (filter);
        edit::Result r;
        r.ok = true;
        return r;
    });
    CHECK_FALSE (rack.isOpen());
    CHECK (canvas.getBounds() == closedBounds);
    in.session().undo();

    // PLAY hides it; a source card's EDIT comes back to PATCH with that source's rack
    editor.openRack (osc);
    editor.showPlay (true);
    CHECK_FALSE (rack.isVisible());
    editor.showPlay (false);
    CHECK (rack.isVisible());
    CHECK (rack.shown() == osc);
}

TEST_CASE ("ECHO opens ANDOR's own ECHO rack, bound to that one module: two echoes never share a control", "[product][ui][rack][native]")
{
    Instrument in;
    REQUIRE (in.editor != nullptr);
    in.editor->setSize (1448, 1086);
    auto& s = in.session();
    s.newPatch();
    const auto osc = s.perform ("add", [&] (Document& d) { return edit::addModule (d, s.registry(), "osc.analog", 0, 0); }).focus;
    REQUIRE_FALSE (osc.empty());
    const auto mix = in.firstOfType ("util.voicemix");
    REQUIRE_FALSE (mix.empty());
    const auto first = s.perform ("add", [&] (Document& d) { return edit::addModule (d, s.registry(), "fx.echo", 600, 0, mix); }).focus;
    const auto second = s.perform ("add", [&] (Document& d) { return edit::addModule (d, s.registry(), "fx.echo", 800, 0, first); }).focus;
    REQUIRE_FALSE (first.empty());
    REQUIRE_FALSE (second.empty());
    REQUIRE (first != second);
    settle();
    auto& editor = *in.editor;
    auto& rack = editor.rackView();
    const auto value = [&] (const std::string& node, const char* param) { return s.document().paramValue (s.registry(), node, param); };
    s.beginGesture ("set");
    s.setParam (second, "feedback", 0.2);
    s.setParam (second, "type", 1.0);   // BBD

    run (in.processor, 0.1);
    const int swaps = in.processor.engine().swapsCompleted();
    const auto opening = juce::Time::getMillisecondCounterHiRes();
    editor.openRack (first);
    std::printf ("echo rack: open in %.2f ms\n", juce::Time::getMillisecondCounterHiRes() - opening);
    REQUIRE (rack.isOpen());
    REQUIRE (rack.isNative());
    auto* module = rack.module();
    REQUIRE (module != nullptr);
    CHECK (module->title() == "ECHO");
    CHECK (module->getHeight() >= 204);
    CHECK (rack.getHeight() >= 210);
    CHECK (rack.getHeight() <= 290);
    auto* host = rack.binding();
    REQUIRE (host != nullptr);
    CHECK (host->nodeId() == first);
    // the rack shows this module
    CHECK (host->parameterValue ("echo.feedback") == Catch::Approx (100.0 * value (first, "feedback")).margin (0.01));
    CHECK (juce::roundToInt (host->parameterValue ("echo.type")) == juce::roundToInt (value (first, "type")));
    CHECK (host->parameterValue ("echo") == Catch::Approx (100.0 * value (first, "mix")).margin (0.01));

    // a control of the rack is that module's parameter, and only that module's; one undo step
    const auto before = saveToString (s.document());
    host->setParameterValue ("echo.feedback", 70.0f);
    CHECK (value (first, "feedback") == Catch::Approx (0.7).margin (1.0e-4));
    CHECK (value (second, "feedback") == Catch::Approx (0.2).margin (1.0e-6));
    run (in.processor, 0.1);
    CHECK (in.processor.engine().swapsCompleted() == swaps);   // (opening it and turning a knob: the running graph)
    s.undo();
    CHECK (saveToString (s.document()) == before);
    CHECK (host->parameterValue ("echo.feedback") == Catch::Approx (100.0 * value (first, "feedback")).margin (0.01));
    run (in.processor, 0.1);
    const int afterUndo = in.processor.engine().swapsCompleted();   // (an undo restores the patch by rebuilding: RELEASE_BLOCKERS)
    // a switch of the rack (TAPE -> ASCEND) changes the module and leaves the rack standing
    host->setParameterValue ("echo.type", 2.0f);
    CHECK (juce::roundToInt (value (first, "type")) == 2);
    CHECK (juce::roundToInt (value (second, "type")) == 1);
    CHECK (rack.isNative());
    CHECK (rack.module() == module);
    settle (80);
    host->setParameterValue ("echo.ascend.interval", 1.0f);
    CHECK (juce::roundToInt (value (first, "interval")) == 1);
    host->setParameterValue ("echo.type", 0.0f);
    // the module moved from elsewhere (its tile, a macro, a scene): the rack follows
    s.beginGesture ("tile");
    s.setParam (first, "tone", 0.9);
    CHECK (host->parameterValue ("echo.tone") == Catch::Approx (90.0).margin (0.01));
    s.setParam (first, "sync", 0.0);
    s.setParam (first, "time", 100.0);
    CHECK (host->parameterValue ("echo.sync") < 0.5f);
    CHECK (host->parameterValue ("echo.time") == Catch::Approx (100.0).margin (0.01));

    // the other echo: its own rack, its own values, the same drawer
    editor.openRack (second);
    REQUIRE (rack.isNative());
    host = rack.binding();
    REQUIRE (host != nullptr);
    CHECK (host->nodeId() == second);
    CHECK (juce::roundToInt (host->parameterValue ("echo.type")) == 1);
    CHECK (host->parameterValue ("echo.feedback") == Catch::Approx (20.0).margin (0.01));
    host->setParameterValue ("echo.age", 80.0f);
    CHECK (value (second, "age") == Catch::Approx (0.8).margin (1.0e-4));
    CHECK (value (first, "age") != Catch::Approx (0.8).margin (1.0e-4));
    // opening, swapping and turning its knobs never rebuilt the running graph
    run (in.processor, 0.1);
    CHECK (in.processor.engine().swapsCompleted() == afterUndo);

    // ...and it is the sound that changes: the first echo's FEEDBACK decides how long a note rings
    const auto tail = [&] (float feedback) {
        editor.openRack (first);
        auto* h = rack.binding();
        h->setParameterValue ("echo", 100.0f);
        h->setParameterValue ("echo.feedback", feedback);
        s.beginGesture ("bypass");
        s.setParam (second, "mix", 0.0);
        run (in.processor, 0.25, { juce::MidiMessage::noteOn (1, 57, 0.9f) });
        run (in.processor, 0.25, { juce::MidiMessage::noteOff (1, 57) });
        run (in.processor, 0.9);
        const auto out = run (in.processor, 0.4);
        run (in.processor, 3.0);   // (let it die away before the next measure)
        return rms (out);
    };
    const double dry = tail (0.0f), ringing = tail (90.0f);
    INFO ("tail without feedback " << dry << ", with 90 % " << ringing);
    CHECK (ringing > 10.0 * dry + 1.0e-5);

    // X on its rail (the family's collapse key) and Escape close the drawer
    editor.openRack (first);
    CHECK (editor.dismissTopmost());
    CHECK_FALSE (rack.isOpen());
    // a module without a native rack still opens the plain one
    editor.openRack (osc);
    CHECK (rack.isOpen());
    editor.openRack ({});
}

TEST_CASE ("SPACE and MOVEMENT open ANDOR's own racks; every engine and mode the rack offers plays in the module", "[product][ui][rack][native]")
{
    Instrument in;
    REQUIRE (in.editor != nullptr);
    in.editor->setSize (1448, 1086);
    auto& s = in.session();
    REQUIRE (in.loadFactory ("Analog Organic Morph"));
    s.assets().waitIdle (20.0);
    settle();
    auto& editor = *in.editor;
    auto& rack = editor.rackView();
    const auto space = in.firstOfType ("fx.space");
    REQUIRE_FALSE (space.empty());
    const auto value = [&] (const std::string& node, const char* param) { return s.document().paramValue (s.registry(), node, param); };

    editor.openRack (space);
    REQUIRE (rack.isNative());
    REQUIRE (rack.module() != nullptr);
    CHECK (rack.module()->title() == "SPACE");
    auto* host = rack.binding();
    // the module's one TYPE is the rack's room and engine
    CHECK (juce::roundToInt (host->parameterValue ("space.engine")) == 0);
    CHECK (juce::roundToInt (host->parameterValue ("space.type")) == juce::roundToInt (value (space, "type")));
    CHECK (host->parameterValue ("space.decay") == Catch::Approx (value (space, "decay")).margin (0.01));
    host->setParameterValue ("space.type", 2.0f);   // PLATE
    CHECK (juce::roundToInt (value (space, "type")) == 2);
    // each engine of the rack is an engine of the module, and sounds
    const auto wet = [&] {
        run (in.processor, 0.3, { juce::MidiMessage::noteOn (1, 60, 0.9f) });
        run (in.processor, 0.1, { juce::MidiMessage::noteOff (1, 60) });
        const auto out = run (in.processor, 0.8);
        run (in.processor, 1.5);
        return rms (out, 9600);
    };
    host->setParameterValue ("space", 100.0f);
    CHECK (value (space, "mix") == Catch::Approx (1.0).margin (1.0e-4));
    for (const int engine : { 1, 2, 3, 0 })
    {
        INFO ("engine " << engine);
        host->setParameterValue ("space.engine", static_cast<float> (engine));
        CHECK (juce::roundToInt (value (space, "type")) == (engine == 0 ? 2 : engine + 3));   // (ROOMS comes back to the PLATE it was)
        CHECK (juce::roundToInt (host->parameterValue ("space.engine")) == engine);
        settle (70);
        CHECK (rack.isNative());
        CHECK (wet() > 1.0e-4);
    }
    host->setParameterValue ("space.engine", 2.0f);
    host->setParameterValue ("space.vintage.era", 1.0f);
    CHECK (juce::roundToInt (value (space, "vintageEra")) == 1);
    host->setParameterValue ("space.engine", 3.0f);
    host->setParameterValue ("space.bloom.diffusion", 25.0f);
    CHECK (value (space, "bloomDiffusion") == Catch::Approx (0.25).margin (1.0e-4));
    host->setParameterValue ("space.engine", 0.0f);

    // MOVEMENT
    const auto movement = s.perform ("add", [&] (Document& d) { return edit::addModule (d, s.registry(), "fx.movement", 0, 0, space); }).focus;
    REQUIRE_FALSE (movement.empty());
    settle (300);
    editor.openRack (movement);
    REQUIRE (rack.isNative());
    CHECK (rack.module()->title() == "MOVEMENT");
    host = rack.binding();
    CHECK (host->nodeId() == movement);
    CHECK (host->parameterValue ("motion") == Catch::Approx (100.0 * value (movement, "amount")).margin (0.01));
    host->setParameterValue ("movement.drift.speed", 80.0f);
    CHECK (value (movement, "a") == Catch::Approx (0.8).margin (1.0e-4));
    host->setParameterValue ("movement.mode", 1.0f);   // TAPE: its first control is the same one
    settle (70);
    CHECK (host->parameterValue ("movement.tape.wow") == Catch::Approx (80.0).margin (0.01));
    // SHAPER: a pattern on the host's grid; its place is read from the running module
    host->setParameterValue ("movement.mode", 4.0f);
    CHECK (juce::roundToInt (value (movement, "mode")) == 4);
    host->setParameterValue ("motion", 100.0f);
    host->setParameterValue ("movement.shaper.pattern", 0.0f);
    settle (70);
    run (in.processor, 0.5, { juce::MidiMessage::noteOn (1, 60, 0.9f) });
    CHECK (host->shaperPhase() >= 0.0f);
    // the musician's own sixteen steps: one action, one undo step
    auto pattern = host->shaperCustomPattern();
    const auto before = saveToString (s.document());
    pattern[2].start = 0.25f;
    pattern[2].end = 0.5f;
    pattern[5].shape = osp::StepShape::down;
    settle (120);
    host->setShaperCustomPattern (pattern, true);
    CHECK (value (movement, "s3") == Catch::Approx (0.25).margin (1.0e-6));
    CHECK (value (movement, "e3") == Catch::Approx (0.5).margin (1.0e-6));
    CHECK (juce::roundToInt (value (movement, "h6")) == 1);
    CHECK (host->shaperCustomPattern()[2].start == Catch::Approx (0.25f));
    s.undo();
    CHECK (saveToString (s.document()) == before);
    run (in.processor, 0.2, { juce::MidiMessage::noteOff (1, 60) });
    editor.openRack ({});
}

TEST_CASE ("Modules never land on each other: added, inserted, connected from a port, loaded", "[product][ui][layout]")
{
    Instrument in;
    REQUIRE (in.editor != nullptr);
    auto& s = in.session();
    for (int i = 0; i < static_cast<int> (Session::factoryPatches().size()); ++i)
    {
        INFO (Session::factoryPatches()[static_cast<std::size_t> (i)].name);
        REQUIRE (s.loadFactory (i));
        CHECK (overlappingTiles (s.document(), s.registry()) == 0);
    }
    REQUIRE (in.loadFactory ("Three Arps"));
    settle();
    auto& menu = in.editor->quickAddView();
    // Twelve modules dropped on the same spot through the menu, some following the selection.
    const char* const queries[] = { "lfo", "filter", "space", "analog", "envelope", "mixer", "echo", "random", "granular", "chorus", "drive", "clock" };
    ui::QuickAddContext ctx;
    ctx.world = { 240.0f, 120.0f };
    for (const char* q : queries)
    {
        INFO (q);
        const auto count = s.document().nodes.size();
        in.editor->openQuickAdd (ctx, in.editor->getScreenBounds().getCentre());
        menu.setQuery (q);
        if (menu.visibleTypes().empty())
        {
            menu.close();
            continue;
        }
        menu.chooseSelected();
        if (juce::PopupMenu::dismissAllActiveMenus())
            settle (20);
        CHECK (overlappingTiles (s.document(), s.registry()) == 0);
        if (s.document().nodes.size() > count)
        {
            // one step back is the patch as it was, tidy too
            const auto after = saveToString (s.document());
            s.undo();
            CHECK (s.document().nodes.size() == count);
            CHECK (overlappingTiles (s.document(), s.registry()) == 0);
            s.redo();
            CHECK (saveToString (s.document()) == after);
        }
    }
    // Making room is shown as a short glide (the picture only: the patch is already final),
    // and it ends.
    {
        auto& canvas = in.editor->canvasView();
        const auto tileOf = [&] (const std::string& id) -> juce::Rectangle<int> {
            for (int i = 0; i < canvas.getNumChildComponents(); ++i)
                if (auto* tile = dynamic_cast<ui::NodeView*> (canvas.getChildComponent (i)); tile != nullptr && tile->id() == id)
                    return tile->getBoundsInParent();
            return {};
        };
        in.editor->showPlay (false);
        settle (300);
        CHECK_FALSE (canvas.isGliding());
        const auto first = s.document().nodes[0].id, second = s.document().nodes[1].id;
        const auto before = tileOf (second);
        const float x = s.document().nodes[1].x, y = s.document().nodes[1].y;
        s.perform ("move", [&] (Document& d) {
            auto* n = d.findNode (first);
            n->x = x;
            n->y = y;
            edit::Result r;
            r.ok = true;
            r.placed = { first };
            return r;
        });
        CHECK (overlappingTiles (s.document(), s.registry()) == 0);
        CHECK (canvas.isGliding());
        const auto during = tileOf (second);
        settle (320);
        CHECK_FALSE (canvas.isGliding());
        const auto after = tileOf (second);
        CHECK (after != before);                                        // it made room
        CHECK (during.getPosition().getDistanceFrom (before.getPosition()) <= after.getPosition().getDistanceFrom (before.getPosition()));
        s.undo();
        settle (320);
        CHECK (tileOf (second) == before);
    }
    // A tile dragged onto another: where it was dropped stays, the other makes room.
    const auto& nodes = s.document().nodes;
    REQUIRE (nodes.size() > 2);
    const auto mover = nodes[0].id, target = nodes[1].id;
    const float tx = nodes[1].x, ty = nodes[1].y;
    s.perform ("move", [&] (Document& d) {
        auto* n = d.findNode (mover);
        n->x = tx + 16.0f;
        n->y = ty + 8.0f;
        edit::Result r;
        r.ok = true;
        r.placed = { mover };
        return r;
    });
    CHECK (s.document().findNode (mover)->x == tx + 16.0f);
    CHECK (s.document().findNode (mover)->y == ty + 8.0f);
    CHECK (overlappingTiles (s.document(), s.registry()) == 0);
    juce::ignoreUnused (target);
}

TEST_CASE ("The board follows the scroll gesture; F is only a letter; FIT is the button", "[product][ui][gesture]")
{
    Instrument in;
    REQUIRE (in.editor != nullptr);
    in.editor->setSize (1448, 1086);
    REQUIRE (in.loadFactory ("Analog Organic Morph"));
    settle();
    auto& canvas = in.editor->canvasView();
    canvas.fitAll();
    const auto tileOf = [&] (const std::string& id) -> juce::Rectangle<int> {
        for (int i = 0; i < canvas.getNumChildComponents(); ++i)
            if (auto* tile = dynamic_cast<ui::NodeView*> (canvas.getChildComponent (i)); tile != nullptr && tile->id() == id)
                return tile->getBoundsInParent();
        return {};
    };
    const auto id = in.session().document().nodes.front().id;
    const auto start = tileOf (id);
    REQUIRE_FALSE (start.isEmpty());

    // Natural (the default): a positive delta, as the system delivers "content down / right",
    // moves the modules down / right on screen; and back.
    CHECK_FALSE (ui::Canvas::traditionalScroll);
    canvas.panByWheel (0.0f, 0.1f);
    CHECK (tileOf (id).getY() > start.getY());
    CHECK (tileOf (id).getX() == start.getX());
    canvas.panByWheel (0.0f, -0.1f);
    CHECK (std::abs (tileOf (id).getY() - start.getY()) <= 1);
    canvas.panByWheel (0.1f, 0.0f);
    CHECK (tileOf (id).getX() > start.getX());
    canvas.panByWheel (-0.1f, 0.0f);
    // The real event takes the same path, whatever the system says about its own direction
    // (it has already applied it: reading isReversed again was the double reversal).
    for (const bool reversed : { false, true })
    {
        juce::MouseWheelDetails wheel;
        wheel.deltaX = 0.0f;
        wheel.deltaY = 0.1f;
        wheel.isReversed = reversed;
        wheel.isSmooth = true;
        wheel.isInertial = false;
        canvas.mouseWheelMove (mouseAt (canvas, { 300.0f, 200.0f }, {}), wheel);
        CHECK (tileOf (id).getY() > start.getY());
        wheel.deltaY = -0.1f;
        canvas.mouseWheelMove (mouseAt (canvas, { 300.0f, 200.0f }, {}), wheel);
        CHECK (std::abs (tileOf (id).getY() - start.getY()) <= 1);
    }
    // Traditional: the other way.
    ui::Canvas::traditionalScroll = true;
    canvas.panByWheel (0.0f, 0.1f);
    CHECK (tileOf (id).getY() < start.getY());
    canvas.panByWheel (0.0f, -0.1f);
    ui::Canvas::traditionalScroll = false;

    // Scrolling never zooms; Cmd / Ctrl + scroll does, around the pointer.
    const float zoom = canvas.zoomLevel();
    canvas.panByWheel (0.3f, 0.3f);
    CHECK (canvas.zoomLevel() == zoom);
    {
        juce::MouseWheelDetails wheel;
        wheel.deltaX = 0.0f;
        wheel.deltaY = 0.2f;
        wheel.isReversed = false;
        wheel.isSmooth = false;
        wheel.isInertial = false;
        canvas.mouseWheelMove (mouseAt (canvas, { 300.0f, 200.0f }, juce::ModifierKeys::commandModifier), wheel);
        CHECK (canvas.zoomLevel() > zoom);
    }

    // F does nothing (with or without Shift); the patch is not refitted, not edited.
    canvas.panByWheel (0.4f, 0.4f);
    const auto origin = canvas.viewOrigin();
    const float zoomed = canvas.zoomLevel();
    const auto patch = saveToString (in.session().document());
    CHECK_FALSE (canvas.keyPressed (juce::KeyPress ('f')));
    CHECK_FALSE (canvas.keyPressed (juce::KeyPress ('F', juce::ModifierKeys::shiftModifier, 'F')));
    CHECK (canvas.viewOrigin() == origin);
    CHECK (canvas.zoomLevel() == zoomed);
    // FIT (the button's action) still does
    canvas.fitAll();
    CHECK ((canvas.viewOrigin() != origin || canvas.zoomLevel() != zoomed));
    for (const auto& n : in.session().document().nodes)
        if (n.group.empty())
            CHECK (canvas.getLocalBounds().contains (tileOf (n.id)));
}

TEST_CASE ("The finish: ebonized ash cheeks and black rails round the family's light ceramic", "[product][ui][materials]")
{
    // The pack is Modular's own, in the family's format; the shared defaults are untouched.
    {
        juce::MemoryInputStream bytes (ModularMaterialsData::ebonizedashskin_zip, static_cast<size_t> (ModularMaterialsData::ebonizedashskin_zipSize), false);
        juce::ZipFile zip (bytes);
        const auto* entry = zip.getEntry ("manifest.json");
        REQUIRE (entry != nullptr);
        std::unique_ptr<juce::InputStream> in (zip.createStreamForEntry (*entry));
        const auto manifest = juce::JSON::parse (in->readEntireStreamAsString());
        CHECK (manifest["name"].toString() == "modular-ebonized-ash");
        CHECK (static_cast<int> (manifest["schemaVersion"]) == 1);
        juce::StringArray names;
        for (const auto& piece : *manifest["pieces"].getArray())
            names.add (piece["name"].toString());
        CHECK (names.contains ("housing"));
        CHECK (ui::design::Materials::osp().pack == nullptr);
    }
    Instrument in;
    REQUIRE (in.editor != nullptr);
    in.session().newPatch();
    for (const int width : { 869, 1448, 1810 })
    {
        INFO ("width " << width);
        const int height = width * 1086 / 1448;
        in.editor->setSize (width, height);
        in.editor->showPlay (false);
        settle();
        const auto image = in.editor->createComponentSnapshot (in.editor->getLocalBounds(), true, 1.0f);
        const float scale = static_cast<float> (width) / 1448.0f;
        // (reference pixels in: the mean colour of a small patch)
        const auto at = [&] (float x, float y, float w = 3.0f, float h = 3.0f) {
            double r = 0.0, g = 0.0, b = 0.0;
            int count = 0;
            for (int py = juce::roundToInt (y * scale); py < juce::roundToInt ((y + h) * scale) + 1; ++py)
                for (int px = juce::roundToInt (x * scale); px < juce::roundToInt ((x + w) * scale) + 1; ++px)
                {
                    const auto c = image.getPixelAt (juce::jlimit (0, image.getWidth() - 1, px), juce::jlimit (0, image.getHeight() - 1, py));
                    r += c.getRed();
                    g += c.getGreen();
                    b += c.getBlue();
                    ++count;
                }
            return std::array<double, 3> { r / count, g / count, b / count };
        };
        const auto lum = [] (const std::array<double, 3>& c) { return 0.299 * c[0] + 0.587 * c[1] + 0.114 * c[2]; };
        // Both cheeks: near-black, an espresso undertone (never walnut's warmth), never flat.
        for (const float x : { 8.0f, 1428.0f })
        {
            const auto cheek = at (x, 300.0f, 10.0f, 400.0f);
            CHECK (lum (cheek) < 60.0);
            CHECK (lum (cheek) > 12.0);
            CHECK (cheek[0] >= cheek[2]);
            CHECK (cheek[0] - cheek[2] < 26.0);
            double low = 255.0, high = 0.0;
            for (float gx = x; gx < x + 11.0f; gx += 1.0f / scale)
            {
                const double v = lum (at (gx, 500.0f, 0.0f, 0.0f));
                low = std::min (low, v);
                high = std::max (high, v);
            }
            CHECK (high - low > 5.0);   // the grain is there
        }
        // The rails: dark, neutral metal along the body's top and bottom edges.
        for (const float y : { 13.0f, 1067.0f })
        {
            const auto rail = at (500.0f, y, 300.0f, 3.0f);
            CHECK (lum (rail) < 60.0);
            CHECK (std::abs (rail[0] - rail[2]) < 9.0);
        }
        // The instrument itself stays light: the ceramic body, the board's face.
        CHECK (lum (at (700.0f, 28.0f, 40.0f, 6.0f)) > 200.0);
        CHECK (lum (at (700.0f, 500.0f, 60.0f, 40.0f)) > 225.0);
        CHECK (lum (at (40.0f, 1020.0f, 4.0f, 30.0f)) > 190.0);
    }
}

TEST_CASE ("Screenshots of the reference states", "[product][ui][screens]")
{
    ModularProcessor p;
    p.prepareToPlay (48000.0, 512);
    std::unique_ptr<juce::AudioProcessorEditor> base (p.createEditor());
    auto* editor = dynamic_cast<Editor*> (base.get());
    REQUIRE (editor != nullptr);
    struct Shot
    {
        const char* patch;
        bool play;
        const char* name;
    };
    for (const int width : { 869, 1448, 1810 })
    {
        const int height = width * 1086 / 1448;   // the instrument's own shape (1448 x 1086, scaled as one piece)
        editor->setSize (width, height);
        for (const auto& shot : { Shot { "Analog Organic Morph", false, "patch-morph" }, Shot { "Three Arps", false, "patch-three-arps" },
                                  Shot { "Scene Performance", true, "play-scenes" }, Shot { "Three Arps", true, "play-three-arps" },
                                  Shot { "Six Oscillator Stack", true, "play-six-sources" }, Shot { "Bowed Machine", true, "play-bowed-machine" } })
        {
            const auto& items = Session::factoryPatches();
            for (int i = 0; i < static_cast<int> (items.size()); ++i)
                if (items[static_cast<std::size_t> (i)].name == shot.patch)
                    p.session().loadFactory (i);
            p.session().assets().waitIdle (20.0);
            editor->showPlay (shot.play);
            if (! shot.play)
                editor->canvasView().fitAll();
            run (p, 0.5, { juce::MidiMessage::noteOn (1, 48, 0.8f), juce::MidiMessage::noteOn (1, 55, 0.7f), juce::MidiMessage::noteOn (1, 64, 0.7f) });
            settle();
            snapshot (*editor, juce::String (shot.name) + "-" + juce::String (width));
        }
        // EVOLVE over the instrument
        editor->showEvolve (true);
        settle();
        snapshot (*editor, "play-evolve-" + juce::String (width));
        editor->showEvolve (false);
        // Quick Add: the root with a category open, then a search
        editor->showPlay (false);
        ui::QuickAddContext ctx;
        ctx.world = { 0.0f, 0.0f };
        const auto anchor = editor->getScreenBounds().getCentre().translated (-width / 5, -height / 5);
        editor->openQuickAdd (ctx, anchor);
        editor->quickAddView().openCategory ("Processors");
        settle();
        snapshot (*editor, "quick-add-" + juce::String (width));
        editor->quickAddView().setQuery ("mo");
        settle();
        snapshot (*editor, "quick-add-search-" + juce::String (width));
        editor->quickAddView().close();
        // A module's rack along the board's foot (a filter, then the oscillator: swapped)
        for (int i = 0; i < static_cast<int> (Session::factoryPatches().size()); ++i)
            if (Session::factoryPatches()[static_cast<std::size_t> (i)].name == "Analog Organic Morph")
                p.session().loadFactory (i);
        p.session().assets().waitIdle (20.0);
        for (const char* type : { "proc.filter", "osc.analog" })
            for (const auto& n : p.session().document().nodes)
                if (n.type == type && n.group == p.session().document().view.currentGroup)
                {
                    editor->canvasView().fitAll();
                    editor->canvasView().select (n.id, false);
                    editor->openRack (n.id);
                    run (p, 0.3, { juce::MidiMessage::noteOn (1, 48, 0.8f) });
                    settle();
                    snapshot (*editor, juce::String ("patch-rack-") + (juce::String (type) == "proc.filter" ? "filter-" : "oscillator-") + juce::String (width));
                    break;
                }
        // ANDOR's own ECHO rack on an echo of the patch (TAPE, then BBD)
        {
            std::string space;
            for (const auto& n : p.session().document().nodes)
                if (n.type == "fx.space")
                    space = n.id;
            const auto made = p.session().perform ("add", [&] (Document& d) { return edit::addModule (d, p.session().registry(), "fx.echo", 0, 0, space); });
            REQUIRE (made.ok);
            settle (300);
            editor->canvasView().fitAll();
            editor->canvasView().select (made.focus, false);
            editor->openRack (made.focus);
            run (p, 0.3, { juce::MidiMessage::noteOn (1, 48, 0.8f) });
            settle();
            snapshot (*editor, "patch-rack-echo-tape-" + juce::String (width));
            p.session().beginGesture ("type");
            p.session().setParam (made.focus, "type", 1.0);
            settle (120);
            snapshot (*editor, "patch-rack-echo-bbd-" + juce::String (width));
            // SPACE (a room, then ABYSS) and MOVEMENT (DRIFT, then SHAPER): ANDOR's own racks
            editor->canvasView().select (space, false);
            editor->openRack (space);
            settle (120);
            snapshot (*editor, "patch-rack-space-" + juce::String (width));
            p.session().setParam (space, "type", 4.0);
            settle (150);
            snapshot (*editor, "patch-rack-space-abyss-" + juce::String (width));
            p.session().setParam (space, "type", 1.0);
            const auto movement = p.session().perform ("add", [&] (Document& d) { return edit::addModule (d, p.session().registry(), "fx.movement", 0, 0, space); });
            REQUIRE (movement.ok);
            settle (300);
            editor->canvasView().fitAll();
            editor->canvasView().select (movement.focus, false);
            editor->openRack (movement.focus);
            settle (120);
            snapshot (*editor, "patch-rack-movement-" + juce::String (width));
            p.session().setParam (movement.focus, "mode", 4.0);
            p.session().setParam (movement.focus, "amount", 0.8);
            run (p, 0.3);
            settle (150);
            snapshot (*editor, "patch-rack-movement-shaper-" + juce::String (width));
        }
        editor->openRack ({});
    }
    // the step sequencer's rack (its step editor in the centre)
    {
        p.session().newPatch();
        editor->setSize (1448, 1086);
        editor->showPlay (false);
        p.session().perform ("add", [&] (Document& d) { return edit::addModule (d, p.session().registry(), "osc.analog", 0, 0); });
        const auto made = p.session().perform ("add", [&] (Document& d) { return edit::addModule (d, p.session().registry(), "event.stepseq", 0, 200); });
        REQUIRE (made.ok);
        editor->canvasView().fitAll();
        editor->canvasView().select (made.focus, false);
        editor->openRack (made.focus);
        settle (300);   // (the tiles that made room have arrived)
        snapshot (*editor, "patch-rack-steps-1448");
        CHECK (editor->rackView().controlsArea().getWidth() > 600);
        CHECK_FALSE (editor->rackView().scrolls());   // the step editor takes the room that is left
        editor->openRack ({});
    }
    // an empty patch: the first thing a musician sees after NEW
    p.session().newPatch();
    editor->setSize (1448, 1086);
    settle();
    snapshot (*editor, "patch-empty-1448");
    editor->showPlay (true);
    settle();
    snapshot (*editor, "play-empty-1448");
    CHECK (editor->getWidth() == 1448);
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juce;
    return Catch::Session().run (argc, argv);
}
