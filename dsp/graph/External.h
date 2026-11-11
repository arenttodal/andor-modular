#pragma once

#include "graph/Events.h"

#include <atomic>
#include <functional>
#include <memory>
#include <string>

namespace andor::modular
{

/**
    A hosted instrument (EXTERNAL INSTRUMENT node): one per patch, always GLOBAL (one instance
    hears every note; it is never copied per voice). The kernel knows only this interface; the
    plugin implements it with JUCE's VST3/AU hosting (product/Source/ExternalHost.*), the tests
    with a surrogate instrument (tests/SurrogateInstrument.h).

    Instances outlive compiled graphs (a hosted plugin's state must survive every patch edit):
    the factory returns the same object for the same key. During a graph swap both the old and
    the new graph may call render(); only the newest generation is heard (`claim`).

    Real-time caveat: a third-party plugin may allocate or lock inside its own processing; that
    is outside this engine's guarantees and is reported as such (docs/RELEASE_BLOCKERS.md).
*/
class ExternalInstrument
{
public:
    virtual ~ExternalInstrument() = default;
    virtual std::string name() const = 0;
    virtual bool isLoaded() const noexcept = 0;
    /** Message thread. */
    virtual void prepare (double sampleRate, int maxBlock) = 0;
    /** Audio thread: this chunk's events (offsets relative to the chunk start) and audio out. */
    virtual void render (const NoteEvent* events, int numEvents, float* left, float* right, int numSamples) noexcept = 0;
    virtual int latencySamples() const noexcept { return 0; }

    /** Only the newest graph renders (a swap's fading graph goes quiet for this node). */
    bool claim (std::uint64_t generation) noexcept
    {
        auto seen = newest.load (std::memory_order_relaxed);
        while (generation > seen && ! newest.compare_exchange_weak (seen, generation, std::memory_order_relaxed)) {}
        return generation >= newest.load (std::memory_order_relaxed);
    }

private:
    std::atomic<std::uint64_t> newest { 0 };
};

using ExternalFactory = std::function<std::shared_ptr<ExternalInstrument> (const std::string& key)>;

/** Message thread: the product installs its host (the tests install the surrogate). */
void setExternalFactory (ExternalFactory factory);
std::shared_ptr<ExternalInstrument> makeExternal (const std::string& key);

} // namespace andor::modular
