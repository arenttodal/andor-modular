// ANDOR Modular product tests: the processor as a host drives it, its state, its editor and
// the musician's first gestures through the real interface. MODULAR_SCREENSHOT_DIR=dir also
// writes the reference screenshots (869 / 1448 / 1810 wide).

#include "PluginEditor.h"
#include "PluginProcessor.h"

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
                                  Shot { "Scene Performance", true, "play-scenes" }, Shot { "Bowed Machine", true, "play-bowed-machine" } })
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
        // Quick Add open, and a module's detail column
        editor->showPlay (false);
        ui::QuickAddContext ctx;
        ctx.world = { 0.0f, 0.0f };
        editor->openQuickAdd (ctx, editor->getScreenBounds().getCentre().translated (-300, -200));
        editor->quickAddView().setQuery ("morph");
        settle();
        snapshot (*editor, "quick-add-" + juce::String (width));
        editor->quickAddView().close();
        for (const auto& n : p.session().document().nodes)
            if ((n.type == "event.stepseq" || n.type == "proc.filter" || n.type == "fx.space") && n.group == p.session().document().view.currentGroup)
            {
                editor->openDetail (n.id);
                editor->canvasView().select (n.id, false);
                break;
            }
        editor->canvasView().fitAll();
        settle();
        snapshot (*editor, "patch-detail-" + juce::String (width));
    }
    // an empty patch: the first thing a musician sees after NEW
    p.session().newPatch();
    editor->setSize (1448, 1086);
    settle();
    snapshot (*editor, "patch-empty-1448");
    CHECK (editor->getWidth() == 1448);
}

int main (int argc, char* argv[])
{
    juce::ScopedJuceInitialiser_GUI juce;
    return Catch::Session().run (argc, argv);
}
