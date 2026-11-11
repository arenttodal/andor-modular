#pragma once

#include "graph/External.h"

#include <juce_audio_processors/juce_audio_processors.h>

#include <map>
#include <memory>

namespace andor::modular
{

/**
    EXTERNAL INSTRUMENT's host: one installed VST3 (or AU on macOS) instrument per node, loaded
    from the file the musician chose (no library scan in the prototype), with its state saved in
    the patch (node string "state", base64). The instance outlives patch edits (keyed by node id
    and file), so its sound and settings survive every recompile.

    Missing plugin: the node stays, silent, with its file and state kept for when the plugin is
    installed again. Third-party plugins may allocate or lock inside their own processing; that
    is outside ANDOR Modular's real-time guarantee (docs/RELEASE_BLOCKERS.md).
*/
class ExternalHost
{
public:
    ExternalHost();
    ~ExternalHost();

    /** The compile-time factory (CompileOptions::externals). */
    std::shared_ptr<ExternalInstrument> instanceFor (const std::string& nodeId, const std::vector<std::string>& strings);

    void prepare (double sampleRate, int maxBlock);
    /** Message thread: each hosted instrument's state (base64) by node id, for saving. */
    std::map<std::string, std::string> states() const;
    /** Opens a hosted instrument's own editor in a window. */
    bool openEditor (const std::string& nodeId);
    juce::String status (const std::string& nodeId) const;
    /** Forgets instances whose nodes are gone. */
    void retain (const std::vector<std::string>& nodeIds);

    juce::AudioPluginFormatManager& formats() noexcept { return manager; }

private:
    class Hosted;
    juce::AudioPluginFormatManager manager;
    std::map<std::string, std::shared_ptr<Hosted>> instances;
    std::map<std::string, std::unique_ptr<juce::DocumentWindow>> windows;
    double rate = 48000.0;
    int block = 512;
};

} // namespace andor::modular
