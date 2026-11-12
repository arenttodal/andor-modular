#include "graph/Engine.h"

#include "core/Prng.h"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace andor::modular
{

namespace
{
    inline bool same (float a, float b) noexcept { return ! (a < b) && ! (b < a); }

    /** True while an envelope / VCA that ends voices is running in the lane (a bypassed one
        holds no voice: the chain then ends like one without an envelope). */
    inline bool endsLive (const CompiledGraph& g, const Lane& lane) noexcept
    {
        for (const int idx : lane.endsVoice)
            if (g.nodes[static_cast<std::size_t> (idx)].live.load (std::memory_order_relaxed))
                return true;
        return false;
    }
    std::uint32_t mixId (std::uint32_t id, std::uint32_t salt) noexcept
    {
        std::uint32_t x = id * 0x9E3779B1u ^ salt;
        x ^= x >> 16;
        x *= 0x85EBCA6Bu;
        x ^= x >> 13;
        return x == 0 ? 1u : x;
    }

    void addEventsTransposed (EventList& dest, const EventList& src, int transpose, std::uint32_t salt) noexcept
    {
        for (const auto& e : src)
        {
            NoteEvent t = e;
            if (e.kind != NoteEvent::Kind::allNotesOff)
            {
                const int note = static_cast<int> (e.note) + transpose;
                if (note < 0 || note > 127)
                    continue;
                t.note = static_cast<std::uint8_t> (note);
                t.id = mixId (e.id, salt);
            }
            dest.add (t);
        }
    }
}

Engine::Engine()
{
    for (auto& m : macros)
        m.store (0.0f, std::memory_order_relaxed);
    for (auto& r : retired)
        r.store (nullptr, std::memory_order_relaxed);
    for (auto& s : scope)
        s.store (0.0f, std::memory_order_relaxed);
}

Engine::~Engine()
{
    // Whatever the audio thread held is ours again (audio is stopped by now).
    pending.store (nullptr);
    current = fading = nullptr;
    owned.clear();
}

void Engine::prepare (double sampleRate, int maxBlockSize)
{
    rate = sampleRate > 0.0 ? sampleRate : 48000.0;
    blockCapacity = std::max (16, maxBlockSize);
    scratchL.assign (static_cast<std::size_t> (maxChunk), 0.0f);
    scratchR.assign (static_cast<std::size_t> (maxChunk), 0.0f);
    marks.assign (static_cast<std::size_t> (maxChunk) + 1, 0);
    std::size_t ring = 1;
    while (ring < static_cast<std::size_t> (blockCapacity + 4 * maxChunk))
        ring <<= 1;
    outLeft.assign (ring, 0.0f);
    outRight.assign (ring, 0.0f);
    inLeft.assign (ring, 0.0f);
    inRight.assign (ring, 0.0f);
    hostClock = engineClock = 0;
    queueHead = queueCount = 0;
}

void Engine::publish (std::unique_ptr<CompiledGraph> graph)
{
    if (graph == nullptr)
        return;
    CompiledGraph* raw = graph.get();
    owned.push_back (std::move (graph));
    latestGraph = raw;
    if (auto* superseded = pending.exchange (raw, std::memory_order_acq_rel))
    {
        // The audio thread never saw it: free it now.
        owned.erase (std::remove_if (owned.begin(), owned.end(), [&] (const auto& p) { return p.get() == superseded; }), owned.end());
    }
}

void Engine::collectGarbage()
{
    for (auto& slot : retired)
        if (auto* g = slot.exchange (nullptr, std::memory_order_acq_rel))
            owned.erase (std::remove_if (owned.begin(), owned.end(), [&] (const auto& p) { return p.get() == g; }), owned.end());
}

void Engine::retire (CompiledGraph* g) noexcept
{
    if (g == nullptr)
        return;
    for (int attempt = 0; attempt < retireCapacity; ++attempt)
    {
        const int i = (retireWrite.fetch_add (1, std::memory_order_relaxed)) % retireCapacity;
        CompiledGraph* expected = nullptr;
        if (retired[static_cast<std::size_t> (i)].compare_exchange_strong (expected, g, std::memory_order_acq_rel))
            return;
    }
    // Every slot is full (the message thread has not collected for a long time): the graph stays
    // owned and is freed with the engine. Never deleted here.
}

void Engine::setMacro (int index, float value) noexcept
{
    if (index >= 0 && index < maxMacros)
        macros[static_cast<std::size_t> (index)].store (std::clamp (value, 0.0f, 1.0f), std::memory_order_relaxed);
}

float Engine::macro (int index) const noexcept
{
    return index >= 0 && index < maxMacros ? macros[static_cast<std::size_t> (index)].load (std::memory_order_relaxed) : 0.0f;
}

void Engine::setScenePosition (float position) noexcept
{
    scenePos.store (std::clamp (position, 0.0f, static_cast<float> (sceneCount - 1)), std::memory_order_relaxed);
}

void Engine::copyScope (float* dest, int count) const noexcept
{
    count = std::min (count, scopeSize);
    const int start = scopeWrite;   // racy read of the write index is fine for a display
    for (int i = 0; i < count; ++i)
        dest[i] = scope[static_cast<std::size_t> ((start - count + i + 2 * scopeSize) % scopeSize)].load (std::memory_order_relaxed);
}

//==============================================================================
/*  The engine renders on a fixed grid of maxChunk samples counted from its start, and the
    output is delayed by one grid block (maxChunk samples, reported to the host as latency).
    Every control-rate decision therefore falls on the same absolute samples whatever the host's
    block size: a bounce at 64 and a live performance at 512 are the same, sample for sample.
    MIDI keeps its exact sample time (offsets inside the grid split the chunks as before). */
void Engine::process (float* left, float* right, int numSamples, const MidiMessage* midi, int numMidi, const osp::HostTiming& timing,
                      const float* hostValues) noexcept
{
    if (numSamples <= 0)
        return;
    tempoNow.store (static_cast<float> (timing.valid ? timing.bpm : 120.0), std::memory_order_relaxed);
    if (numSamples > blockCapacity)
    {
        // Longer than prepared: in parts, each with its own MIDI.
        int done = 0;
        std::array<MidiMessage, 512> part {};
        while (done < numSamples)
        {
            const int n = std::min (blockCapacity, numSamples - done);
            int count = 0;
            for (int i = 0; i < numMidi && count < static_cast<int> (part.size()); ++i)
                if (midi[i].offset >= done && midi[i].offset < done + n)
                {
                    part[static_cast<std::size_t> (count)] = midi[i];
                    part[static_cast<std::size_t> (count++)].offset -= done;
                }
            osp::HostTiming t = timing;
            if (t.valid)
                t.ppq += static_cast<double> (done) / (rate * 60.0 / std::max (1.0, t.bpm));
            process (left + done, right + done, n, part.data(), count, t, hostValues);
            done += n;
        }
        return;
    }
    // Queue the block's MIDI at its absolute host time.
    for (int i = 0; i < numMidi; ++i)
    {
        if (queueCount >= static_cast<int> (queue.size()))
            break;   // (4096 events in flight: the rest of this block's MIDI is dropped)
        auto& q = queue[static_cast<std::size_t> ((queueHead + queueCount) % static_cast<int> (queue.size()))];
        q.time = hostClock + std::clamp (midi[i].offset, 0, numSamples - 1);
        q.message = midi[i];
        ++queueCount;
    }
    // Host input (LIVE INPUT) into its ring at host time.
    const auto ringMask = static_cast<std::int64_t> (outLeft.size()) - 1;
    for (int i = 0; i < numSamples; ++i)
    {
        const auto at = static_cast<std::size_t> ((hostClock + i) & ringMask);
        inLeft[at] = liveLeft != nullptr ? liveLeft[i] : 0.0f;
        inRight[at] = liveLeft != nullptr ? (liveRight != nullptr ? liveRight[i] : liveLeft[i]) : 0.0f;
    }
    blockTiming = timing;
    blockStart = hostClock;
    hostValuesNow = hostValues;

    // Render grid blocks until the output for this host block exists (engine time = host time - maxChunk).
    blockPeak = 0.0f;
    const std::int64_t needUntil = hostClock + numSamples - maxChunk;
    while (engineClock < needUntil)
    {
        const std::int64_t e0 = engineClock;
        int count = 0;
        while (queueCount > 0 && count < static_cast<int> (blockMidi.size()))
        {
            const auto& q = queue[static_cast<std::size_t> (queueHead)];
            if (q.time >= e0 + maxChunk)
                break;
            blockMidi[static_cast<std::size_t> (count)] = q.message;
            blockMidi[static_cast<std::size_t> (count++)].offset = static_cast<int> (std::clamp<std::int64_t> (q.time - e0, 0, maxChunk - 1));
            queueHead = (queueHead + 1) % static_cast<int> (queue.size());
            --queueCount;
        }
        osp::HostTiming t = blockTiming;
        if (t.valid)
            t.ppq += static_cast<double> (e0 - blockStart) / (rate * 60.0 / std::max (1.0, t.bpm));
        liveBlockLeft = liveLeft != nullptr ? inLeft.data() : nullptr;
        std::array<float, maxChunk> l {}, r {};
        for (int i = 0; i < maxChunk; ++i)
        {
            const auto at = static_cast<std::size_t> ((e0 + i) & ringMask);
            liveChunkL[static_cast<std::size_t> (i)] = inLeft[at];
            liveChunkR[static_cast<std::size_t> (i)] = inRight[at];
        }
        renderEngineBlock (l.data(), r.data(), maxChunk, blockMidi.data(), count, t, hostValuesNow);
        for (int i = 0; i < maxChunk; ++i)
        {
            const auto at = static_cast<std::size_t> ((e0 + i) & ringMask);
            outLeft[at] = l[static_cast<std::size_t> (i)];
            outRight[at] = r[static_cast<std::size_t> (i)];
        }
        engineClock += maxChunk;
    }
    for (int i = 0; i < numSamples; ++i)
    {
        const std::int64_t e = hostClock + i - maxChunk;
        const auto at = static_cast<std::size_t> (e & ringMask);
        left[i] = e >= 0 ? outLeft[at] : 0.0f;
        right[i] = e >= 0 ? outRight[at] : 0.0f;
    }
    peakNow.store (blockPeak, std::memory_order_relaxed);
    hostClock += numSamples;
}

//==============================================================================
void Engine::renderEngineBlock (float* left, float* right, int numSamples, const MidiMessage* midi, int numMidi, const osp::HostTiming& timing,
                                const float* hostValues) noexcept
{
    if (auto* next = pending.exchange (nullptr, std::memory_order_acq_rel))
    {
        if (fading != nullptr)
            retire (fading);
        fading = current;
        fadePosition = 0;
        current = next;
        currentFresh = true;
        generationNow.store (next->generation, std::memory_order_relaxed);
        latencyNow.store (next->latencySamples + maxChunk, std::memory_order_relaxed);
    }

    std::fill (left, left + numSamples, 0.0f);
    std::fill (right, right + numSamples, 0.0f);
    if (current == nullptr)
        return;

    if (panic.exchange (false, std::memory_order_relaxed))
    {
        for (auto* g : { current, fading })
            if (g != nullptr)
                for (auto& lane : g->lanes)
                {
                    for (auto& v : lane.voices)
                        v.state = LaneVoice::State::free;
                    lane.heldCount = 0;
                }
        for (auto& ch : held)
            ch.fill (0.0f);
        for (auto& ch : sustained)
            ch.fill (false);
    }

    // A fresh graph hears the keys held right now, at its first sample.
    const MidiMessage* midiForCurrent = midi;
    int midiCountForCurrent = numMidi;
    if (currentFresh)
    {
        int count = 0;
        for (int ch = 0; ch < 16; ++ch)
            for (int note = 0; note < 128; ++note)
                if (held[static_cast<std::size_t> (ch)][static_cast<std::size_t> (note)] > 0.0f && count < 128)
                {
                    MidiMessage m;
                    m.offset = 0;
                    m.kind = MidiMessage::Kind::noteOn;
                    m.channel = static_cast<std::uint8_t> (ch + 1);
                    m.note = static_cast<std::uint8_t> (note);
                    m.value = held[static_cast<std::size_t> (ch)][static_cast<std::size_t> (note)];
                    replay[static_cast<std::size_t> (count++)] = m;
                }
        for (int i = 0; i < numMidi && count < static_cast<int> (replay.size()); ++i)
            replay[static_cast<std::size_t> (count++)] = midi[i];
        midiForCurrent = replay.data();
        midiCountForCurrent = count;
    }

    renderGraph (*current, left, right, numSamples, midiForCurrent, midiCountForCurrent, timing, currentFresh, hostValues);
    currentFresh = false;

    if (fading != nullptr)
    {
        renderGraph (*fading, scratchL.data(), scratchR.data(), numSamples, midi, numMidi, timing, false, nullptr);
        for (int i = 0; i < numSamples; ++i)
        {
            const float t = std::min (1.0f, static_cast<float> (fadePosition + i) / static_cast<float> (swapFadeSamples));
            const float a = std::sin (1.5707963f * t), b = std::cos (1.5707963f * t);   // equal power
            left[i] = left[i] * a + scratchL[static_cast<std::size_t> (i)] * b;
            right[i] = right[i] * a + scratchR[static_cast<std::size_t> (i)] * b;
        }
        fadePosition += numSamples;
        if (fadePosition >= swapFadeSamples)
        {
            retire (fading);
            fading = nullptr;
            swaps.fetch_add (1, std::memory_order_relaxed);
        }
    }

    // Keyboard memory, after the block (the replay above used the state before it).
    for (int i = 0; i < numMidi; ++i)
    {
        const auto& m = midi[i];
        const auto ch = static_cast<std::size_t> (std::clamp<int> (m.channel, 1, 16) - 1);
        if (m.kind == MidiMessage::Kind::noteOn && m.value > 0.0f)
            held[ch][m.note & 127] = m.value;
        else if (m.kind == MidiMessage::Kind::noteOn || m.kind == MidiMessage::Kind::noteOff)
            held[ch][m.note & 127] = 0.0f;
        else if (m.kind == MidiMessage::Kind::allNotesOff)
            for (auto& c : held)
                c.fill (0.0f);
    }

    // Master: level, safety limiter (instant attack, 120 ms release), scope, peak.
    const double masterDb = liveMasterSet.load (std::memory_order_relaxed) ? static_cast<double> (liveMasterDb.load (std::memory_order_relaxed)) : current->settings.masterDb;
    const float masterTarget = static_cast<float> (std::pow (10.0, masterDb / 20.0));
    if (masterNow < 0.0f)
        masterNow = masterTarget;   // (a level that never moves is applied exactly, as before)
    const float masterGlide = static_cast<float> (1.0 - std::exp (-1.0 / (0.01 * rate)));
    const bool limit = current->settings.limiter;
    const float release = static_cast<float> (1.0 - std::exp (-1.0 / (0.12 * rate)));
    float peak = 0.0f;
    for (int i = 0; i < numSamples; ++i)
    {
        masterNow += (masterTarget - masterNow) * masterGlide;
        const float master = masterNow;
        float l = left[i] * master, r = right[i] * master;
        if (! std::isfinite (l) || ! std::isfinite (r))
            l = r = 0.0f;   // never pass NaN/inf to the host
        if (limit)
        {
            const float level = std::max (std::abs (l), std::abs (r)) * limiterGain;
            if (level > 0.97f)
                limiterGain *= 0.97f / level;
            l *= limiterGain;
            r *= limiterGain;
            limiterGain += release * (1.0f - limiterGain);
            l = std::clamp (l, -1.0f, 1.0f);
            r = std::clamp (r, -1.0f, 1.0f);
        }
        left[i] = l;
        right[i] = r;
        peak = std::max (peak, std::max (std::abs (l), std::abs (r)));
        scope[static_cast<std::size_t> (scopeWrite)].store (0.5f * (l + r), std::memory_order_relaxed);
        scopeWrite = (scopeWrite + 1) % scopeSize;
    }
    blockPeak = std::max (blockPeak, peak);
    sampleClock += numSamples;
}

//==============================================================================
void Engine::applyControls (CompiledGraph& g, const float* hostValues) noexcept
{
    // Host automation bank.
    if (hostValues != nullptr)
        for (std::size_t b = 0; b < g.bindings.size(); ++b)
        {
            auto& hb = g.bindings[b];
            const float v = hostValues[b];
            if (hb.node < 0 || v < 0.0f || v == hb.last)
                continue;
            hb.last = v;
            auto& n = g.nodes[static_cast<std::size_t> (hb.node)];
            const auto& pd = n.desc->params[static_cast<std::size_t> (hb.param)];
            n.base[static_cast<std::size_t> (hb.param)].store (static_cast<float> (pd.denormalise (v)), std::memory_order_relaxed);
        }

    // Scenes: when the position moves, captured values are interpolated into the bases.
    const float pos = scenePos.load (std::memory_order_relaxed);
    if (g.scenesCaptured != 0 && pos != lastScenePos)
    {
        lastScenePos = pos;
        auto between = [&] (std::uint8_t present, int& a, int& b, float& t) {
            a = -1;
            b = -1;
            for (int s = 0; s < sceneCount; ++s)
                if ((present >> s) & 1u)
                {
                    if (static_cast<float> (s) <= pos)
                        a = s;
                    if (static_cast<float> (s) >= pos && b < 0)
                        b = s;
                }
            if (a < 0)
                a = b;
            if (b < 0)
                b = a;
            t = (a == b) ? 0.0f : (pos - static_cast<float> (a)) / static_cast<float> (b - a);
        };
        for (const auto& st : g.sceneTargets)
        {
            int a, b;
            float t;
            between (st.present, a, b, t);
            if (a < 0)
                continue;
            const float na = st.norm[static_cast<std::size_t> (a)], nb = st.norm[static_cast<std::size_t> (b)];
            const float nrm = st.stepped ? (t < 0.5f ? na : nb) : na + (nb - na) * t;
            auto& n = g.nodes[static_cast<std::size_t> (st.node)];
            n.base[static_cast<std::size_t> (st.param)].store (static_cast<float> (n.desc->params[static_cast<std::size_t> (st.param)].denormalise (nrm)),
                                                               std::memory_order_relaxed);
        }
        int a, b;
        float t;
        between (g.scenesCaptured, a, b, t);
        if (a >= 0)
            for (int m = 0; m < maxMacros; ++m)
            {
                const float ma = g.sceneMacros[static_cast<std::size_t> (a)][static_cast<std::size_t> (m)];
                const float mb = g.sceneMacros[static_cast<std::size_t> (b)][static_cast<std::size_t> (m)];
                macros[static_cast<std::size_t> (m)].store (ma + (mb - ma) * t, std::memory_order_relaxed);
            }
    }

    // Bases.
    for (auto& n : g.nodes)
    {
        if (n.desc == nullptr)
            continue;
        for (std::size_t p = 0; p < n.desc->params.size(); ++p)
        {
            const float v = n.base[p].load (std::memory_order_relaxed);
            if (v != n.baseNative[p] || n.macroMapped[p] != 0)
            {
                n.baseNative[p] = v;
                n.baseNorm[p] = static_cast<float> (n.desc->params[p].normalise (v));
            }
            if (n.macroMapped[p] != 0)
                n.macroMapped[p] = 1;
        }
    }

    // Control channels: macros (smoothed) drive group controls and parameters.
    for (int c = maxMacros; c < static_cast<int> (g.channels.size()); ++c)
    {
        auto& ch = *g.channels[static_cast<std::size_t> (c)];
        if (! ch.hasDriver)
            ch.effective = ch.value.load (std::memory_order_relaxed);
    }
    for (const int c : g.channelOrder)
    {
        auto& ch = *g.channels[static_cast<std::size_t> (c)];
        if (c < maxMacros)
        {
            const float target = macros[static_cast<std::size_t> (c)].load (std::memory_order_relaxed);
            ch.smoothed += ch.coef * (target - ch.smoothed);
            if (std::abs (target - ch.smoothed) < 1.0e-5f)
                ch.smoothed = target;
            ch.effective = ch.smoothed;
        }
        for (const auto& t : ch.targets)
        {
            const float shaped = static_cast<float> (applyMacroCurve (t.curve, ch.effective));
            const float value = t.min + (t.max - t.min) * shaped;
            if (t.channel >= 0)
            {
                g.channels[static_cast<std::size_t> (t.channel)]->effective = std::clamp (value, 0.0f, 1.0f);
                continue;
            }
            auto& n = g.nodes[static_cast<std::size_t> (t.node)];
            auto& flag = n.macroMapped[static_cast<std::size_t> (t.param)];
            auto& norm = n.baseNorm[static_cast<std::size_t> (t.param)];
            if (flag == 1)
            {
                norm = value;
                flag = 2;
            }
            else
                norm += (t.max - t.min) * shaped;
        }
    }
    for (auto& n : g.nodes)
        if (n.desc != nullptr)
            for (std::size_t p = 0; p < n.desc->params.size(); ++p)
            {
                if (n.macroMapped[p] == 2)
                    n.baseNative[p] = static_cast<float> (n.desc->params[p].denormalise (std::clamp (n.baseNorm[p], 0.0f, 1.0f)));
                n.shown[p].store (n.baseNative[p], std::memory_order_relaxed);
            }
}

//==============================================================================
void Engine::runEvents (CompiledGraph& g, int numSamples, const MidiMessage* midi, int numMidi, const osp::HostTiming& timing, bool firstBlock) noexcept
{
    hostEvents.clear();
    for (int i = 0; i < numMidi; ++i)
    {
        const auto& m = midi[i];
        const int offset = std::clamp (m.offset, 0, numSamples - 1);
        const auto ch = static_cast<std::size_t> (std::clamp<int> (m.channel, 1, 16) - 1);
        NoteEvent e;
        e.offset = offset;
        e.channel = static_cast<std::uint8_t> (ch + 1);
        e.note = m.note & 127;
        e.id = static_cast<std::uint32_t> ((ch << 8) | e.note) + 1u;
        switch (m.kind)
        {
            case MidiMessage::Kind::noteOn:
                if (m.value > 0.0f)
                {
                    sustained[ch][e.note] = false;
                    e.kind = NoteEvent::Kind::noteOn;
                    e.velocity = std::clamp (m.value, 0.0f, 1.0f);
                    hostEvents.add (e);
                    break;
                }
                [[fallthrough]];
            case MidiMessage::Kind::noteOff:
                if (sustainDown)
                    sustained[ch][e.note] = true;
                else
                {
                    e.kind = NoteEvent::Kind::noteOff;
                    hostEvents.add (e);
                }
                break;
            case MidiMessage::Kind::sustain:
            {
                const bool down = m.value >= 0.5f;
                if (sustainDown && ! down)
                    for (std::size_t c = 0; c < 16; ++c)
                        for (std::size_t n = 0; n < 128; ++n)
                            if (sustained[c][n])
                            {
                                sustained[c][n] = false;
                                NoteEvent off;
                                off.offset = offset;
                                off.kind = NoteEvent::Kind::noteOff;
                                off.channel = static_cast<std::uint8_t> (c + 1);
                                off.note = static_cast<std::uint8_t> (n);
                                off.id = static_cast<std::uint32_t> ((c << 8) | n) + 1u;
                                hostEvents.add (off);
                            }
                sustainDown = down;
                break;
            }
            case MidiMessage::Kind::allNotesOff:
                e.kind = NoteEvent::Kind::allNotesOff;
                hostEvents.add (e);
                break;
            case MidiMessage::Kind::pitchBend:
                bendSemitones = static_cast<float> (std::clamp (m.value, -1.0f, 1.0f) * g.settings.bendRange);
                break;
            case MidiMessage::Kind::controller:
                break;
        }
    }
    hostEvents.sort();

    EventBlock block;
    block.numSamples = numSamples;
    block.sampleRate = rate;
    block.timing = timing;
    block.samplePosition = sampleClock;
    block.transportStarted = restart.exchange (false, std::memory_order_relaxed) || firstBlock;

    std::array<const EventList*, maxPorts> inputs {};
    std::array<float*, maxPorts> controls {};
    for (const int idx : g.eventOrder)
    {
        auto& n = g.nodes[static_cast<std::size_t> (idx)];
        n.eventOut.clear();
        if (n.eventProcessor == nullptr)
            continue;
        int numInputs = 0;
        if (n.desc->type == "event.keyboard")
        {
            inputs[0] = &hostEvents;
            numInputs = 1;
        }
        else
            for (std::size_t p = 0; p < n.eventInputs.size() && p < static_cast<std::size_t> (maxPorts); ++p)
            {
                auto& merged = n.eventInputs[p];
                merged.clear();
                for (const auto& src : n.eventSources[p])
                    addEventsTransposed (merged, g.nodes[static_cast<std::size_t> (src[0])].eventOut, src[2],
                                         static_cast<std::uint32_t> (src[0] * 7919 + src[2] + 101));
                merged.sort();
                inputs[p] = &merged;
                numInputs = static_cast<int> (p) + 1;
            }
        int numControls = 0;
        for (std::size_t q = 0; q < n.eventControl.size() && q < static_cast<std::size_t> (maxPorts); ++q)
            if (n.desc->outputs[q].kind != PortKind::notes)
            {
                controls[static_cast<std::size_t> (numControls++)] = n.eventControl[q].data();
            }
        if (! n.enabled)
        {
            // Disabled: notes pass straight through (a bypassed ARP plays the keys).
            if (numInputs > 0 && inputs[0] != nullptr)
                for (const auto& e : *inputs[0])
                    n.eventOut.add (e);
            continue;
        }
        n.eventProcessor->process (block, inputs.data(), numInputs, n.baseNative.data(), n.eventOut, controls.data(), numControls);
        n.eventOut.sort();
    }

    for (auto& lane : g.lanes)
    {
        lane.events.clear();
        lane.cursor = 0;
        for (const auto& src : lane.noteSources)
            addEventsTransposed (lane.events, g.nodes[static_cast<std::size_t> (src.node)].eventOut, src.transpose, src.salt);
        lane.events.sort();
    }
    for (auto& n : g.nodes)
        if (n.takesNotes)
        {
            n.notes.clear();
            n.noteCursor = 0;
            for (const auto& port : n.eventSources)
                for (const auto& src : port)
                    addEventsTransposed (n.notes, g.nodes[static_cast<std::size_t> (src[0])].eventOut, src[2], static_cast<std::uint32_t> (src[0] * 31 + 7));
            n.notes.sort();
        }
}

//==============================================================================
void Engine::startVoice (CompiledGraph& g, Lane& lane, LaneVoice& voice, const NoteEvent& e, bool retrigger, bool resetState) noexcept
{
    const int v = voice.info.voiceIndex;
    if (resetState)
        for (const int idx : lane.nodes)
        {
            auto& inst = g.nodes[static_cast<std::size_t> (idx)].instances[static_cast<std::size_t> (v)];
            if (inst.processor != nullptr)
                inst.processor->reset();
        }
    const bool glide = g.settings.glideSeconds > 0.0 && g.settings.voiceMode != 0 && voice.state != LaneVoice::State::free;
    voice.info.note = e.note;
    voice.targetPitch = e.note;
    if (! glide)
        voice.info.pitch = e.note;
    voice.info.velocity = e.velocity;
    voice.info.gate = true;
    voice.info.legato = ! retrigger;
    voice.info.noteId = e.id;
    voice.info.seed = osp::Prng::deriveSeed (g.settings.seed, ++lane.counter, e.note);
    voice.info.age = 0;
    voice.info.fade = voice.info.fadeEnd = 1.0f;
    voice.laneNoteId = e.id;
    voice.order = lane.counter;
    voice.state = LaneVoice::State::active;
    voice.fadeRemaining = 0;
    for (const int idx : lane.nodes)
    {
        auto& inst = g.nodes[static_cast<std::size_t> (idx)].instances[static_cast<std::size_t> (v)];
        if (inst.processor != nullptr)
            inst.processor->start (voice.info, retrigger);
    }
}

void Engine::laneNoteEvent (CompiledGraph& g, Lane& lane, const NoteEvent& e) noexcept
{
    auto release = [&] (LaneVoice& voice) {
        voice.state = LaneVoice::State::released;
        voice.info.gate = false;
        if (! endsLive (g, lane))
            voice.fadeRemaining = 256;   // no (running) envelope in the chain: a short fade ends it
        for (const int idx : lane.nodes)
        {
            auto& inst = g.nodes[static_cast<std::size_t> (idx)].instances[static_cast<std::size_t> (voice.info.voiceIndex)];
            if (inst.processor != nullptr)
                inst.processor->release();
        }
    };

    if (e.kind == NoteEvent::Kind::allNotesOff)
    {
        for (auto& voice : lane.voices)
            if (voice.state == LaneVoice::State::active)
                release (voice);
            else if (voice.state == LaneVoice::State::stealing)
                voice.pendingReleased = true;
        lane.heldCount = 0;
        return;
    }

    const bool mono = g.settings.voiceMode != 0;
    if (mono)
    {
        auto& voice = lane.voices[0];
        if (e.kind == NoteEvent::Kind::noteOn)
        {
            if (lane.heldCount < static_cast<int> (lane.held.size()))
                lane.held[static_cast<std::size_t> (lane.heldCount++)] = e;
            const bool wasFree = voice.state == LaneVoice::State::free;
            const bool keyHeld = voice.state == LaneVoice::State::active && voice.info.gate;
            // MONO retriggers (from the envelope's current level, no click); LEGATO glides while a key is held.
            const bool retrigger = g.settings.voiceMode == 1 || ! keyHeld;
            startVoice (g, lane, voice, e, retrigger, wasFree);
        }
        else
        {
            int found = -1;
            for (int i = 0; i < lane.heldCount; ++i)
                if (lane.held[static_cast<std::size_t> (i)].id == e.id)
                    found = i;
            if (found < 0)
                return;
            for (int i = found; i + 1 < lane.heldCount; ++i)
                lane.held[static_cast<std::size_t> (i)] = lane.held[static_cast<std::size_t> (i + 1)];
            --lane.heldCount;
            if (voice.laneNoteId != e.id || voice.state != LaneVoice::State::active)
                return;
            if (lane.heldCount > 0)
                startVoice (g, lane, voice, lane.held[static_cast<std::size_t> (lane.heldCount - 1)], false, false);   // back to the held note, legato
            else
                release (voice);
        }
        return;
    }

    if (e.kind == NoteEvent::Kind::noteOff)
    {
        for (auto& voice : lane.voices)
        {
            if (voice.state == LaneVoice::State::active && voice.laneNoteId == e.id)
            {
                release (voice);
                return;
            }
            if (voice.state == LaneVoice::State::stealing && voice.hasPending && voice.pending.id == e.id)
            {
                voice.pendingReleased = true;
                return;
            }
        }
        return;
    }

    // Note on (poly): a free voice, else steal (released first, then the oldest).
    const int cap = voiceCap.load (std::memory_order_relaxed);
    int sounding = 0;
    for (const auto& v : lane.voices)
        sounding += v.state != LaneVoice::State::free ? 1 : 0;
    LaneVoice* target = nullptr;
    if (cap <= 0 || sounding < cap)
        for (auto& v : lane.voices)
            if (v.state == LaneVoice::State::free)
            {
                target = &v;
                break;
            }
    if (target != nullptr)
    {
        startVoice (g, lane, *target, e, true, true);
        return;
    }
    LaneVoice* victim = nullptr;
    for (auto& v : lane.voices)
        if (v.state == LaneVoice::State::released && (victim == nullptr || v.order < victim->order))
            victim = &v;
    if (victim == nullptr)
        for (auto& v : lane.voices)
            if (v.state == LaneVoice::State::active && (victim == nullptr || v.order < victim->order))
                victim = &v;
    if (victim == nullptr)
    {
        // Every voice is already being stolen: replace the newest pending note.
        for (auto& v : lane.voices)
            if (v.state == LaneVoice::State::stealing)
                victim = &v;
        if (victim != nullptr)
        {
            victim->pending = e;
            victim->pendingReleased = false;
        }
        return;
    }
    victim->state = LaneVoice::State::stealing;
    victim->hasPending = true;
    victim->pendingReleased = false;
    victim->pending = e;
    victim->fadeRemaining = 64;   // 1.3 ms at 48 kHz
    victim->fadeFrom = victim->info.fade;
}

void Engine::runInstance (CompiledNode& n, Instance& inst, const ChunkContext& ctx) noexcept
{
    const int len = ctx.numSamples;
    for (auto& s : inst.sums)
    {
        for (int c = 0; c < s.channels; ++c)
        {
            float* d = s.sum->ch[static_cast<std::size_t> (c)].data();
            std::fill (d, d + len, 0.0f);
            for (const auto& src : s.sources)
            {
                const float* x = src[static_cast<std::size_t> (c)];
                for (int i = 0; i < len; ++i)
                    d[i] += x[i];
            }
        }
    }
    for (auto& al : inst.aligns)
    {
        const int size = static_cast<int> (al.line.size()) / 2;
        for (int i = 0; i < len; ++i)
        {
            for (int c = 0; c < 2; ++c)
            {
                float* line = al.line.data() + c * size;
                line[al.write] = al.source[static_cast<std::size_t> (c)][i];
                al.out->ch[static_cast<std::size_t> (c)][static_cast<std::size_t> (i)] = line[(al.write - al.delay + size) % size];
            }
            al.write = (al.write + 1) % size;
        }
    }

    const auto& d = *n.desc;
    const std::size_t np = d.params.size();
    for (std::size_t p = 0; p < np; ++p)
        inst.resolved[p] = n.baseNative[p];
    if (! inst.mods.empty())
    {
        std::array<float, 64> sum {};
        std::array<bool, 64> touched {};
        for (const auto& m : inst.mods)
        {
            if (m.param < 0 || m.param >= 64 || m.source == nullptr)
                continue;
            const float v = m.source[0];
            if (std::isfinite (v))
            {
                sum[static_cast<std::size_t> (m.param)] += m.depth * v;
                touched[static_cast<std::size_t> (m.param)] = true;
            }
        }
        for (std::size_t p = 0; p < np && p < 64; ++p)
            if (touched[p])
                inst.resolved[p] = static_cast<float> (d.params[p].denormalise (std::clamp (n.baseNorm[p] + sum[p], 0.0f, 1.0f)));
    }

    // On, bypassed, or between the two (CompiledNode::live): a bypass is a 12 ms crossfade
    // between the processed signal and what passes (the first audio input to the first audio
    // output where both exist, else silence), never a rebuilt graph.
    const bool on = n.live.load (std::memory_order_relaxed);
    const float mixTarget = on ? 1.0f : 0.0f;
    if (inst.mixNow < 0.0f)
        inst.mixNow = mixTarget;
    const bool passes = d.role != NodeRole::source && ! d.inputs.empty() && ! d.outputs.empty() && isAudio (d.inputs[0].kind) && isAudio (d.outputs[0].kind)
                        && inst.io.inConnected[0];
    if (inst.processor == nullptr || (! on && inst.mixNow <= 0.0f))
    {
        for (std::size_t q = 0; q < d.outputs.size(); ++q)
            for (int c = 0; c < 2; ++c)
                std::fill (inst.io.out[q][static_cast<std::size_t> (c)], inst.io.out[q][static_cast<std::size_t> (c)] + len, 0.0f);
        if (passes && ! on)
            for (int c = 0; c < 2; ++c)
                std::memcpy (inst.io.out[0][static_cast<std::size_t> (c)], inst.io.in[0][static_cast<std::size_t> (c)], sizeof (float) * static_cast<std::size_t> (len));
        return;
    }
    inst.processor->process (ctx, inst.io);

    const float rampStep = static_cast<float> (1.0 / (0.012 * ctx.sampleRate));
    if (! same (inst.mixNow, mixTarget))
    {
        float m = inst.mixNow;
        for (std::size_t q = 0; q < d.outputs.size(); ++q)
        {
            if (d.outputs[q].kind == PortKind::notes)
                continue;
            m = inst.mixNow;
            for (int i = 0; i < len; ++i)
            {
                m = mixTarget > m ? std::min (mixTarget, m + rampStep) : std::max (mixTarget, m - rampStep);
                for (int c = 0; c < 2; ++c)
                {
                    float* o = inst.io.out[q][static_cast<std::size_t> (c)];
                    const float pass = q == 0 && passes ? inst.io.in[0][static_cast<std::size_t> (c)][i] : 0.0f;
                    o[i] = pass + (o[i] - pass) * m;
                }
            }
        }
        inst.mixNow = m;
    }
    // A source's audible contribution (mute, solo): ramped here, at its own output.
    const float gainTarget = n.gain.load (std::memory_order_relaxed);
    if (inst.gainNow < 0.0f)
        inst.gainNow = gainTarget;
    if (! same (inst.gainNow, gainTarget) || gainTarget < 1.0f)
    {
        float gNow = inst.gainNow;
        for (std::size_t q = 0; q < d.outputs.size(); ++q)
        {
            if (! isAudio (d.outputs[q].kind))
                continue;
            gNow = inst.gainNow;
            for (int i = 0; i < len; ++i)
            {
                gNow = gainTarget > gNow ? std::min (gainTarget, gNow + rampStep) : std::max (gainTarget, gNow - rampStep);
                inst.io.out[q][0][i] *= gNow;
                inst.io.out[q][1][i] *= gNow;
            }
        }
        inst.gainNow = gNow;
    }

    if (! d.outputs.empty() && d.outputs[0].kind != PortKind::notes)
    {
        float pk = n.peak.load (std::memory_order_relaxed);
        const float* o = inst.io.out[0][0];
        for (int i = 0; i < len; ++i)
            pk = std::max (pk, std::abs (o[i]));
        n.peak.store (pk, std::memory_order_relaxed);
    }
}

void Engine::renderLane (CompiledGraph& g, Lane& lane, int laneIndex, const ChunkContext& base) noexcept
{
    (void) laneIndex;
    const int end = base.offset + base.numSamples;
    while (lane.cursor < lane.events.count && lane.events.events[static_cast<std::size_t> (lane.cursor)].offset < end)
        laneNoteEvent (g, lane, lane.events.events[static_cast<std::size_t> (lane.cursor++)]);

    const double glideCoef = g.settings.glideSeconds > 0.0
                                 ? 1.0 - std::exp (-static_cast<double> (base.numSamples) / (g.settings.glideSeconds * base.sampleRate))
                                 : 1.0;
    int active = 0;
    for (auto& voice : lane.voices)
    {
        if (voice.state == LaneVoice::State::free)
            continue;
        ++active;
        auto& info = voice.info;
        const double basePitch = info.pitch - static_cast<double> (base.pitchBendSemitones);
        double glided = basePitch + (voice.targetPitch - basePitch) * glideCoef;
        if (std::abs (voice.targetPitch - glided) < 1.0e-4)
            glided = voice.targetPitch;
        info.pitch = glided + static_cast<double> (base.pitchBendSemitones);

        // Fades: a steal (64 samples) or the end of a chain with no envelope.
        info.fade = info.fadeEnd;
        if (voice.state == LaneVoice::State::stealing || (voice.state == LaneVoice::State::released && ! endsLive (g, lane)))
        {
            const int total = voice.state == LaneVoice::State::stealing ? 64 : 256;
            const int left = std::max (0, voice.fadeRemaining - base.numSamples);
            info.fadeEnd = static_cast<float> (left) / static_cast<float> (total) * (voice.state == LaneVoice::State::stealing ? voice.fadeFrom : 1.0f);
        }
        else
            info.fadeEnd = 1.0f;

        ChunkContext ctx = base;
        ctx.voice = &info;
        const auto v = static_cast<std::size_t> (info.voiceIndex);
        for (const int idx : lane.nodes)
        {
            auto& n = g.nodes[static_cast<std::size_t> (idx)];
            runInstance (n, n.instances[v], ctx);
        }
        for (const auto& [b, feed] : lane.boundaries)
        {
            auto& bn = g.nodes[static_cast<std::size_t> (b)];
            auto& f = bn.feeds[static_cast<std::size_t> (feed)];
            for (auto& s : f.voiceSums[v])
                for (int c = 0; c < s.channels; ++c)
                {
                    float* d = s.sum->ch[static_cast<std::size_t> (c)].data();
                    std::fill (d, d + base.numSamples, 0.0f);
                    for (const auto& src : s.sources)
                        for (int i = 0; i < base.numSamples; ++i)
                            d[i] += src[static_cast<std::size_t> (c)][i];
                }
            if (bn.enabled && ! bn.instances.empty() && bn.instances[0].processor != nullptr)
                bn.instances[0].processor->accumulate (ctx, f.voiceIO[v], info);
        }
        info.age += base.numSamples;

        if (voice.state == LaneVoice::State::stealing)
        {
            voice.fadeRemaining -= base.numSamples;
            if (voice.fadeRemaining <= 0)
            {
                voice.state = LaneVoice::State::free;
                if (voice.hasPending)
                {
                    voice.hasPending = false;
                    startVoice (g, lane, voice, voice.pending, true, true);
                    if (voice.pendingReleased)
                    {
                        voice.pendingReleased = false;
                        NoteEvent off = voice.pending;
                        off.kind = NoteEvent::Kind::noteOff;
                        laneNoteEvent (g, lane, off);
                    }
                }
            }
        }
        else if (voice.state == LaneVoice::State::released)
        {
            if (! endsLive (g, lane))
            {
                voice.fadeRemaining -= base.numSamples;
                if (voice.fadeRemaining <= 0)
                    voice.state = LaneVoice::State::free;
            }
            else
            {
                bool alive = false;
                for (const int idx : lane.endsVoice)
                {
                    const auto& en = g.nodes[static_cast<std::size_t> (idx)];
                    const auto& inst = en.instances[v];
                    alive = alive || (en.live.load (std::memory_order_relaxed) && inst.processor != nullptr && inst.processor->active());
                }
                if (! alive)
                    voice.state = LaneVoice::State::free;
            }
        }
    }
    lane.activeVoices.store (active, std::memory_order_relaxed);
}

void Engine::renderGraph (CompiledGraph& g, float* left, float* right, int numSamples, const MidiMessage* midi, int numMidi,
                          const osp::HostTiming& timing, bool firstBlock, const float* hostValues) noexcept
{
    applyControls (g, hostValues);
    runEvents (g, numSamples, midi, numMidi, timing, firstBlock);

    // Chunk boundaries: every event offset any lane or note-reading node sees.
    std::fill (marks.begin(), marks.begin() + numSamples + 1, static_cast<std::uint8_t> (0));
    for (const auto& lane : g.lanes)
        for (const auto& e : lane.events)
            marks[static_cast<std::size_t> (std::clamp (e.offset, 0, numSamples - 1))] = 1;
    for (const auto& n : g.nodes)
        if (n.takesNotes)
            for (const auto& e : n.notes)
                marks[static_cast<std::size_t> (std::clamp (e.offset, 0, numSamples - 1))] = 1;

    const bool playing = timing.valid && timing.playing;
    const double bpm = timing.valid && timing.bpm > 0.0 ? timing.bpm : 120.0;
    const double spq = rate * 60.0 / bpm;
    const double ppqStart = playing ? timing.ppq : freePpq;
    const bool jumped = playing && lastPpqEnd >= 0.0 && std::abs (timing.ppq - lastPpqEnd) > 0.02;

    int voices = 0;
    for (int pos = 0; pos < numSamples;)
    {
        int end = std::min (numSamples, pos + maxChunk);
        for (int i = pos + 1; i < end; ++i)
            if (marks[static_cast<std::size_t> (i)] != 0)
            {
                end = i;
                break;
            }
        const int len = end - pos;
        ChunkContext ctx;
        ctx.sampleRate = rate;
        ctx.numSamples = len;
        ctx.offset = pos;
        ctx.samplePosition = sampleClock + pos;
        ctx.bpm = bpm;
        ctx.ppq = ppqStart + static_cast<double> (pos) / spq;
        ctx.playing = playing;
        ctx.transportJumped = jumped && pos == 0;
        ctx.pitchBendSemitones = bendSemitones;
        ctx.generation = g.generation;
        if (liveBlockLeft != nullptr)
            ctx.liveInput = { liveChunkL.data() + pos, liveChunkR.data() + pos };

        for (const int idx : g.eventOrder)
        {
            auto& n = g.nodes[static_cast<std::size_t> (idx)];
            for (std::size_t q = 0; q < n.eventControl.size(); ++q)
                std::memcpy (n.instances[0].outputs[q].ch[0].data(), n.eventControl[q].data() + pos, sizeof (float) * static_cast<std::size_t> (len));
        }
        for (const int b : g.boundaryNodes)
        {
            auto& bn = g.nodes[static_cast<std::size_t> (b)];
            if (! bn.instances.empty() && bn.instances[0].processor != nullptr)
                bn.instances[0].processor->beginChunk (len);
        }
        for (const auto& u : g.schedule)
        {
            if (u.lane)
            {
                renderLane (g, g.lanes[static_cast<std::size_t> (u.index)], u.index, ctx);
                continue;
            }
            auto& n = g.nodes[static_cast<std::size_t> (u.index)];
            if (n.instances.empty())
                continue;
            ChunkContext gctx = ctx;
            if (n.takesNotes)
            {
                const int first = n.noteCursor;
                while (n.noteCursor < n.notes.count && n.notes.events[static_cast<std::size_t> (n.noteCursor)].offset < end)
                    ++n.noteCursor;
                gctx.events = n.notes.events.data() + first;
                gctx.numEvents = n.noteCursor - first;
            }
            runInstance (n, n.instances[0], gctx);
        }
        for (const int f : g.feedbackNodes)
        {
            auto& n = g.nodes[static_cast<std::size_t> (f)];
            if (! n.instances.empty() && n.instances[0].processor != nullptr)
                n.instances[0].processor->writeFeedback (ctx, n.instances[0].io);
        }
        if (g.outputNode >= 0)
        {
            const auto& out = g.nodes[static_cast<std::size_t> (g.outputNode)].instances[0].outputs[0];
            for (int i = 0; i < len; ++i)
            {
                left[pos + i] += out.ch[0][static_cast<std::size_t> (i)];
                right[pos + i] += out.ch[1][static_cast<std::size_t> (i)];
            }
        }
        pos = end;
    }
    for (const auto& lane : g.lanes)
        voices += lane.activeVoices.load (std::memory_order_relaxed);
    if (&g == current)
        voicesNow.store (voices, std::memory_order_relaxed);

    if (&g == current)
    {
        if (! playing)
            freePpq += static_cast<double> (numSamples) / spq;
        else
            freePpq = timing.ppq + static_cast<double> (numSamples) / spq;
        lastPpqEnd = playing ? timing.ppq + static_cast<double> (numSamples) / spq : -1.0;
    }

    // Telemetry: each node's latest values (the most recent voice for voice nodes).
    for (auto& n : g.nodes)
    {
        if (n.desc == nullptr || n.instances.empty())
            continue;
        Telemetry t;
        if (n.domain == Domain::event)
        {
            if (n.eventProcessor != nullptr)
                n.eventProcessor->telemetry (t);
        }
        else
        {
            std::size_t pick = 0;
            if (n.domain == Domain::voice && n.lane >= 0)
            {
                std::uint64_t newest = 0;
                const auto& lane = g.lanes[static_cast<std::size_t> (n.lane)];
                for (std::size_t v = 0; v < lane.voices.size(); ++v)
                    if (lane.voices[v].state != LaneVoice::State::free && lane.voices[v].order >= newest)
                    {
                        newest = lane.voices[v].order;
                        pick = v;
                    }
            }
            if (pick < n.instances.size() && n.instances[pick].processor != nullptr)
                n.instances[pick].processor->telemetry (t);
        }
        for (int i = 0; i < t.count && i < 16; ++i)
            n.telemetry[static_cast<std::size_t> (i)].store (t.values[static_cast<std::size_t> (i)], std::memory_order_relaxed);
        n.telemetryCount.store (t.count, std::memory_order_relaxed);
    }
}

} // namespace andor::modular
