#include "Session.h"

#include "ModularData.h"

#include "graph/Compiler.h"

#include <algorithm>
#include <chrono>

namespace andor::modular
{

Session::Session (Engine& e) : engine (e)
{
    formats.registerBasicFormats();
    // Recordings decode through JUCE here (WAV, AIFF, FLAC), on the library's worker thread.
    library.setDecoder ([this] (const std::string& path, osp::AudioData& out, std::string& error) {
        const juce::File f { juce::String (path) };
        if (! f.existsAsFile())
        {
            error = "cannot open " + path;
            return false;
        }
        std::unique_ptr<juce::AudioFormatReader> reader (formats.createReaderFor (f));
        if (reader == nullptr)
        {
            error = "not a readable audio file (WAV, AIFF or FLAC)";
            return false;
        }
        const auto frames = std::min<juce::int64> (reader->lengthInSamples, static_cast<juce::int64> (reader->sampleRate * 600.0));
        const int channels = static_cast<int> (std::min<unsigned int> (reader->numChannels, 2));
        juce::AudioBuffer<float> buffer (channels, static_cast<int> (frames));
        reader->read (&buffer, 0, static_cast<int> (frames), 0, true, channels > 1);
        out.sampleRate = reader->sampleRate;
        out.channels.assign (static_cast<std::size_t> (channels), {});
        for (int c = 0; c < channels; ++c)
            out.channels[static_cast<std::size_t> (c)].assign (buffer.getReadPointer (c), buffer.getReadPointer (c) + frames);
        return true;
    });
    installFactoryContent();
    if (! loadFactory (0))
        newPatch();
}

Session::~Session() = default;

void Session::prepare (double sampleRate, int maxBlock)
{
    rate = sampleRate;
    block = maxBlock;
    host.prepare (sampleRate, maxBlock);
    engine.prepare (sampleRate, maxBlock);
    recompile();
}

void Session::recompile()
{
    CompileOptions options;
    options.sampleRate = rate;
    options.maxBlock = std::max (block, maxChunk);
    options.assets = [this] (const std::string& key) {
        const auto path = resolveKey (key).getFullPathName().toStdString();
        auto a = library.resolve (path);
        if (a == nullptr)
        {
            library.request (path);
            a = library.resolve (path);
        }
        return a;
    };
    options.externals = [this] (const std::string& node, const std::vector<std::string>& strings) { return host.instanceFor (node, strings); };
    auto graph = compileGraph (doc, Registry::builtIn(), options);
    compileMs = graph->compileMilliseconds;
    lastReport = graph->report;
    std::vector<std::string> ids;
    for (const auto& n : doc.nodes)
        ids.push_back (n.id);
    host.retain (ids);
    engine.publish (std::move (graph));
}

void Session::pushMacros()
{
    for (int m = 0; m < maxMacros; ++m)
    {
        const auto v = static_cast<float> (doc.macros[static_cast<std::size_t> (m)].value);
        engine.setMacro (m, v);
        if (onMacroValue)
            onMacroValue (m, v);
    }
}

void Session::changed (bool structural)
{
    listeners.call ([structural] (Listener& l) { l.sessionChanged (structural); });
}

void Session::message (const juce::String& text, bool canUndo, bool isError)
{
    listeners.call ([&] (Listener& l) { l.sessionMessage (text, canUndo, isError); });
}

edit::Result Session::perform (const juce::String& label, const std::function<edit::Result (Document&)>& action, bool structural)
{
    gestureOpen = false;
    const Document before = doc;
    auto result = action (doc);
    if (! result.ok && result.choices.empty())
    {
        doc = before;
        if (! result.message.empty())
            message (result.message, false, true);
        return result;
    }
    hist.push (before, label.toStdString());
    recompile();
    bool macrosMoved = false;
    for (int m = 0; m < maxMacros; ++m)
        macrosMoved = macrosMoved || doc.macros[static_cast<std::size_t> (m)].value != before.macros[static_cast<std::size_t> (m)].value;
    if (macrosMoved)
        pushMacros();
    changed (structural);
    if (! result.message.empty())
        message (result.message, true, false);
    return result;
}

void Session::beginGesture (const juce::String& label)
{
    hist.push (doc, label.toStdString());
    gestureOpen = true;
}

void Session::setParam (const std::string& node, const std::string& param, double value)
{
    auto* n = doc.findNode (node);
    const auto* d = n != nullptr ? registry().find (n->type) : nullptr;
    const int p = d != nullptr ? d->paramIndex (param) : -1;
    if (p < 0)
        return;
    const auto& pd = d->params[static_cast<std::size_t> (p)];
    n->params[param] = pd.clampNative (value);
    if (pd.structural || d->role == NodeRole::event || param == "domain")
        recompile();
    else if (auto* g = graph())
        g->setParam (node, param, value);
    // A mode switch can show or hide controls (progressive disclosure): the cards follow.
    changed (pd.curve == ParamCurve::choice || pd.curve == ParamCurve::toggle);
}

void Session::setNodeString (const std::string& node, const std::string& key, const std::string& value)
{
    perform ("set " + juce::String (key), [&] (Document& d) {
        edit::Result r;
        if (auto* n = d.findNode (node))
        {
            n->strings[key] = value;
            r.ok = true;
        }
        return r;
    });
}

bool Session::undo()
{
    if (! hist.undo (doc))
        return false;
    recompile();
    pushMacros();
    changed (true);
    message ("undone");
    return true;
}

bool Session::redo()
{
    if (! hist.redo (doc))
        return false;
    recompile();
    pushMacros();
    changed (true);
    message ("redone");
    return true;
}

void Session::syncFromEngine()
{
    for (int m = 0; m < maxMacros; ++m)
        doc.macros[static_cast<std::size_t> (m)].value = engine.macro (m);
    doc.scenePosition = engine.scenePosition();
}

void Session::tick()
{
    engine.collectGarbage();
    if (library.generation() != assetGeneration)
    {
        assetGeneration = library.generation();
        recompile();
        changed (false);
    }
    // Scenes move the engine; the document follows (what a save keeps, what knobs show).
    const float scene = engine.scenePosition();
    if (scene != lastScene)
    {
        lastScene = scene;
        bool any = false;
        for (const auto& s : doc.scenes)
            any = any || s.captured;
        if (any)
        {
            edit::applyScenePosition (doc, registry(), scene);
            changed (false);
        }
    }
    syncFromEngine();
}

//==============================================================================
void Session::newPatch()
{
    Document d;
    d.name = "New Instrument";
    loadDocument (d, "an empty patch: right-click (or Cmd/Ctrl+K) to add a sound");
}

bool Session::loadDocument (const Document& d, const juce::String& text)
{
    doc = d;
    hist.clear();
    gestureOpen = false;
    pushMacros();
    engine.setScenePosition (static_cast<float> (doc.scenePosition));
    lastScene = static_cast<float> (doc.scenePosition);
    recompile();
    changed (true);
    if (text.isNotEmpty())
        message (text);
    // Recordings that are missing are named (the modules keep their settings until relinked).
    for (const auto& key : doc.assetKeys())
        if (! resolveKey (key).existsAsFile() && key.rfind ("plugin:", 0) != 0 && key.find (".wav") != std::string::npos)
            message ("missing recording: " + juce::String (key) + " (drop a file on its module to relink)", false, true);
    return true;
}

bool Session::loadFile (const juce::File& f)
{
    Document d;
    const auto r = loadFromFile (f.getFullPathName().toStdString(), d);
    if (! r.ok)
    {
        message ("could not open " + f.getFileName() + ": " + juce::String (r.error), false, true);
        return false;
    }
    file = f;
    return loadDocument (d, "opened " + f.getFileName());
}

bool Session::saveFile (const juce::File& f)
{
    syncFromEngine();
    // Recording keys stay valid from the new place (relative beside it, else absolute).
    Document out = doc;
    for (auto& n : out.nodes)
    {
        auto it = n.strings.find ("sample");
        if (it == n.strings.end() || it->second.empty())
            continue;
        const auto abs = resolveKey (it->second);
        it->second = abs.isAChildOf (f.getParentDirectory()) ? abs.getRelativePathFrom (f.getParentDirectory()).replaceCharacter ('\\', '/').toStdString()
                                                             : abs.getFullPathName().toStdString();
    }
    for (const auto& [id, state] : host.states())
        if (auto* n = out.findNode (id))
            n->strings["state"] = state;
    std::string error;
    if (! saveToFile (out, f.getFullPathName().toStdString(), error))
    {
        message ("could not save: " + juce::String (error), false, true);
        return false;
    }
    doc = out;
    file = f;
    message ("saved " + f.getFileName());
    return true;
}

namespace
{
    std::vector<Session::FactoryItem> readResources (const char* extension)
    {
        std::vector<Session::FactoryItem> items;
        for (int i = 0; i < ModularData::namedResourceListSize; ++i)
        {
            const juce::String file (ModularData::originalFilenames[i]);
            if (! file.endsWith (extension))
                continue;
            int size = 0;
            const char* data = ModularData::getNamedResource (ModularData::namedResourceList[i], size);
            Session::FactoryItem item;
            item.text.assign (data, static_cast<std::size_t> (size));
            const auto parsed = json::parse (item.text);
            item.name = juce::String (parsed.value.getString (std::string (extension) == ".andormodule" ? "module" : "name", file.toStdString()));
            item.description = juce::String (parsed.value.getString ("description"));
            items.push_back (std::move (item));
        }
        // The factory's own order (the most immediate first).
        static const char* order[] = { "Basic Poly", "Analog Organic Morph", "Bowed Machine", "Three Arps", "Clockwork", "Satellite Orchestra",
                                       "Granular Rain", "Generative Sequence", "Six Oscillator Stack", "Two-Morph Dream", "Scene Performance",
                                       "External Instrument Demo" };
        auto rank = [&] (const juce::String& n) {
            for (int k = 0; k < 12; ++k)
                if (n == order[k])
                    return k;
            return 100;
        };
        std::stable_sort (items.begin(), items.end(), [&] (const auto& a, const auto& b) { return rank (a.name) < rank (b.name); });
        return items;
    }
}

const std::vector<Session::FactoryItem>& Session::factoryPatches()
{
    static const auto items = readResources (".andormodular");
    return items;
}

const std::vector<Session::FactoryItem>& Session::factoryModules()
{
    static const auto items = readResources (".andormodule");
    return items;
}

bool Session::loadFactory (int index)
{
    const auto& items = factoryPatches();
    if (index < 0 || index >= static_cast<int> (items.size()))
        return false;
    Document d;
    if (! loadFromString (items[static_cast<std::size_t> (index)].text, d).ok)
        return false;
    file = juce::File();
    return loadDocument (d, "factory: " + items[static_cast<std::size_t> (index)].name);
}

juce::File Session::userDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory).getChildFile ("ANDOR").getChildFile ("Modular");
}

juce::File Session::factoryDirectory() { return userDirectory().getChildFile ("Factory"); }

void Session::installFactoryContent()
{
    const auto dir = factoryDirectory().getChildFile ("samples");
    dir.createDirectory();
    for (int i = 0; i < ModularData::namedResourceListSize; ++i)
    {
        const juce::String name (ModularData::originalFilenames[i]);
        if (! name.endsWith (".wav"))
            continue;
        int size = 0;
        const char* data = ModularData::getNamedResource (ModularData::namedResourceList[i], size);
        const auto target = dir.getChildFile (name);
        if (target.getSize() != static_cast<juce::int64> (size))
            target.replaceWithData (data, static_cast<std::size_t> (size));
    }
}

juce::File Session::resolveKey (const std::string& key) const
{
    const juce::String k (key);
    if (juce::File::isAbsolutePath (k))
        return juce::File (k);
    if (file != juce::File())
    {
        const auto beside = file.getParentDirectory().getChildFile (k);
        if (beside.existsAsFile())
            return beside;
    }
    return factoryDirectory().getChildFile (k);
}

std::string Session::keyFor (const juce::File& audio) const
{
    if (file != juce::File() && audio.isAChildOf (file.getParentDirectory()))
        return audio.getRelativePathFrom (file.getParentDirectory()).replaceCharacter ('\\', '/').toStdString();
    if (audio.isAChildOf (factoryDirectory()))
        return audio.getRelativePathFrom (factoryDirectory()).replaceCharacter ('\\', '/').toStdString();
    return audio.getFullPathName().toStdString();
}

std::string Session::toState()
{
    syncFromEngine();
    Document out = doc;
    for (const auto& [id, state] : host.states())
        if (auto* n = out.findNode (id))
            n->strings["state"] = state;
    juce::ValueTree tree ("ANDOR_MODULAR");
    tree.setProperty ("schemaVersion", 1, nullptr);
    tree.setProperty ("patch", juce::String (saveToString (out)), nullptr);
    tree.setProperty ("file", file.getFullPathName(), nullptr);
    return tree.toXmlString().toStdString();
}

bool Session::fromState (const std::string& state)
{
    auto xml = juce::parseXML (juce::String (state));
    if (xml == nullptr)
        return false;
    const auto tree = juce::ValueTree::fromXml (*xml);
    if (! tree.hasType ("ANDOR_MODULAR"))
        return false;
    Document d;
    const auto r = loadFromString (tree["patch"].toString().toStdString(), d);
    if (! r.ok)
    {
        message ("the saved patch could not be read: " + juce::String (r.error), false, true);
        return false;
    }
    file = juce::File (tree["file"].toString());
    return loadDocument (d);
}

} // namespace andor::modular
