#pragma once

#include "ExternalHost.h"

#include "edit/Edits.h"
#include "graph/Engine.h"
#include "io/AssetLoader.h"

#include <juce_audio_formats/juce_audio_formats.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace andor::modular
{

/**
    The patch being edited (message thread): the document, its undo history, its recordings
    and its hosted instruments, compiled and handed to the engine after every change. The
    editor and the processor's state both go through it; the audio thread never sees it.

    Every edit is one transaction (perform): a snapshot for undo, the change, a recompile, and
    a notification ("Created MIXER", with its undo) for the interface.
*/
class Session
{
public:
    explicit Session (Engine& engine);
    ~Session();

    struct Listener
    {
        virtual ~Listener() = default;
        /** structural: modules or cables changed (the canvas rebuilds); else values only. */
        virtual void sessionChanged (bool structural) = 0;
        virtual void sessionMessage (const juce::String& text, bool canUndo, bool isError) = 0;
    };
    void addListener (Listener* l) { listeners.add (l); }
    void removeListener (Listener* l) { listeners.remove (l); }

    Document& document() noexcept { return doc; }
    const Document& document() const noexcept { return doc; }
    const Registry& registry() const noexcept { return Registry::builtIn(); }
    CompiledGraph* graph() noexcept { return engine.latest(); }
    Engine& audio() noexcept { return engine; }
    AssetLibrary& assets() noexcept { return library; }
    ExternalHost& externals() noexcept { return host; }

    /** One undoable transaction. The action returns its result; a failed one is undone at once
        and its reason shown. */
    edit::Result perform (const juce::String& label, const std::function<edit::Result (Document&)>& action, bool structural = true);
    /** A parameter moved by a control: sent live (no recompile unless structural). Call
        beginGesture before a drag so the drag undoes in one step. */
    void beginGesture (const juce::String& label);
    void setParam (const std::string& node, const std::string& param, double value);
    void setNodeString (const std::string& node, const std::string& key, const std::string& value);
    /** A module's light: on, or bypassed. One undo step. Audio and control modules cross-fade
        in the running graph (no rebuild: held notes and effect tails carry on); a note module's
        bypass changes which notes exist, so the graph is rebuilt for it. */
    void setEnabled (const std::string& node, bool on);
    /** M on a source or a group (see andor::modular::audibleGain). Ramped, never a rebuild. */
    void setMuted (const std::string& nodeOrGroup, bool muted);
    /** S on a source or a group. A plain click solos it alone (again: no solo); `additive`
        (Shift) adds it to, or takes it out of, the soloed set. */
    void toggleSolo (const std::string& nodeOrGroup, bool additive);
    void clearSolo();
    bool anySolo() const;
    /** The patch's master level (dB), heard at once (VOLUME; beginGesture first for its undo). */
    void setMasterDb (double dB);
    bool undo();
    bool redo();
    edit::History& history() noexcept { return hist; }

    void recompile();
    void prepare (double sampleRate, int maxBlock);
    /** Message thread, ~30 Hz: frees old graphs, recompiles when a recording finishes loading,
        follows scene moves into the document. */
    void tick();

    // Patches.
    void newPatch();
    bool loadDocument (const Document& d, const juce::String& message = {});
    /** Counts the patches loaded (another count: another patch, not an edit of this one). */
    int loadCount() const noexcept { return loads; }
    bool loadFile (const juce::File& f);
    bool saveFile (const juce::File& f);
    juce::File currentFile() const { return file; }
    struct FactoryItem
    {
        juce::String name, description;
        std::string text;
    };
    static const std::vector<FactoryItem>& factoryPatches();
    static const std::vector<FactoryItem>& factoryModules();
    bool loadFactory (int index);

    /** Where ANDOR Modular keeps its files (factory recordings, user patches and modules). */
    static juce::File userDirectory();
    static juce::File factoryDirectory();
    /** Writes the factory recordings to factoryDirectory() if missing. */
    static void installFactoryContent();

    /** An asset key (relative or absolute) as a file. */
    juce::File resolveKey (const std::string& key) const;
    /** A dropped audio file as the key the patch stores (relative to the patch when it sits
        beside it, else absolute). */
    std::string keyFor (const juce::File& audio) const;

    /** State for the host (ValueTree XML text) and back. */
    std::string toState();
    bool fromState (const std::string& state);

    /** The processor's macro parameters follow the document (a load, a mapping, a scene). */
    std::function<void (int macro, float value)> onMacroValue;
    void pushMacros();

    double lastCompileMilliseconds() const noexcept { return compileMs; }
    const ValidationReport& report() const noexcept { return lastReport; }
    void message (const juce::String& text, bool canUndo = false, bool isError = false);

private:
    void changed (bool structural);
    void publishAudibility();   ///< every source's mute / solo gain, to the running graph
    void syncFromEngine();

    Engine& engine;
    Document doc;
    edit::History hist;
    AssetLibrary library;
    ExternalHost host;
    juce::AudioFormatManager formats;
    juce::ListenerList<Listener> listeners;
    juce::File file;
    std::uint64_t assetGeneration = 0;
    double rate = 48000.0;
    int block = 512;
    double compileMs = 0.0;
    ValidationReport lastReport;
    float lastScene = -1.0f;
    bool gestureOpen = false;
    int loads = 0;
};

} // namespace andor::modular
