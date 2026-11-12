#include "nodes/NodeKit.h"

#include "core/Prng.h"
#include "engine/Arpeggiator.h"

#include <array>

namespace andor::modular
{
using namespace kit;

namespace
{
    std::vector<std::string> rateChoices()
    {
        std::vector<std::string> r;
        for (int i = 0; i < osp::arp::rateCount; ++i)
            r.emplace_back (osp::arp::rateName (static_cast<osp::ArpRate> (i)));
        return r;
    }

    double rateQuarters (int index) noexcept
    {
        return osp::arp::rateQuarters (static_cast<osp::ArpRate> (std::clamp (index, 0, osp::arp::rateCount - 1)));
    }

    /** Scales as semitone masks (bit n: degree n semitones above the root). */
    constexpr std::array<std::uint16_t, 9> scaleMasks {
        0b101010110101,   // major
        0b010110101101,   // natural minor
        0b011010101101,   // dorian
        0b001010010101,   // major pentatonic
        0b010010101001,   // minor pentatonic
        0b111111111111,   // chromatic
        0b100110101101,   // harmonic minor
        0b010101010101,   // whole tone
        0b011010110101    // mixolydian
    };
    std::vector<std::string> scaleChoices()
    {
        return { "MAJOR", "MINOR", "DORIAN", "PENTA MAJ", "PENTA MIN", "CHROMATIC", "HARM MINOR", "WHOLE TONE", "MIXOLYDIAN" };
    }

    int quantise (int note, int scale, int root) noexcept
    {
        const auto mask = scaleMasks[static_cast<std::size_t> (std::clamp (scale, 0, static_cast<int> (scaleMasks.size()) - 1))];
        for (int d = 0; d < 12; ++d)
            for (const int s : { -d, d })
            {
                const int n = note + s;
                const int degree = ((n - root) % 12 + 12) % 12;
                if ((mask >> degree) & 1u)
                    return std::clamp (n, 0, 127);
            }
        return std::clamp (note, 0, 127);
    }

    /** The i-th note of a scale counted from the root at `low` (degree may be negative). */
    int scaleDegree (int degree, int scale, int root, int low) noexcept
    {
        const auto mask = scaleMasks[static_cast<std::size_t> (std::clamp (scale, 0, static_cast<int> (scaleMasks.size()) - 1))];
        std::array<int, 12> steps {};
        int count = 0;
        for (int s = 0; s < 12; ++s)
            if ((mask >> s) & 1u)
                steps[static_cast<std::size_t> (count++)] = s;
        if (count == 0)
            return low;
        const int octave = degree >= 0 ? degree / count : -((-degree + count - 1) / count);
        const int index = degree - octave * count;
        const int base = low - ((low - root) % 12 + 12) % 12;
        return std::clamp (base + octave * 12 + steps[static_cast<std::size_t> (index)], 0, 127);
    }

    /** Note-offs scheduled past the block they were born in. */
    struct PendingOffs
    {
        struct Off
        {
            std::int64_t at = 0;
            std::uint8_t note = 0;
            std::uint32_t id = 0;
        };
        std::array<Off, 128> offs {};
        int count = 0;

        void add (std::int64_t at, int note, std::uint32_t id) noexcept
        {
            if (count < static_cast<int> (offs.size()))
                offs[static_cast<std::size_t> (count++)] = { at, static_cast<std::uint8_t> (note), id };
        }
        void emitDue (std::int64_t blockStart, int n, EventList& out) noexcept
        {
            for (int i = 0; i < count;)
            {
                auto& o = offs[static_cast<std::size_t> (i)];
                if (o.at < blockStart + n)
                {
                    NoteEvent e;
                    e.kind = NoteEvent::Kind::noteOff;
                    e.offset = static_cast<int> (std::clamp<std::int64_t> (o.at - blockStart, 0, n - 1));
                    e.note = o.note;
                    e.id = o.id;
                    out.add (e);
                    offs[static_cast<std::size_t> (i)] = offs[static_cast<std::size_t> (--count)];
                }
                else
                    ++i;
            }
        }
        void flush (int offset, EventList& out) noexcept
        {
            for (int i = 0; i < count; ++i)
            {
                NoteEvent e;
                e.kind = NoteEvent::Kind::noteOff;
                e.offset = offset;
                e.note = offs[static_cast<std::size_t> (i)].note;
                e.id = offs[static_cast<std::size_t> (i)].id;
                out.add (e);
            }
            count = 0;
        }
    };

    /** A musical step clock: host-synced while the transport plays, free-running otherwise. */
    struct StepClock
    {
        double freePpq = 0.0;
        bool wasPlaying = false;

        /** Calls fire(offset, stepIndex) for every step boundary in the block. */
        template <typename F>
        void run (const EventBlock& b, double stepQuarters, double swing, F&& fire) noexcept
        {
            const bool playing = b.timing.valid && b.timing.playing;
            const double bpm = b.timing.valid && b.timing.bpm > 0.0 ? b.timing.bpm : 120.0;
            const double spq = b.sampleRate * 60.0 / bpm;
            const double start = playing ? b.timing.ppq : freePpq;
            const double end = start + static_cast<double> (b.numSamples) / spq;
            // Steps whose (swung) time falls in [start, end).
            const auto first = static_cast<std::int64_t> (std::floor (start / stepQuarters)) - 1;
            const auto last = static_cast<std::int64_t> (std::floor (end / stepQuarters)) + 1;
            for (std::int64_t k = first; k <= last; ++k)
            {
                double t = static_cast<double> (k) * stepQuarters;
                if ((k & 1) != 0)
                    t += swing * 0.5 * stepQuarters;
                if (t >= start && t < end && k >= 0)
                    fire (static_cast<int> (std::clamp ((t - start) * spq, 0.0, static_cast<double> (b.numSamples - 1))), k);
            }
            freePpq = playing ? end : freePpq + static_cast<double> (b.numSamples) / spq;
            wasPlaying = playing;
        }
        double samplesPerStep (const EventBlock& b, double stepQuarters) const noexcept
        {
            const double bpm = b.timing.valid && b.timing.bpm > 0.0 ? b.timing.bpm : 120.0;
            return b.sampleRate * 60.0 / bpm * stepQuarters;
        }
    };

    /** Held keys (for sequencers that follow the keyboard). */
    struct HeldKeys
    {
        std::array<std::uint8_t, 32> notes {};
        std::array<std::uint32_t, 32> ids {};
        int count = 0;
        void apply (const NoteEvent& e) noexcept
        {
            if (e.kind == NoteEvent::Kind::allNotesOff)
            {
                count = 0;
                return;
            }
            if (e.kind == NoteEvent::Kind::noteOn)
            {
                if (count < 32)
                {
                    notes[static_cast<std::size_t> (count)] = e.note;
                    ids[static_cast<std::size_t> (count++)] = e.id;
                }
                return;
            }
            for (int i = 0; i < count; ++i)
                if (ids[static_cast<std::size_t> (i)] == e.id)
                {
                    for (int j = i; j + 1 < count; ++j)
                    {
                        notes[static_cast<std::size_t> (j)] = notes[static_cast<std::size_t> (j + 1)];
                        ids[static_cast<std::size_t> (j)] = ids[static_cast<std::size_t> (j + 1)];
                    }
                    --count;
                    return;
                }
        }
        int lowest() const noexcept
        {
            int lo = 128;
            for (int i = 0; i < count; ++i)
                lo = std::min<int> (lo, notes[static_cast<std::size_t> (i)]);
            return lo;
        }
    };

    //==========================================================================
    /** What a note module let through, by the note's identity: the pitch its note-on became.
        Its note-off ends exactly that, whatever the module's settings are by then (an octave,
        a range, a split point or a scale changed while a key is down must never leave a note
        hanging, and never end a different one). Fixed size, no allocation. */
    struct NoteMemory
    {
        void remember (std::uint32_t id, std::uint8_t note) noexcept
        {
            int slot = -1;
            for (int i = 0; i < size; ++i)
                if (used[static_cast<std::size_t> (i)] && ids[static_cast<std::size_t> (i)] == id)
                    slot = i;   // (the same identity again: its newest pitch)
            for (int i = 0; slot < 0 && i < size; ++i)
                if (! used[static_cast<std::size_t> (i)])
                    slot = i;
            if (slot < 0)
                slot = static_cast<int> (next++ % static_cast<unsigned> (size));   // (more than 128 held: the oldest slot goes)
            used[static_cast<std::size_t> (slot)] = true;
            ids[static_cast<std::size_t> (slot)] = id;
            notes[static_cast<std::size_t> (slot)] = note;
        }
        /** The pitch that identity sounds as, forgetting it; -1: its note-on never passed. */
        int take (std::uint32_t id) noexcept
        {
            for (int i = 0; i < size; ++i)
                if (used[static_cast<std::size_t> (i)] && ids[static_cast<std::size_t> (i)] == id)
                {
                    used[static_cast<std::size_t> (i)] = false;
                    return notes[static_cast<std::size_t> (i)];
                }
            return -1;
        }
        void clear() noexcept { used.fill (false); }
        static constexpr int size = 128;
        std::array<std::uint32_t, size> ids {};
        std::array<std::uint8_t, size> notes {};
        std::array<bool, size> used {};
        unsigned next = 0;
    };

    class Keyboard final : public EventProcessor
    {
    public:
        void reset() noexcept override { held.clear(); }
        void process (const EventBlock&, const EventList* const* inputs, int numInputs, const float* p, EventList& out, float* const*, int) noexcept override
        {
            if (numInputs < 1 || inputs[0] == nullptr)
                return;
            const int octave = static_cast<int> (p[0]);
            const int curve = static_cast<int> (p[1]);
            const int channel = static_cast<int> (p[2]);
            const int lo = static_cast<int> (p[3]), hi = static_cast<int> (p[4]);
            for (const auto& e : *inputs[0])
            {
                NoteEvent t = e;
                if (e.kind == NoteEvent::Kind::allNotesOff)
                    held.clear();
                else if (e.kind == NoteEvent::Kind::noteOn)
                {
                    // OCTAVE, RANGE and CHANNEL decide what a key *starts*: a held key keeps the
                    // pitch it started (a new octave is for the next notes).
                    if ((channel > 0 && e.channel != channel) || e.note < lo || e.note > hi)
                        continue;
                    const int n = e.note + 12 * octave;
                    if (n < 0 || n > 127)
                        continue;
                    t.note = static_cast<std::uint8_t> (n);
                    const float v = std::clamp (e.velocity, 0.0f, 1.0f);
                    t.velocity = curve == 0 ? std::sqrt (v) : (curve == 2 ? v * v : v);
                    held.remember (e.id, t.note);
                    ++notesIn;
                }
                else
                {
                    // The release of exactly what its key started; nothing if that never passed.
                    const int n = held.take (e.id);
                    if (n < 0)
                        continue;
                    t.note = static_cast<std::uint8_t> (n);
                }
                out.add (t);
            }
        }
        void telemetry (Telemetry& t) const noexcept override
        {
            t.values[0] = static_cast<float> (notesIn);
            t.count = 1;
        }

    private:
        int notesIn = 0;
        NoteMemory held;
    };

    //==========================================================================
    class Arp final : public EventProcessor
    {
    public:
        void prepare (const PrepareContext& pc) override
        {
            arp.prepare (pc.sampleRate);
            arp.setSeed (pc.seed);
        }
        void reset() noexcept override { arp.reset(); }

        void process (const EventBlock& b, const EventList* const* inputs, int numInputs, const float* p, EventList& out, float* const*, int) noexcept override
        {
            osp::Arpeggiator::Settings s;
            s.enabled = true;
            s.pattern = static_cast<osp::ArpPattern> (std::clamp (static_cast<int> (p[0]), 0, osp::arp::patternCount - 1));
            s.rate = static_cast<osp::ArpRate> (std::clamp (static_cast<int> (p[1]), 0, osp::arp::rateCount - 1));
            s.gate = std::clamp (static_cast<double> (p[2]), osp::arp::minGate, osp::arp::maxGate);
            s.swing = std::clamp (static_cast<double> (p[3]), 0.0, osp::arp::maxSwing);
            s.octaves = std::clamp (static_cast<int> (p[4]), osp::arp::minOctaves, osp::arp::maxOctaves);
            s.velocityScale = std::clamp (static_cast<double> (p[5]), 0.0, 2.0);
            if (b.transportStarted)
                arp.restartPattern();
            arp.beginBlock (s, b.timing, b.numSamples);
            if (numInputs > 0 && inputs[0] != nullptr)
                for (const auto& e : *inputs[0])
                {
                    arp.advanceTo (e.offset);
                    if (e.kind == NoteEvent::Kind::noteOn)
                        arp.noteOn (e.offset, e.note, std::clamp (static_cast<int> (e.velocity * 127.0f + 0.5f), 1, 127), e.channel);
                    else if (e.kind == NoteEvent::Kind::noteOff)
                        arp.noteOff (e.offset, e.note, e.channel);
                    else
                        arp.allNotesOff (e.offset);
                }
            arp.endBlock();
            for (int i = 0; i < arp.numEvents(); ++i)
            {
                const auto& a = arp.event (i);
                if (a.kind != osp::Arpeggiator::Event::Kind::noteOn && a.kind != osp::Arpeggiator::Event::Kind::noteOff)
                    continue;
                NoteEvent e;
                e.offset = std::clamp (a.offset, 0, b.numSamples - 1);
                e.kind = a.kind == osp::Arpeggiator::Event::Kind::noteOn ? NoteEvent::Kind::noteOn : NoteEvent::Kind::noteOff;
                e.note = a.note;
                e.channel = a.channel;
                e.velocity = static_cast<float> (a.velocity) / 127.0f;
                e.id = 0x10000u + a.note;   // the arpeggiator knows notes by pitch (it ends a pitch before replaying it)
                out.add (e);
            }
            arp.display (shown);
        }

        void telemetry (Telemetry& t) const noexcept override
        {
            t.values[0] = static_cast<float> (shown.current);
            t.values[1] = shown.active ? 1.0f : 0.0f;
            for (int i = 0; i < 14; ++i)
                t.values[static_cast<std::size_t> (2 + i)] = shown.low[static_cast<std::size_t> (i)];
            t.count = 16;
        }

    private:
        osp::Arpeggiator arp;
        osp::Arpeggiator::Display shown;
    };

    //==========================================================================
    constexpr int seqSteps = 16;

    class StepSequencer final : public EventProcessor
    {
    public:
        void prepare (const PrepareContext& pc) override { seed = pc.seed; }
        void reset() noexcept override { offs.count = 0; }

        void process (const EventBlock& b, const EventList* const* inputs, int numInputs, const float* p, EventList& out, float* const* control,
                      int numControl) noexcept override
        {
            const int length = std::clamp (static_cast<int> (p[0]), 1, seqSteps);
            const double stepQ = rateQuarters (static_cast<int> (p[1]));
            const double gate = std::clamp (static_cast<double> (p[2]), 0.05, 1.5);
            const double swing = std::clamp (static_cast<double> (p[3]), 0.0, 1.0);
            const int root = static_cast<int> (p[4]);
            const bool follow = p[5] >= 0.5f;
            const float probability = std::clamp (p[6], 0.0f, 1.0f);
            const float* pitch = p + 7;
            const float* gates = p + 7 + seqSteps;
            const float* velocity = p + 7 + 2 * seqSteps;

            const bool keysConnected = numInputs > 0 && inputs[0] != nullptr;
            if (keysConnected)
                for (const auto& e : *inputs[0])
                    keys.apply (e);
            const bool run = ! follow || ! keysConnected || keys.count > 0;
            const int transpose = follow && keysConnected && keys.count > 0 ? keys.lowest() - 60 : 0;

            offs.emitDue (b.samplePosition, b.numSamples, out);
            if (! run && offs.count > 0)
                offs.flush (0, out);
            const double sps = clock.samplesPerStep (b, stepQ);
            clock.run (b, stepQ, swing, [&] (int offset, std::int64_t k) {
                const int step = static_cast<int> (k % length);
                current = step;
                if (! run)
                    return;
                const auto si = static_cast<std::size_t> (step);
                if (gates[si] < 0.5f)
                    return;
                osp::Prng chance (osp::Prng::deriveSeed (seed, static_cast<std::uint64_t> (k), 17));
                if (probability < 1.0f && chance.nextDouble() >= probability)
                    return;
                const int note = std::clamp (root + static_cast<int> (std::lround (pitch[si])) + transpose, 0, 127);
                NoteEvent e;
                e.offset = offset;
                e.kind = NoteEvent::Kind::noteOn;
                e.note = static_cast<std::uint8_t> (note);
                e.velocity = std::clamp (velocity[si], 0.01f, 1.0f);
                e.id = 0x20000u + static_cast<std::uint32_t> (k & 0xFFFF);
                out.add (e);
                offs.add (b.samplePosition + offset + static_cast<std::int64_t> (std::max (1.0, gate * sps)), note, e.id);
                lastValue = velocity[si];
            });
            offs.emitDue (b.samplePosition, b.numSamples, out);
            // Control lanes: the step's velocity (0..1) and its gate.
            if (numControl > 0)
                std::fill (control[0], control[0] + b.numSamples, lastValue);
            if (numControl > 1)
                std::fill (control[1], control[1] + b.numSamples, offs.count > 0 ? 1.0f : 0.0f);
        }

        void telemetry (Telemetry& t) const noexcept override
        {
            t.values[0] = static_cast<float> (current);
            t.values[1] = lastValue;
            t.count = 2;
        }

    private:
        StepClock clock;
        PendingOffs offs;
        HeldKeys keys;
        std::uint64_t seed = 1;
        int current = -1;
        float lastValue = 0.0f;
    };

    //==========================================================================
    class Generative final : public EventProcessor
    {
    public:
        static constexpr int maxLoop = 128;

        void prepare (const PrepareContext& pc) override { seed = pc.seed; }
        void reset() noexcept override { offs.count = 0; }

        void process (const EventBlock& b, const EventList* const* inputs, int numInputs, const float* p, EventList& out, float* const* control,
                      int numControl) noexcept override
        {
            const double stepQ = rateQuarters (static_cast<int> (p[0]));
            const float density = std::clamp (p[1], 0.0f, 1.0f);
            const int scale = static_cast<int> (p[2]);
            const int root = static_cast<int> (p[3]);
            const int low = static_cast<int> (p[4]);
            const int range = std::clamp (static_cast<int> (p[5]), 1, 4);
            const float stepwise = std::clamp (p[6], 0.0f, 1.0f);
            const double gate = std::clamp (static_cast<double> (p[7]), 0.05, 1.5);
            const int bars = std::clamp (static_cast<int> (p[8]), 1, 8);
            const int variation = static_cast<int> (p[9]);
            const bool follow = p[10] >= 0.5f;

            // The loop is regenerated only when what defines it changes (fixed arrays, no allocation).
            const int loopSteps = std::clamp (static_cast<int> (std::lround (4.0 * bars / stepQ)), 1, maxLoop);
            const std::uint64_t signature = osp::Prng::deriveSeed (seed + static_cast<std::uint64_t> (variation),
                                                                  static_cast<std::uint64_t> (scale * 131 + root * 7 + low * 3 + range + loopSteps * 1009),
                                                                  static_cast<std::uint64_t> (std::lround (density * 1000.0f) * 7 + std::lround (stepwise * 1000.0f)));
            if (signature != builtFor)
            {
                builtFor = signature;
                osp::Prng r (signature);
                int degree = 0;
                const int degreesInRange = range * 7;
                for (int k = 0; k < loopSteps; ++k)
                {
                    const bool strong = (k % 4) == 0;
                    const double chance = std::clamp (static_cast<double> (density) * (strong ? 1.25 : 0.85), 0.0, 1.0);
                    hits[static_cast<std::size_t> (k)] = r.nextDouble() < chance;
                    if (r.nextDouble() < stepwise)
                        degree += r.nextDouble() < 0.5 ? -1 : 1;
                    else
                        degree = static_cast<int> (r.nextBelow (static_cast<std::uint64_t> (degreesInRange)));
                    degree = std::clamp (degree, 0, degreesInRange - 1);
                    loop[static_cast<std::size_t> (k)] = static_cast<std::uint8_t> (scaleDegree (degree, scale, root, low));
                    accents[static_cast<std::size_t> (k)] = static_cast<float> (strong ? 0.85 + 0.15 * r.nextDouble() : 0.55 + 0.25 * r.nextDouble());
                }
            }
            const bool keysConnected = numInputs > 0 && inputs[0] != nullptr;
            if (keysConnected)
                for (const auto& e : *inputs[0])
                    keys.apply (e);
            const bool run = ! follow || ! keysConnected || keys.count > 0;
            const int transpose = follow && keysConnected && keys.count > 0 ? keys.lowest() - 60 : 0;

            offs.emitDue (b.samplePosition, b.numSamples, out);
            if (! run && offs.count > 0)
                offs.flush (0, out);
            const double sps = clock.samplesPerStep (b, stepQ);
            clock.run (b, stepQ, 0.0, [&] (int offset, std::int64_t k) {
                const auto i = static_cast<std::size_t> (k % loopSteps);
                current = static_cast<int> (i);
                if (! run || ! hits[i])
                    return;
                const int note = std::clamp (static_cast<int> (loop[i]) + transpose, 0, 127);
                NoteEvent e;
                e.offset = offset;
                e.kind = NoteEvent::Kind::noteOn;
                e.note = static_cast<std::uint8_t> (note);
                e.velocity = accents[i];
                e.id = 0x30000u + static_cast<std::uint32_t> (k & 0xFFFF);
                out.add (e);
                offs.add (b.samplePosition + offset + static_cast<std::int64_t> (std::max (1.0, gate * sps)), note, e.id);
                lastNote = note;
            });
            offs.emitDue (b.samplePosition, b.numSamples, out);
            if (numControl > 0)
                std::fill (control[0], control[0] + b.numSamples, static_cast<float> (lastNote - 60) / 12.0f);
        }

        void telemetry (Telemetry& t) const noexcept override
        {
            t.values[0] = static_cast<float> (current);
            t.values[1] = static_cast<float> (lastNote);
            t.count = 2;
        }

    private:
        StepClock clock;
        PendingOffs offs;
        HeldKeys keys;
        std::uint64_t seed = 1, builtFor = 0;
        std::array<std::uint8_t, maxLoop> loop {};
        std::array<bool, maxLoop> hits {};
        std::array<float, maxLoop> accents {};
        int current = -1, lastNote = 60;
    };

    //==========================================================================
    class Merge final : public EventProcessor
    {
    public:
        void process (const EventBlock&, const EventList* const* inputs, int numInputs, const float* p, EventList& out, float* const*, int) noexcept override
        {
            const int policy = static_cast<int> (p[0]);
            EventList& all = scratch;
            all.clear();
            for (int i = 0; i < numInputs; ++i)
                if (inputs[i] != nullptr)
                    for (const auto& e : *inputs[i])
                    {
                        NoteEvent t = e;
                        t.id = e.id * 4u + static_cast<std::uint32_t> (i);   // distinct per input (MERGE)
                        all.add (t);
                    }
            all.sort();
            for (const auto& e : all)
            {
                if (policy == 0 || e.kind == NoteEvent::Kind::allNotesOff)
                {
                    out.add (e);
                    continue;
                }
                // REPLACE / HIGHEST / LOWEST: one note at a time.
                if (e.kind == NoteEvent::Kind::noteOn)
                {
                    const bool wins = soundingId == 0 || policy == 1 || (policy == 2 && e.note >= soundingNote) || (policy == 3 && e.note <= soundingNote);
                    if (! wins)
                        continue;
                    if (soundingId != 0)
                    {
                        NoteEvent off = e;
                        off.kind = NoteEvent::Kind::noteOff;
                        off.id = soundingId;
                        off.note = soundingNote;
                        out.add (off);
                    }
                    soundingId = e.id;
                    soundingNote = e.note;
                    out.add (e);
                }
                else if (e.id == soundingId)
                {
                    out.add (e);
                    soundingId = 0;
                }
            }
        }

    private:
        EventList scratch;
        std::uint32_t soundingId = 0;
        std::uint8_t soundingNote = 0;
    };

    //==========================================================================
    class Split final : public EventProcessor
    {
    public:
        void process (const EventBlock&, const EventList* const* inputs, int numInputs, const float* p, EventList& out, float* const*, int) noexcept override
        {
            // Two outputs share one list here: low notes keep their ids, high notes are marked
            // (bit 31) and the engine routes by port... a single notes output per node keeps the
            // event model simple, so SPLIT passes the chosen side (p[1]: 0 low, 1 high).
            if (numInputs < 1 || inputs[0] == nullptr)
                return;
            const int key = static_cast<int> (p[0]);
            const bool high = p[1] >= 0.5f;
            for (const auto& e : *inputs[0])
            {
                if (e.kind == NoteEvent::Kind::allNotesOff)
                    held.clear();
                else if (e.kind == NoteEvent::Kind::noteOn)
                {
                    if ((e.note >= key) != high)
                        continue;
                    held.remember (e.id, e.note);
                }
                else if (held.take (e.id) < 0)
                    continue;   // (its note-on went to the other side, or never came)
                out.add (e);
            }
        }
        void reset() noexcept override { held.clear(); }

    private:
        NoteMemory held;
    };

    //==========================================================================
    class ScaleTranspose final : public EventProcessor
    {
    public:
        void process (const EventBlock&, const EventList* const* inputs, int numInputs, const float* p, EventList& out, float* const*, int) noexcept override
        {
            if (numInputs < 1 || inputs[0] == nullptr)
                return;
            const int shift = static_cast<int> (p[0]) + 12 * static_cast<int> (p[1]);
            const bool quantiseOn = p[2] >= 0.5f;
            const int scale = static_cast<int> (p[3]);
            const int root = static_cast<int> (p[4]);
            for (const auto& e : *inputs[0])
            {
                NoteEvent t = e;
                if (e.kind == NoteEvent::Kind::noteOn)
                {
                    int n = e.note + shift;
                    if (quantiseOn)
                        n = quantise (n, scale, root);
                    if (n < 0 || n > 127)
                        continue;
                    t.note = static_cast<std::uint8_t> (n);
                    held.remember (e.id, t.note);
                }
                else if (e.kind == NoteEvent::Kind::noteOff)
                {
                    // The note-off ends the pitch its note-on became (a scale or a transpose
                    // changed in between must not leave it hanging, nor end another note).
                    const int n = held.take (e.id);
                    if (n < 0)
                        continue;
                    t.note = static_cast<std::uint8_t> (n);
                }
                else
                    held.clear();
                out.add (t);
            }
        }
        void reset() noexcept override { held.clear(); }

    private:
        NoteMemory held;
    };

    //==========================================================================
    class Clock final : public EventProcessor
    {
    public:
        void process (const EventBlock& b, const EventList* const*, int, const float* p, EventList&, float* const* control, int numControl) noexcept override
        {
            const double stepQ = rateQuarters (static_cast<int> (p[0]));
            const double swing = std::clamp (static_cast<double> (p[1]), 0.0, 1.0);
            const bool playing = b.timing.valid && b.timing.playing;
            const double bpm = b.timing.valid && b.timing.bpm > 0.0 ? b.timing.bpm : 120.0;
            const double spq = b.sampleRate * 60.0 / bpm;
            const double start = playing ? b.timing.ppq : clock.freePpq;
            for (int c = 0; c < numControl; ++c)
                std::fill (control[c], control[c] + b.numSamples, 0.0f);
            // phase ramp (0..1 per step), beat (quarters) and bar triggers
            const int bar = std::max (1, b.timing.numerator) * 4 / std::max (1, b.timing.denominator);
            for (int i = 0; i < b.numSamples; ++i)
            {
                const double q = start + static_cast<double> (i) / spq;
                if (numControl > 1)
                    control[1][i] = static_cast<float> (q / stepQ - std::floor (q / stepQ));
                const double prev = q - 1.0 / spq;
                if (numControl > 2 && std::floor (q) != std::floor (prev))
                    control[2][i] = 1.0f;
                if (numControl > 3 && std::floor (q / bar) != std::floor (prev / bar))
                    control[3][i] = 1.0f;
            }
            clock.run (b, stepQ, swing, [&] (int offset, std::int64_t k) {
                if (numControl > 0)
                    control[0][offset] = 1.0f;
                ticks = k;
            });
        }
        void telemetry (Telemetry& t) const noexcept override
        {
            t.values[0] = static_cast<float> (ticks % 16);
            t.count = 1;
        }

    private:
        StepClock clock;
        std::int64_t ticks = 0;
    };
}

void registerEventNodes (Registry& r)
{
    auto base = [] (const char* type, const char* name, const char* description) {
        NodeDesc d;
        d.type = type;
        d.name = name;
        d.description = description;
        d.category = "Controllers";
        d.domains = eventOnly;
        d.defaultDomain = Domain::event;
        d.role = NodeRole::event;
        d.cost.units = 0.05f;
        return d;
    };
    auto noModulation = [] (NodeDesc& d) {
        for (auto& p : d.params)
            p.modulatable = false;   // event nodes run before audio, once per block: macros, scenes and automation only
    };

    {
        auto d = base ("event.keyboard", "KEYBOARD", "The host's MIDI notes: velocity curve, octave, channel and key range");
        d.keywords = { "midi", "input", "keys", "notes" };
        d.help = "Every patch starts here: notes from the keyboard or the DAW. A new sound source is connected to it automatically.";
        d.outputs = { notesOut() };
        d.params = { integer ("octave", "OCTAVE", -3, 3, 0, "oct", true), choice ("curve", "VELOCITY", { "SOFT", "LINEAR", "HARD" }, 1, true),
                     integer ("channel", "CHANNEL", 0, 16, 0), integer ("low", "LOW KEY", 0, 127, 0), integer ("high", "HIGH KEY", 0, 127, 127) };
        d.createEvent = makeEvent<Keyboard>();
        noModulation (d);
        r.add (d);
    }
    {
        auto d = base ("event.arp", "ARPEGGIATOR", "Turns held notes into patterns: 18 styles, synced to the host");
        d.keywords = { "arp", "pattern", "sequence", "rhythm" };
        d.help = "ANDOR OSP's arpeggiator as a module. Each ARP can feed any number of sources, and several ARPs may feed one source "
                 "(their notes merge; one ARP's note-off never cuts another's note).";
        d.inputs = { notesIn() };
        d.outputs = { notesOut() };
        std::vector<std::string> patterns;
        for (int i = 0; i < osp::arp::patternCount; ++i)
            patterns.emplace_back (osp::arp::patternName (static_cast<osp::ArpPattern> (i)));
        d.params = { choice ("pattern", "PATTERN", patterns, 0, true), choice ("rate", "RATE", rateChoices(), 1, true),
                     num ("gate", "GATE", osp::arp::minGate, osp::arp::maxGate, 0.75, "%", true), num ("swing", "SWING", 0.0, 1.0, 0.0, "%"),
                     integer ("octaves", "OCTAVES", 1, 4, 1, "", true), num ("velocity", "VELOCITY", 0.0, 2.0, 1.0, "%") };
        d.isArp = true;
        d.createEvent = makeEvent<Arp>();
        noModulation (d);
        r.add (d);
    }
    {
        auto d = base ("event.stepseq", "STEP SEQUENCER", "16 steps of pitch, gate and velocity; probability; follows the keys");
        d.keywords = { "sequencer", "steps", "pattern", "seq" };
        d.help = "Steps sit on the host's grid (free-running at the last tempo when stopped). FOLLOW KEYS plays only while keys are held, "
                 "transposed by the lowest key (C3 = no change). Its VALUE and GATE outputs are control lanes.";
        d.inputs = { notesIn() };
        d.outputs = { notesOut(), out ("value", "VALUE", PortKind::control, Polarity::unipolar), out ("gate", "GATE", PortKind::gate, Polarity::unipolar) };
        d.params = { integer ("length", "LENGTH", 1, seqSteps, 8, "", true), choice ("rate", "RATE", rateChoices(), 2, true),
                     num ("gate", "GATE", 0.05, 1.5, 0.6, "%"), num ("swing", "SWING", 0.0, 1.0, 0.0, "%"),
                     integer ("root", "ROOT", 24, 96, 48, "", true), toggle ("follow", "FOLLOW KEYS", false), num ("probability", "PROBABILITY", 0.0, 1.0, 1.0, "%") };
        const int pattern[seqSteps] = { 0, 12, 7, 3, 0, 10, 7, 5, 0, 12, 7, 3, 0, 15, 14, 10 };
        for (int s = 0; s < seqSteps; ++s)
            d.params.push_back (integer ("p" + std::to_string (s + 1), "STEP " + std::to_string (s + 1), -24, 24, pattern[s], "st"));
        for (int s = 0; s < seqSteps; ++s)
            d.params.push_back (toggle ("g" + std::to_string (s + 1), "GATE " + std::to_string (s + 1), true));
        for (int s = 0; s < seqSteps; ++s)
            d.params.push_back (num ("v" + std::to_string (s + 1), "VEL " + std::to_string (s + 1), 0.0, 1.0, (s % 4) == 0 ? 0.95 : 0.7, "%"));
        d.createEvent = makeEvent<StepSequencer>();
        noModulation (d);
        r.add (d);
    }
    {
        auto d = base ("event.generative", "GENERATIVE", "Seeded, musically bounded melodies in a scale: repeatable, never random noise");
        d.keywords = { "generative", "random", "melody", "sequence", "seed" };
        d.help = "A loop of BARS length is drawn from the patch seed and VARIATION: the same settings always play the same notes "
                 "(bounces repeat exactly). DENSITY favours strong beats; STEPWISE prefers neighbouring scale degrees.";
        d.inputs = { notesIn() };
        d.outputs = { notesOut(), out ("pitch", "PITCH", PortKind::pitch) };
        d.params = { choice ("rate", "RATE", rateChoices(), 2, true), num ("density", "DENSITY", 0.0, 1.0, 0.55, "%", true),
                     choice ("scale", "SCALE", scaleChoices(), 3, true), integer ("root", "ROOT", 0, 11, 0), integer ("low", "LOW NOTE", 24, 96, 55),
                     integer ("range", "RANGE", 1, 4, 2, "oct"), num ("stepwise", "STEPWISE", 0.0, 1.0, 0.65, "%"), num ("gate", "GATE", 0.05, 1.5, 0.7, "%"),
                     integer ("bars", "BARS", 1, 8, 2), integer ("variation", "VARIATION", 0, 9999, 0, "", true), toggle ("follow", "FOLLOW KEYS", false) };
        d.createEvent = makeEvent<Generative>();
        noModulation (d);
        r.add (d);
    }
    {
        auto d = base ("event.merge", "NOTE MERGE", "Joins note streams: MERGE keeps every note; REPLACE / HIGHEST / LOWEST play one");
        d.category = "Utilities";
        d.keywords = { "merge", "combine", "notes", "priority" };
        d.inputs = { in ("a", "A", PortKind::notes, true), in ("b", "B", PortKind::notes, true), in ("c", "C", PortKind::notes, true), in ("d", "D", PortKind::notes, true) };
        d.outputs = { notesOut() };
        d.params = { choice ("policy", "POLICY", { "MERGE", "REPLACE", "HIGHEST", "LOWEST" }, 0, true) };
        d.createEvent = makeEvent<Merge>();
        noModulation (d);
        r.add (d);
    }
    {
        auto d = base ("event.split", "KEY SPLIT", "Passes the notes below or above a split key");
        d.category = "Utilities";
        d.keywords = { "split", "zone", "range", "keys" };
        d.inputs = { notesIn() };
        d.outputs = { notesOut() };
        d.params = { integer ("key", "SPLIT KEY", 0, 127, 60, "", true), choice ("side", "SIDE", { "BELOW", "ABOVE" }, 0, true) };
        d.createEvent = makeEvent<Split>();
        noModulation (d);
        r.add (d);
    }
    {
        auto d = base ("event.scale", "SCALE / TRANSPOSE", "Transposes notes and snaps them to a scale");
        d.category = "Utilities";
        d.keywords = { "quantize", "quantise", "scale", "transpose", "octave", "pitch" };
        d.inputs = { notesIn() };
        d.outputs = { notesOut() };
        d.params = { integer ("semitones", "TRANSPOSE", -24, 24, 0, "st", true), integer ("octave", "OCTAVE", -3, 3, 0, "oct", true),
                     toggle ("quantise", "QUANTISE", true, true), choice ("scale", "SCALE", scaleChoices(), 0, true), integer ("root", "ROOT", 0, 11, 0) };
        d.createEvent = makeEvent<ScaleTranspose>();
        noModulation (d);
        r.add (d);
    }
    {
        auto d = base ("event.clock", "CLOCK", "Musical pulses from the host tempo: clock, phase, beat and bar");
        d.keywords = { "clock", "tempo", "sync", "pulse", "beat" };
        d.outputs = { out ("clock", "CLOCK", PortKind::clock, Polarity::unipolar), out ("phase", "PHASE", PortKind::control, Polarity::unipolar),
                      out ("beat", "BEAT", PortKind::trigger, Polarity::unipolar), out ("bar", "BAR", PortKind::trigger, Polarity::unipolar) };
        d.params = { choice ("rate", "RATE", rateChoices(), 2, true), num ("swing", "SWING", 0.0, 1.0, 0.0, "%") };
        d.createEvent = makeEvent<Clock>();
        noModulation (d);
        r.add (d);
    }
}

} // namespace andor::modular
