#pragma once

#include "graph/Node.h"

#include "core/AudioData.h"

#include <atomic>
#include <condition_variable>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <thread>

namespace andor::modular
{

/**
    The patch's recordings: decoded, analysed and built into playable models off the audio
    thread (a worker thread here; never the audio thread), in ANDOR OSP's staged order:

        loading -> provisional (root + onset: playable at once)
                -> ready (+ continuation: sustained notes cycle through its loop)

    Every stage publishes a new immutable SampleAsset; the owner recompiles the graph when
    generation() moves (the compiled graph keeps the asset it was built with alive). A file
    that is missing or undecodable stays as a MISSING / FAILED asset with its key and name, so
    the patch keeps its module and settings until the recording is relinked.

    Keys are what the patch stores (normally the file path). The content hash identifies the
    recording itself (dependency manifest, relink by content).
*/
class AssetLibrary
{
public:
    using Decoder = std::function<bool (const std::string& path, osp::AudioData& out, std::string& error)>;

    AssetLibrary();
    ~AssetLibrary();

    /** Replaces the decoder (the plugin adds AIFF / FLAC through JUCE). Default: WAV. */
    void setDecoder (Decoder d);

    /** Starts loading in the background (no-op if the key is known and not failed). */
    void request (const std::string& key);
    /** Loads now on the calling thread (tools, tests). */
    std::shared_ptr<const SampleAsset> loadNow (const std::string& key, bool withContinuation = true);
    /** The asset now (null if never requested). Thread-safe; not for the audio thread. */
    std::shared_ptr<const SampleAsset> resolve (const std::string& key) const;
    /** An AssetResolver for the compiler (requests unknown keys). */
    AssetResolver resolver();

    /** Increments whenever an asset changes stage. */
    std::uint64_t generation() const noexcept { return gen.load (std::memory_order_acquire); }
    /** Waits (tests) until nothing is queued or loading, or the timeout passes. */
    bool waitIdle (double seconds) const;

    /** Tempo and onsets of a loop (pure analysis, exposed for tests). */
    static void analyseRhythm (const osp::AudioData& audio, double& bpm, double& beats, std::vector<double>& onsets);

private:
    void worker();
    void load (const std::string& key, bool withContinuation);
    void store (const std::string& key, std::shared_ptr<const SampleAsset> asset);

    mutable std::mutex lock;
    mutable std::condition_variable wake;
    std::map<std::string, std::shared_ptr<const SampleAsset>> assets;
    std::deque<std::string> queue;
    std::atomic<std::uint64_t> gen { 0 };
    std::atomic<int> busy { 0 };
    Decoder decoder;
    bool quit = false;
    std::thread thread;
};

} // namespace andor::modular
